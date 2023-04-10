// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/deopt.h"

#include "Jit/bytecode_offsets.h"
#include "Jit/codegen/gen_asm.h"
#include "Jit/hir/analysis.h"
#include "Jit/jit_rt.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

#include <bit>
#include <shared_mutex>

using jit::codegen::PhyLocation;

namespace jit {

hir::ValueKind deoptValueKind(hir::Type type) {
  if (type <= jit::hir::TCBool) {
    return jit::hir::ValueKind::kBool;
  }

  if (type <= jit::hir::TCDouble) {
    return jit::hir::ValueKind::kDouble;
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

BorrowedRef<> MemoryView::readBorrowed(const LiveValue& value) const {
  JIT_CHECK(
      value.value_kind == jit::hir::ValueKind::kObject,
      "cannot materialize a borrowed primitive value");
  return reinterpret_cast<PyObject*>(readRaw(value));
}

Ref<> MemoryView::readOwned(const LiveValue& value) const {
  uint64_t raw = readRaw(value);

  switch (value.value_kind) {
    case jit::hir::ValueKind::kSigned: {
      Py_ssize_t raw_signed = bit_cast<Py_ssize_t, uint64_t>(raw);
      return Ref<>::steal(PyLong_FromSsize_t(raw_signed));
    }
    case jit::hir::ValueKind::kUnsigned:
      return Ref<>::steal(PyLong_FromSize_t(raw));
    case hir::ValueKind::kDouble:
      return Ref<>::steal(PyFloat_FromDouble(raw));
    case jit::hir::ValueKind::kBool:
      return Ref<>::create(raw ? Py_True : Py_False);
    case jit::hir::ValueKind::kObject:
      return Ref<>::create(reinterpret_cast<PyObject*>(raw));
  }
  JIT_CHECK(false, "Unhandled ValueKind");
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
    PyObject* obj = mem.readOwned(*value).release();
    Py_XSETREF(frame->f_localsplus[i], obj);
  }
}

static void reifyStack(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const MemoryView& mem) {
  frame->f_stackdepth = frame_meta.stack.size();
  for (int i = frame_meta.stack.size() - 1; i >= 0; i--) {
    const auto& value = meta.getStackValue(i, frame_meta);
    Ref<> obj = mem.readOwned(value);
    if (value.isLoadMethodResult()) {
      // When we are deoptimizing a JIT-compiled function that contains an
      // optimizable LoadMethod, we need to be able to know whether or not the
      // LoadMethod returned a bound method object in order to properly
      // reconstruct the stack for the interpreter. We use Py_None as the
      // LoadMethodResult to indicate that it was a non-method like object,
      // which we need to replace with NULL to match the interpreter semantics.
      if (obj == Py_None) {
        frame->f_valuestack[i] = nullptr;
      } else {
        frame->f_valuestack[i] = obj.release();
      }
    } else {
      frame->f_valuestack[i] = obj.release();
    }
  }
}

Ref<> profileDeopt(
    std::size_t deopt_idx,
    const DeoptMetadata& meta,
    const MemoryView& mem) {
  const LiveValue* live_val = meta.getGuiltyValue();
  Ref<> guilty_obj = live_val == nullptr ? nullptr : mem.readOwned(*live_val);
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
    frame->f_blockstack[i].b_handler = block.handler_off.asIndex().value();
    frame->f_blockstack[i].b_level = block.stack_level;
  }
}

static void reifyFrameImpl(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    bool for_gen_resume,
    const DeoptFrameMetadata& frame_meta,
    const uint64_t* regs) {
  frame->f_locals = NULL;
  frame->f_trace = NULL;
  frame->f_trace_opcodes = 0;
  frame->f_trace_lines = 1;
  if (meta.reason == DeoptReason::kGuardFailure || for_gen_resume) {
    frame->f_state = FRAME_EXECUTING;
  } else {
    frame->f_state = FRAME_UNWINDING;
  }

  // Instruction pointer
  if (frame_meta.next_instr_offset == 0) {
    frame->f_lasti = -1;
  } else {
    frame->f_lasti = (BCIndex{frame_meta.next_instr_offset} - 1).value();
  }
  if (meta.reason == DeoptReason::kYieldFrom && for_gen_resume) {
    // The DeoptMetadata for YieldFrom-like instructions defaults to the state
    // for raising an exception. If we're going to resume execution, we need to
    // pull the instruction pointer back by one, to repeat the YIELD_FROM
    // bytecode.
    frame->f_lasti--;
  }
  MemoryView mem{regs};
  reifyLocalsplus(frame, meta, frame_meta, mem);
  reifyStack(frame, meta, frame_meta, mem);
  reifyBlockStack(frame, frame_meta.block_stack);
  // Generator/frame linkage happens in `materializePyFrame` in frame.cpp
}

void reifyFrame(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const uint64_t* regs) {
  reifyFrameImpl(frame, meta, false, frame_meta, regs);
}

void reifyGeneratorFrame(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const void* base) {
  uint64_t regs[codegen::PhyLocation::NUM_GP_REGS]{};
  regs[codegen::PhyLocation::RBP] = reinterpret_cast<uint64_t>(base);
  reifyFrameImpl(frame, meta, true, frame_meta, regs);
}

void releaseRefs(const DeoptMetadata& meta, const MemoryView& mem) {
  for (const auto& value : meta.live_values) {
    switch (value.ref_kind) {
      case jit::hir::RefKind::kUncounted:
      case jit::hir::RefKind::kBorrowed: {
        continue;
      }
      case jit::hir::RefKind::kOwned: {
        // Read as borrowed then steal to decref.
        Ref<>::steal(mem.readBorrowed(value));
        break;
      }
    }
  }
}

void releaseRefs(const DeoptMetadata& meta, const void* base) {
  uint64_t regs[codegen::PhyLocation::NUM_GP_REGS]{};
  regs[codegen::PhyLocation::RBP] = reinterpret_cast<uint64_t>(base);
  releaseRefs(meta, MemoryView{regs});
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
    case jit::hir::Opcode::kGuardType:
    case jit::hir::Opcode::kLoadSplitDictItem: {
      return DeoptReason::kGuardFailure;
    }
    case jit::hir::Opcode::kYieldAndYieldFrom:
    case jit::hir::Opcode::kYieldFromHandleStopAsyncIteration:
    case jit::hir::Opcode::kYieldFrom: {
      return DeoptReason::kYieldFrom;
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
    CodeRuntime* code_rt) {
  auto get_source = [&](jit::hir::Register* reg) {
    reg = hir::modelReg(reg);
    auto instr = reg->instr();
    if (isAnyLoadMethod(*instr)) {
      return LiveValue::Source::kLoadMethod;
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
      if (isAnyLoadMethod(*reg->instr())) {
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
  JIT_DCHECK(
      fs != nullptr, "need FrameState to calculate inline depth of %s", instr);

  int num_frames = fs->inlineDepth();
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
  if (auto check = dynamic_cast<const hir::CheckBaseWithName*>(&instr)) {
    meta.eh_name = check->name();
  }

  std::string descr = instr.descr();
  if (descr.empty()) {
    descr = hir::kOpcodeNames[static_cast<size_t>(instr.opcode())];
  }

  {
    // Set of interned strings for deopt descriptions.
    static std::unordered_set<std::string> s_descrs;
    static std::shared_mutex s_descrs_mutex;

    std::shared_lock guard{s_descrs_mutex};
    auto iter = s_descrs.find(descr);
    if (iter != s_descrs.end()) {
      meta.descr = iter->c_str();
    } else {
      guard.unlock();
      std::unique_lock guard{s_descrs_mutex};
      meta.descr = s_descrs.emplace(descr).first->c_str();
    }
  }
  return meta;
}

} // namespace jit
