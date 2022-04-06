// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/deopt.h"

#include "pycore_shadow_frame.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/hir/analysis.h"
#include "Jit/jit_rt.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

using jit::codegen::PhyLocation;

namespace jit {

namespace {
// Set of interned strings for deopt descriptions.
std::unordered_set<std::string> s_descrs;
} // namespace

hir::ValueKind deoptValueKind(hir::Type type) {
  if (type <= jit::hir::TCBool) {
    return jit::hir::ValueKind::kBool;
  }

  if (type <= jit::hir::TCDouble) {
    return jit::hir::ValueKind::kDouble;
  }

  if (type <= jit::hir::TCEnum) {
    return jit::hir::ValueKind::kSigned;
  }

  // TODO(bsimmers): The type predicates here are gross and indicate a deeper
  // problem with how we're using Types earlier in the pipeline: we use
  // `LoadNull` to zero-initialize locals with primitive types (currently done
  // in SSAify). It works fine at runtime and a proper fix likely involves
  // reworking HIR's support for constant values, so we paper over the issue
  // here for the moment.
  if (type.couldBe(jit::hir::TCUnsigned | jit::hir::TCSigned)) {
    if (type <= (jit::hir::TCUnsigned | jit::hir::TNullptr)) {
      return jit::hir::ValueKind::kUnsigned;
    }
    if (type <= (jit::hir::TCSigned | jit::hir::TNullptr)) {
      return jit::hir::ValueKind::kSigned;
    }
  } else if (type.couldBe(jit::hir::TCDouble)) {
    return jit::hir::ValueKind::kDouble;
  }

  JIT_CHECK(
      type <= jit::hir::TOptObject, "Unexpected type %s in deopt value", type);
  return jit::hir::ValueKind::kObject;
}

const char* deoptReasonName(DeoptReason reason) {
  switch (reason) {
#define REASON(name)         \
  case DeoptReason::k##name: \
    return #name;
    DEOPT_REASONS(REASON)
#undef REASON
  }
  JIT_CHECK(false, "Invalid DeoptReason %d", static_cast<int>(reason));
}

const char* deoptActionName(DeoptAction action) {
  switch (action) {
    case DeoptAction::kResumeInInterpreter:
      return "ResumeInInterpreter";
    case DeoptAction::kUnwind:
      return "Unwind";
  }
  JIT_CHECK(false, "Invalid DeoptAction %d", static_cast<int>(action));
}

PyObject* MemoryView::read(const LiveValue& value, bool borrow) const {
  uint64_t raw;
  PhyLocation loc = value.location;
  if (loc.is_register()) {
    raw = regs[loc.loc];
  } else {
    uint64_t rbp = regs[PhyLocation::RBP];
    // loc.loc is negative when loc is a memory location relative to RBP
    raw = *(reinterpret_cast<uint64_t*>(rbp + loc.loc));
  }

  switch (value.value_kind) {
    case jit::hir::ValueKind::kSigned:
      JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
      return PyLong_FromSsize_t((Py_ssize_t)raw);
    case jit::hir::ValueKind::kUnsigned:
      JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
      return PyLong_FromSize_t(raw);
    case hir::ValueKind::kDouble:
      JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
      return PyFloat_FromDouble(raw);
    case jit::hir::ValueKind::kBool: {
      JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
      PyObject* res = raw ? Py_True : Py_False;
      Py_INCREF(res);
      return res;
    }
    case jit::hir::ValueKind::kObject: {
      PyObject* res = reinterpret_cast<PyObject*>(raw);
      if (!borrow) {
        Py_XINCREF(res);
      }
      return res;
    }
  }
  return nullptr;
}

static void reifyLocalsplus(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const MemoryView& mem) {
  for (std::size_t i = 0; i < frame_meta.localsplus.size(); i++) {
    auto value = meta.getLocalValue(i, frame_meta);
    if (value == nullptr) {
      // Value is dead
      Py_CLEAR(frame->f_localsplus[i]);
      continue;
    }
    PyObject* obj = mem.read(*value);
    Py_XSETREF(frame->f_localsplus[i], obj);
  }
}

// When we are deoptimizing a JIT-compiled function that contains an
// optimizable LoadMethod, we need to be able to know whether or not the
// LoadMethod returned a bound method object in order to properly reconstruct
// the stack for the interpreter. Unfortunately, this can only be determined at
// runtime, and can be inferred from the value pointed to by call_method_kind.
static bool isUnboundMethod(
    const LiveValue& value,
    const JITRT_CallMethodKind* call_method_kind) {
  if (value.source != LiveValue::Source::kOptimizableLoadMethod) {
    return false;
  }
  return *call_method_kind != JITRT_CALL_KIND_OTHER;
}

static void reifyStack(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const MemoryView& mem,
    const JITRT_CallMethodKind* call_method_kind) {
  frame->f_stacktop = frame->f_valuestack + frame_meta.stack.size();
  for (int i = frame_meta.stack.size() - 1; i >= 0; i--) {
    const auto& value = meta.getStackValue(i, frame_meta);
    if (value.isLoadMethodResult()) {
      PyObject* callable = mem.read(value);
      if (isUnboundMethod(value, call_method_kind)) {
        // We avoided creating a bound method object. The interpreter
        // expects the stack to look like:
        //
        // callable
        // self
        // arg1
        // ...
        // argN       <-- TOS
        PyObject* receiver = mem.read(meta.getStackValue(i - 1, frame_meta));
        frame->f_valuestack[i - 1] = callable;
        frame->f_valuestack[i] = receiver;
      } else {
        // Otherwise, the interpreter expects the stack look like:
        //
        // nullptr
        // bound_method
        // arg1
        // ...
        // argN       <-- TOS
        frame->f_valuestack[i - 1] = nullptr;
        frame->f_valuestack[i] = callable;
      }
      i--;
    } else {
      PyObject* obj = mem.read(value);
      frame->f_valuestack[i] = obj;
    }
  }
}

Ref<> profileDeopt(
    std::size_t deopt_idx,
    const DeoptMetadata& meta,
    const MemoryView& mem) {
  const LiveValue* live_val = meta.getGuiltyValue();
  auto guilty_obj =
      Ref<>::steal(live_val == nullptr ? nullptr : mem.read(*live_val, false));
  Runtime::get()->recordDeopt(deopt_idx, guilty_obj.get());
  return guilty_obj;
}

static void reifyBlockStack(
    PyFrameObject* frame,
    const jit::hir::BlockStack& block_stack) {
  std::size_t bs_size = block_stack.size();
  frame->f_iblock = bs_size;
  for (std::size_t i = 0; i < bs_size; i++) {
    const auto& block = block_stack.at(i);
    frame->f_blockstack[i].b_type = block.opcode;
    frame->f_blockstack[i].b_handler = block.handler_off;
    frame->f_blockstack[i].b_level = block.stack_level;
  }
}

void reifyFrame(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const uint64_t* regs,
    const JITRT_CallMethodKind* call_method_kind) {
  frame->f_locals = NULL;
  frame->f_trace = NULL;
  frame->f_trace_opcodes = 0;
  frame->f_trace_lines = 1;
  frame->f_executing = 0;
  // Interpreter loop will handle filling this in
  frame->f_lineno = frame->f_code->co_firstlineno;
  // Instruction pointer
  if (frame_meta.next_instr_offset == 0) {
    frame->f_lasti = -1;
  } else {
    frame->f_lasti = frame_meta.next_instr_offset - sizeof(_Py_CODEUNIT);
  }
  MemoryView mem{regs};
  reifyLocalsplus(frame, meta, frame_meta, mem);
  reifyStack(frame, meta, frame_meta, mem, call_method_kind);
  reifyBlockStack(frame, frame_meta.block_stack);
  // Generator/frame linkage happens in `materializePyFrame` in frame.cpp
}

static DeoptReason getDeoptReason(const jit::hir::DeoptBase& instr) {
  switch (instr.opcode()) {
    case jit::hir::Opcode::kCheckVar: {
      return DeoptReason::kUnhandledUnboundLocal;
    }
    case jit::hir::Opcode::kCheckFreevar: {
      return DeoptReason::kUnhandledUnboundFreevar;
    }
    case jit::hir::Opcode::kCheckField: {
      return DeoptReason::kUnhandledNullField;
    }
    case jit::hir::Opcode::kDeopt:
    case jit::hir::Opcode::kDeoptPatchpoint:
    case jit::hir::Opcode::kGuard:
    case jit::hir::Opcode::kGuardIs:
    case jit::hir::Opcode::kGuardType: {
      return DeoptReason::kGuardFailure;
    }
    case jit::hir::Opcode::kRaise: {
      auto& raise = static_cast<const hir::Raise&>(instr);
      switch (raise.kind()) {
        case hir::Raise::Kind::kReraise:
          return DeoptReason::kReraise;
        case hir::Raise::Kind::kRaiseWithExc:
        case hir::Raise::Kind::kRaiseWithExcAndCause:
          return DeoptReason::kRaise;
      }
      JIT_CHECK(false, "invalid raise kind");
    }
    case jit::hir::Opcode::kRaiseStatic: {
      return DeoptReason::kRaiseStatic;
    }
    default: {
      return DeoptReason::kUnhandledException;
    }
  }
}

DeoptMetadata DeoptMetadata::fromInstr(
    const jit::hir::DeoptBase& instr,
    const std::unordered_set<const jit::hir::Instr*>& optimizable_lms,
    CodeRuntime* code_rt) {
  auto get_source = [&](jit::hir::Register* reg) {
    reg = hir::modelReg(reg);
    auto instr = reg->instr();
    if (instr->IsLoadMethod()) {
      if (optimizable_lms.count(instr)) {
        return LiveValue::Source::kOptimizableLoadMethod;
      } else {
        return LiveValue::Source::kUnoptimizableLoadMethod;
      }
    }
    return LiveValue::Source::kUnknown;
  };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  DeoptMetadata meta{.code_rt = code_rt};
#pragma GCC diagnostic pop

  std::unordered_map<jit::hir::Register*, int> reg_idx;
  int i = 0;
  for (const auto& reg_state : instr.live_regs()) {
    auto reg = reg_state.reg;

    LiveValue lv = {
        // location will be filled in once we've generated code
        .location = 0,
        .ref_kind = reg_state.ref_kind,
        .value_kind = reg_state.value_kind,
        .source = get_source(reg),
    };
    meta.live_values.emplace_back(std::move(lv));
    reg_idx[reg] = i;
    i++;
  }

  auto get_reg_idx = [&reg_idx](jit::hir::Register* reg) {
    if (reg == nullptr) {
      return -1;
    }
    auto it = reg_idx.find(reg);
    JIT_CHECK(it != reg_idx.end(), "register %s not live", reg->name());
    return it->second;
  };

  auto populate_localsplus =
      [get_reg_idx](DeoptFrameMetadata& meta, hir::FrameState* fs) {
        std::size_t nlocals = fs->locals.size();
        std::size_t ncells = fs->cells.size();
        meta.localsplus.resize(nlocals + ncells, -1);
        for (std::size_t i = 0; i < nlocals; i++) {
          meta.localsplus[i] = get_reg_idx(fs->locals[i]);
        }
        for (std::size_t i = 0; i < ncells; i++) {
          meta.localsplus[nlocals + i] = get_reg_idx(fs->cells[i]);
        }
      };

  auto populate_stack = [get_reg_idx](
                            DeoptFrameMetadata& meta, hir::FrameState* fs) {
    std::unordered_set<jit::hir::Register*> lms_on_stack;
    for (auto& reg : fs->stack) {
      if (reg->instr()->IsLoadMethod()) {
        // Our logic for reconstructing the Python stack assumes that if a
        // value on the stack was produced by a LoadMethod instruction, it
        // corresponds to the output of a LOAD_METHOD opcode and will
        // eventually be consumed by a CALL_METHOD. That doesn't technically
        // have to be true, but it's our contention that the CPython
        // compiler will never produce bytecode that would contradict this.
        auto result = lms_on_stack.emplace(reg);
        JIT_CHECK(
            result.second,
            "load method results may only appear in one stack slot");
      }
      meta.stack.emplace_back(get_reg_idx(reg));
    }
  };

  auto fs = instr.frameState();

  meta.inline_depth = fs->inlineDepth();
  int num_frames = meta.inline_depth;
  meta.frame_meta.resize(num_frames + 1); // +1 for caller
  for (hir::FrameState* frame = fs; frame != NULL; frame = frame->parent) {
    int i = num_frames--;
    // Translate locals and cells
    populate_localsplus(meta.frame_meta.at(i), frame);
    populate_stack(meta.frame_meta.at(i), frame);
    meta.frame_meta.at(i).block_stack = frame->block_stack;
    meta.frame_meta.at(i).next_instr_offset = frame->next_instr_offset;
    meta.frame_meta.at(i).code = frame->code.get();
  }

  if (hir::Register* guilty_reg = instr.guiltyReg()) {
    meta.guilty_value = get_reg_idx(guilty_reg);
  }

  meta.nonce = instr.nonce();
  meta.reason = getDeoptReason(instr);
  JIT_CHECK(
      meta.reason != DeoptReason::kUnhandledNullField ||
          meta.guilty_value != -1,
      "Guilty value is required for UnhandledNullField deopts");
  if (meta.reason == DeoptReason::kGuardFailure || fs->hasTryBlock()) {
    meta.action = DeoptAction::kResumeInInterpreter;
  }
  if (auto check = dynamic_cast<const hir::CheckBaseWithName*>(&instr)) {
    meta.eh_name = check->name();
  }

  std::string descr = instr.descr();
  if (descr.empty()) {
    descr = hir::kOpcodeNames[static_cast<size_t>(instr.opcode())];
  }
  {
    ThreadedCompileSerialize guard;
    meta.descr = s_descrs.emplace(descr).first->c_str();
  }
  return meta;
}

} // namespace jit
