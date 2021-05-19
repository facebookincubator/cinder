// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/deopt.h"

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

hir::RefKind deoptRefKind(hir::Type type, hir::RefKind lifetime_kind) {
  if (type <= jit::hir::TCBool) {
    return jit::hir::RefKind::kBool;
  }

  if (type <= jit::hir::TCDouble) {
    return jit::hir::RefKind::kDouble;
  }

  // TODO(bsimmers): The type predicates here are gross and indicate a deeper
  // problem with how we're using Types earlier in the pipeline: we use
  // `LoadNull` to zero-initialize locals with primitive types (currently done
  // in SSAify). It works fine at runtime and a proper fix likely involves
  // reworking HIR's support for constant values, so we paper over the issue
  // here for the moment.
  if (type.couldBe(jit::hir::TCUnsigned | jit::hir::TCSigned)) {
    if (type <= (jit::hir::TCUnsigned | jit::hir::TNullptr)) {
      return jit::hir::RefKind::kUnsigned;
    }
    if (type <= (jit::hir::TCSigned | jit::hir::TNullptr)) {
      return jit::hir::RefKind::kSigned;
    }
  }

  JIT_CHECK(
      type <= jit::hir::TOptObject, "Unexpected type %s in deopt value", type);
  return lifetime_kind;
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

namespace {
// A simple interface for reading the contents of registers + memory
struct MemoryView {
  const uint64_t* regs;

  // reads the value from memory and returns an object with a new
  // ref count added. If the borrow flag is true the addition of the
  // new ref count is skipped.
  PyObject* read(const LiveValue& value, bool borrow = false) const {
    uint64_t raw;
    PhyLocation loc = value.location;
    if (loc.is_register()) {
      raw = regs[loc.loc];
    } else {
      uint64_t rbp = regs[PhyLocation::RBP];
      // loc.loc is negative when loc is a memory location relative to RBP
      raw = *(reinterpret_cast<uint64_t*>(rbp + loc.loc));
    }

    switch (value.ref_kind) {
      case jit::hir::RefKind::kUncounted:
      case jit::hir::RefKind::kBorrowed:
      case jit::hir::RefKind::kOwned: {
        PyObject* res = reinterpret_cast<PyObject*>(raw);
        if (!borrow) {
          Py_XINCREF(res);
        }
        return res;
      }
      case jit::hir::RefKind::kSigned:
        JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
        return PyLong_FromSsize_t((Py_ssize_t)raw);
      case jit::hir::RefKind::kUnsigned:
        JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
        return PyLong_FromSize_t(raw);
      case hir::RefKind::kDouble:
        JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
        return PyFloat_FromDouble(raw);
      case jit::hir::RefKind::kBool:
        JIT_CHECK(!borrow, "borrow can only get raw pyobjects");
        PyObject* res = raw ? Py_True : Py_False;
        Py_INCREF(res);
        return res;
    }
    return nullptr;
  }

  JITRT_CallMethodKind readCallKind() const {
    // When we are deoptimizing a JIT-compiled function that contains an
    // optimizable LoadMethod, we need to be able to know whether or not
    // the LoadMethod was optimized in order to properly reconstruct the
    // python stack. To do so, we read the call kind value that is stored
    // on the C++ stack. The layout of the stack between the call kind
    // and the saved `rsp` looks like the following
    //
    // +-------------------------+
    // | call kind               |
    // | index of deopt metadata |
    // | address of epilogue     |
    // | r15 through r8          |
    // | rdi                     |
    // | rsi                     |
    // | rsp                     | Saved `rsp` points to saved `rsi`
    // +-------------------------+
    uint64_t rsp = regs[PhyLocation::RSP];
    int delta =
        (PhyLocation::NUM_GP_REGS - PhyLocation::RSP + 1) * kPointerSize;
    return *(reinterpret_cast<JITRT_CallMethodKind*>(rsp + delta));
  }
};
} // namespace

static void reifyLocalsplus(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const MemoryView& mem) {
  for (std::size_t i = 0; i < meta.localsplus.size(); i++) {
    auto value = meta.getLocalValue(i);
    if (value == nullptr) {
      // Value is dead
      Py_CLEAR(frame->f_localsplus[i]);
      continue;
    }
    PyObject* obj = mem.read(*value);
    Py_XSETREF(frame->f_localsplus[i], obj);
  }
}

static bool didOptimizeLoadMethod(
    const LiveValue& value,
    const MemoryView& mem) {
  if (value.source != LiveValue::Source::kOptimizableLoadMethod) {
    return false;
  }
  return mem.readCallKind() != JITRT_CALL_KIND_OTHER;
}

static void reifyStack(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const MemoryView& mem) {
  frame->f_stacktop = frame->f_valuestack + meta.stack.size();
  for (int i = meta.stack.size() - 1; i >= 0; i--) {
    const auto& value = meta.getStackValue(i);
    if (value.isLoadMethodResult()) {
      PyObject* callable = mem.read(value);
      if (didOptimizeLoadMethod(value, mem)) {
        // If we avoided creating a bound method object the interpreter
        // expects the stack to look like:
        //
        // function
        // self
        // arg1
        // ...
        // argN       <-- TOS
        PyObject* receiver = mem.read(meta.getStackValue(i - 1));
        frame->f_valuestack[i - 1] = callable;
        frame->f_valuestack[i] = receiver;
      } else {
        // Otherwise, the interpreter expects the stack look like
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

static void profileDeopt(
    std::size_t deopt_idx,
    const DeoptMetadata& meta,
    const MemoryView& mem) {
  const LiveValue* live_val = meta.getGuiltyValue();
  PyObject* val = live_val == nullptr ? nullptr : mem.read(*live_val, true);
  codegen::NativeGenerator::runtime()->recordDeopt(deopt_idx, val);
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

static void releaseRefs(
    const std::vector<LiveValue>& live_values,
    const MemoryView& mem) {
  for (const auto& value : live_values) {
    switch (value.ref_kind) {
      case jit::hir::RefKind::kSigned:
      case jit::hir::RefKind::kUnsigned:
      case jit::hir::RefKind::kBool:
      case jit::hir::RefKind::kDouble:
      case jit::hir::RefKind::kUncounted:
      case jit::hir::RefKind::kBorrowed: {
        continue;
      }
      case jit::hir::RefKind::kOwned: {
        PyObject* obj = mem.read(value, true);
        // Reference may be NULL if value is not definitely assigned
        Py_XDECREF(obj);
        break;
      }
    }
  }
}

void reifyFrame(
    PyFrameObject* frame,
    std::size_t deopt_idx,
    const DeoptMetadata& meta,
    const uint64_t* regs) {
  frame->f_locals = NULL;
  frame->f_trace = NULL;
  frame->f_trace_opcodes = 0;
  frame->f_trace_lines = 1;
  frame->f_executing = 0;
  // Interpreter loop will handle filling this in
  frame->f_lineno = frame->f_code->co_firstlineno;
  // Instruction pointer
  if (meta.next_instr_offset == 0) {
    frame->f_lasti = -1;
  } else {
    frame->f_lasti = meta.next_instr_offset - sizeof(_Py_CODEUNIT);
  }
  MemoryView mem{regs};
  reifyLocalsplus(frame, meta, mem);
  reifyStack(frame, meta, mem);
  if (deopt_idx != -1ull) {
    profileDeopt(deopt_idx, meta, mem);
  }
  // Clear our references now that we've transferred them to the frame
  releaseRefs(meta.live_values, mem);
  reifyBlockStack(frame, meta.block_stack);
  if (meta.code_rt->GetCode()->co_flags & kCoFlagsAnyGenerator) {
    switch (meta.code_rt->frameMode()) {
      case hir::FrameMode::kNone: {
        auto gen =
            reinterpret_cast<jit::GenDataFooter*>(regs[PhyLocation::RBP])->gen;
        frame->f_gen = reinterpret_cast<PyObject*>(gen);
        gen->gi_frame = frame;
        Py_INCREF(frame);
        break;
      }
      case hir::FrameMode::kNormal:
      case hir::FrameMode::kShadow:
        // Generator/frame linkage happens in `materializePyFrame` in
        // frame.cpp
        break;
    }
  }
}

static DeoptReason getDeoptReason(const jit::hir::DeoptBase& instr) {
  switch (instr.opcode()) {
    case jit::hir::Opcode::kCheckVar: {
      return DeoptReason::kUnhandledUnboundLocal;
    }
    case jit::hir::Opcode::kCheckField: {
      return DeoptReason::kUnhandledNullField;
    }
    case jit::hir::Opcode::kCheckNone: {
      return DeoptReason::kUnhandledNone;
    }
    case jit::hir::Opcode::kDeopt:
    case jit::hir::Opcode::kGuard:
    case jit::hir::Opcode::kGuardIs: {
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

  // Translate locals and cells
  auto fs = instr.frameState();
  auto nlocals = fs->locals.size();
  auto ncells = fs->cells.size();
  meta.localsplus.resize(nlocals + ncells, -1);
  for (std::size_t i = 0; i < nlocals; i++) {
    meta.localsplus[i] = get_reg_idx(fs->locals[i]);
  }
  for (std::size_t i = 0; i < ncells; i++) {
    meta.localsplus[nlocals + i] = get_reg_idx(fs->cells[i]);
  }

  // Translate stack
  std::unordered_set<jit::hir::Register*> lms_on_stack;
  for (auto& reg : fs->stack) {
    if (reg->instr()->IsLoadMethod()) {
      // Our logic for reconstructing the Python stack assumes that if a value
      // on the stack was produced by a LoadMethod instruction, it corresponds
      // to the output of a LOAD_METHOD opcode and will eventually be consumed
      // by a CALL_METHOD. That doesn't technically have to be true, but it's
      // our contention that the CPython compiler will never produce bytecode
      // that would contradict this.
      auto result = lms_on_stack.emplace(reg);
      JIT_CHECK(
          result.second,
          "load method results may only appear in one stack slot");
    }
    meta.stack.emplace_back(get_reg_idx(reg));
  }

  if (hir::Register* guilty_reg = instr.guiltyReg()) {
    meta.guilty_value = get_reg_idx(guilty_reg);
  }

  meta.block_stack = fs->block_stack;
  meta.next_instr_offset = fs->next_instr_offset;
  meta.nonce = instr.nonce();
  meta.reason = getDeoptReason(instr);
  if (meta.reason == DeoptReason::kGuardFailure || fs->hasTryBlock()) {
    meta.action = DeoptAction::kResumeInInterpreter;
  }
  if (instr.IsCheckVar()) {
    const auto& check = static_cast<const jit::hir::CheckVar&>(instr);
    meta.eh_name_index = check.name_idx();
  } else if (instr.IsCheckField()) {
    const auto& check = static_cast<const jit::hir::CheckField&>(instr);
    meta.eh_name_index = check.field_idx();
  } else if (instr.IsCheckNone()) {
    const auto& check = static_cast<const jit::hir::CheckNone&>(instr);
    meta.eh_name_index = check.name_idx();
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
