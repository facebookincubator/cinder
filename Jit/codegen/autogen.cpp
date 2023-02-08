// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/codegen/autogen.h"

#include "Jit/codegen/gen_asm_utils.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/deopt_patcher.h"
#include "Jit/frame.h"
#include "Jit/lir/instruction.h"
#include "Jit/util.h"

#include <asmjit/x86/x86operand.h>

#include <type_traits>
#include <vector>

using namespace asmjit;
using namespace jit::lir;
using namespace jit::codegen;

namespace jit::codegen::autogen {

#define ANY "*"

namespace {
// Add a pattern to an existing trie tree. If the trie tree is nullptr, create a
// new one.
std::unique_ptr<PatternNode> addPattern(
    std::unique_ptr<PatternNode> patterns,
    const std::string& s,
    PatternNode::func_t func) {
  JIT_DCHECK(!s.empty(), "pattern string should not be empty.");

  if (patterns == nullptr) {
    patterns = std::make_unique<PatternNode>();
  }

  PatternNode* cur = patterns.get();
  for (auto& c : s) {
    auto iter = cur->next.find(c);
    if (iter == cur->next.end()) {
      cur = cur->next.emplace(c, std::make_unique<PatternNode>())
                .first->second.get();
      continue;
    }
    cur = iter->second.get();
  }

  JIT_DCHECK(cur->func == nullptr, "Found duplicated pattern.");
  cur->func = func;

  return patterns;
}

// Find the function associated to the pattern given in s.
PatternNode::func_t findByPattern(
    const PatternNode* patterns,
    const std::string& s) {
  auto cur = patterns;
  if (s.empty()) {
    // handle the special case of matching '*' with an empty string
    auto iter = cur->next.find('*');
    if (iter != cur->next.end()) {
      cur = iter->second.get();
      return cur->func;
    }
  }
  for (auto& c : s) {
    auto iter = cur->next.find(c);
    if (iter != cur->next.end()) {
      cur = iter->second.get();
      continue;
    }

    iter = cur->next.find('?');
    if (iter != cur->next.end()) {
      cur = iter->second.get();
      continue;
    }

    iter = cur->next.find('*');
    if (iter != cur->next.end()) {
      cur = iter->second.get();
      break;
    }

    return nullptr;
  }

  return cur->func;
}

} // namespace

// this function generates operand patterns from the inputs and outputs
// of a given instruction instr and calls the correspoinding code generation
// functions.
void AutoTranslator::translateInstr(Environ* env, const Instruction* instr)
    const {
  auto opcode = instr->opcode();
  if (opcode == Instruction::kBind) {
    return;
  }
  auto& instr_map = map_get(instr_rule_map_, opcode);

  std::string pattern;
  pattern.reserve(instr->getNumInputs() + instr->getNumOutputs());

  if (instr->getNumOutputs()) {
    auto operand = instr->output();

    switch (operand->type()) {
      case OperandBase::kReg:
        pattern += (operand->isXmm() ? "X" : "R");
        break;
      case OperandBase::kStack:
      case OperandBase::kMem:
      case OperandBase::kInd:
        pattern += "M";
        break;
      default:
        JIT_CHECK(false, "Output operand has to be of type register or memory");
        break;
    }
  }

  instr->foreachInputOperand([&](const OperandBase* operand) {
    switch (operand->type()) {
      case OperandBase::kReg:
        pattern += (operand->isXmm() ? "x" : "r");
        break;
      case OperandBase::kStack:
      case OperandBase::kMem:
      case OperandBase::kInd:
        pattern += "m";
        break;
      case OperandBase::kImm:
        pattern += "i";
        break;
      case OperandBase::kLabel:
        pattern += "b";
        break;
      default:
        JIT_CHECK(false, "Illegal input type.");
        break;
    }
  });

  auto func = findByPattern(instr_map.get(), pattern);
  JIT_CHECK(
      func != nullptr,
      "No pattern found for opcode %s: %s",
      InstrProperty::getProperties(instr).name,
      pattern);
  func(env, instr);
}

namespace {

void fillLiveValueLocations(
    Runtime* runtime,
    std::size_t deopt_idx,
    const Instruction* instr,
    size_t begin_input,
    size_t end_input) {
  ThreadedCompileSerialize guard;

  DeoptMetadata& deopt_meta = runtime->getDeoptMetadata(deopt_idx);
  for (size_t i = begin_input; i < end_input; i++) {
    auto loc = instr->getInput(i)->getPhyRegOrStackSlot();
    deopt_meta.live_values[i - begin_input].location = loc;
  }
}

// Translate GUARD instruction
void TranslateGuard(Environ* env, const Instruction* instr) {
  auto as = env->as;

  // the first four operands of the guard instruction are:
  //   * kind
  //   * deopt meta id
  //   * guard var (physical register) (0 for AlwaysFail)
  //   * target (for GuardIs, and 0 for all others)

  auto deopt_label = as->newLabel();
  auto kind = instr->getInput(0)->getConstant();
  x86::Gp reg = x86::rax;
  bool is_double = false;
  if (kind != kAlwaysFail) {
    if (instr->getInput(2)->dataType() == jit::lir::OperandBase::kDouble) {
      assert(kind == kNotZero);
      auto xmm_reg = AutoTranslator::getXmm(instr->getInput(2));
      as->ptest(xmm_reg, xmm_reg);
      as->jz(deopt_label);
      is_double = true;
    } else {
      reg = AutoTranslator::getGp(instr->getInput(2));
    }
  }

  auto emit_cmp = [&](auto reg_arg) {
    constexpr size_t kTargetIndex = 3;
    auto target_opnd = instr->getInput(kTargetIndex);
    if (target_opnd->isImm() || target_opnd->isMem()) {
      auto target = target_opnd->getConstantOrAddress();
      JIT_DCHECK(
          fitsInt32(target),
          "Constant operand should fit in a 32-bit register, got %x.",
          target);
      as->cmp(reg_arg, target);
    } else {
      auto target_reg = AutoTranslator::getGp(target_opnd);
      as->cmp(reg_arg, target_reg);
    }
  };

  if (!is_double) {
    switch (kind) {
      case kNotZero: {
        as->test(reg, reg);
        as->jz(deopt_label);
        break;
      }
      case kNotNegative: {
        as->test(reg, reg);
        as->js(deopt_label);
        break;
      }
      case kZero: {
        as->test(reg, reg);
        as->jnz(deopt_label);
        break;
      }
      case kAlwaysFail:
        as->jmp(deopt_label);
        break;
      case kIs:
        emit_cmp(reg);
        as->jne(deopt_label);
        break;
      case kHasType: {
        emit_cmp(x86::qword_ptr(reg, offsetof(PyObject, ob_type)));
        as->jne(deopt_label);
        break;
      }
    }
  }

  auto index = instr->getInput(1)->getConstant();
  // skip the first four inputs in Guard, which are
  // kind, deopt_meta id, guard var, and target.
  fillLiveValueLocations(env->rt, index, instr, 4, instr->getNumInputs());
  env->deopt_exits.emplace_back(index, deopt_label, instr);
}

void TranslateDeoptPatchpoint(Environ* env, const Instruction* instr) {
  auto as = env->as;

  // Generate patchpoint
  auto patchpoint_label = as->newLabel();
  as->bind(patchpoint_label);
  DeoptPatcher::emitPatchpoint(*as);

  // Fill in deopt metadata
  auto index = instr->getInput(1)->getConstant();
  // skip the first two inputs which are the patcher and deopt metadata id
  fillLiveValueLocations(env->rt, index, instr, 2, instr->getNumInputs());
  auto deopt_label = as->newLabel();
  env->deopt_exits.emplace_back(index, deopt_label, instr);

  // The runtime will link the patcher to the appropriate point in the code
  // once code generation has completed.
  auto patcher =
      reinterpret_cast<DeoptPatcher*>(instr->getInput(0)->getConstant());
  env->pending_deopt_patchers.emplace_back(
      patcher, patchpoint_label, deopt_label);
}

void TranslateCompare(Environ* env, const Instruction* instr) {
  auto as = env->as;
  const OperandBase* inp0 = instr->getInput(0);
  const OperandBase* inp1 = instr->getInput(1);
  if (inp1->isImm() || inp1->isMem()) {
    as->cmp(AutoTranslator::getGp(inp0), inp1->getConstantOrAddress());
  } else if (!inp1->isXmm()) {
    as->cmp(AutoTranslator::getGp(inp0), AutoTranslator::getGp(inp1));
  } else {
    as->comisd(AutoTranslator::getXmm(inp0), AutoTranslator::getXmm(inp1));
  }
  auto output = AutoTranslator::getGp(instr->output());
  switch (instr->opcode()) {
    case Instruction::kEqual:
      as->sete(output);
      break;
    case Instruction::kNotEqual:
      as->setne(output);
      break;
    case Instruction::kGreaterThanSigned:
      as->setg(output);
      break;
    case Instruction::kGreaterThanEqualSigned:
      as->setge(output);
      break;
    case Instruction::kLessThanSigned:
      as->setl(output);
      break;
    case Instruction::kLessThanEqualSigned:
      as->setle(output);
      break;
    case Instruction::kGreaterThanUnsigned:
      as->seta(output);
      break;
    case Instruction::kGreaterThanEqualUnsigned:
      as->setae(output);
      break;
    case Instruction::kLessThanUnsigned:
      as->setb(output);
      break;
    case Instruction::kLessThanEqualUnsigned:
      as->setbe(output);
      break;
    default:
      JIT_CHECK(false, "bad instruction for TranslateCompare");
      break;
  }
  if (instr->output()->dataType() != OperandBase::k8bit) {
    as->movzx(
        AutoTranslator::getGp(instr->output()),
        asmjit::x86::gpb(instr->output()->getPhyRegister()));
  }
}

// Store meta-data about this yield in a generator suspend data pointed to by
// suspend_data_r. Data includes things like the address to resume execution at,
// and owned entries in the suspended spill data needed for GC operations etc.
void emitStoreGenYieldPoint(
    x86::Builder* as,
    Environ* env,
    const Instruction* yield,
    asmjit::Label resume_label,
    x86::Gp suspend_data_r,
    x86::Gp scratch_r) {
  bool is_yield_from = yield->isYieldFrom() ||
      yield->isYieldFromSkipInitialSend() ||
      yield->isYieldFromHandleStopAsyncIteration();

  auto calc_spill_offset = [&](size_t live_input_n) {
    int mem_loc = yield->getInput(live_input_n)->getPhyRegOrStackSlot();
    JIT_CHECK(mem_loc < 0, "Expected variable to have memory location");
    return mem_loc / kPointerSize;
  };

  size_t input_n = yield->getNumInputs() - 1;
  size_t deopt_idx = yield->getInput(input_n)->getConstant();

  size_t live_regs_input = input_n - 1;
  int num_live_regs = yield->getInput(live_regs_input)->getConstant();
  fillLiveValueLocations(
      env->rt,
      deopt_idx,
      yield,
      live_regs_input - num_live_regs,
      live_regs_input);

  auto yield_from_offset = is_yield_from ? calc_spill_offset(2) : 0;
  GenYieldPoint* gen_yield_point = env->code_rt->addGenYieldPoint(
      GenYieldPoint{deopt_idx, is_yield_from, yield_from_offset});

  env->unresolved_gen_entry_labels.emplace(gen_yield_point, resume_label);
  if (yield->origin()) {
    env->pending_debug_locs.emplace_back(resume_label, yield->origin());
  }

  as->mov(scratch_r, reinterpret_cast<uint64_t>(gen_yield_point));
  auto yieldPointOffset = offsetof(GenDataFooter, yieldPoint);
  as->mov(x86::qword_ptr(suspend_data_r, yieldPointOffset), scratch_r);
}

void emitLoadResumedYieldInputs(
    asmjit::x86::Builder* as,
    const Instruction* instr,
    PhyLocation sent_in_source_loc,
    x86::Gp tstate_reg) {
  int tstate_loc = instr->getInput(0)->getPhyRegOrStackSlot();
  JIT_CHECK(tstate_loc < 0, "__asm_tstate should be spilled");
  as->mov(x86::ptr(x86::rbp, tstate_loc), tstate_reg);

  const lir::Operand* target = instr->output();
  if (target->type() != OperandBase::kNone) {
    PhyLocation target_loc = target->getPhyRegOrStackSlot();
    if (target_loc.is_register()) {
      if (target_loc != sent_in_source_loc) {
        as->mov(x86::gpq(target_loc), x86::gpq(sent_in_source_loc));
      }
    } else {
      as->mov(x86::ptr(x86::rbp, target_loc), x86::gpq(sent_in_source_loc));
    }
  }
}

void translateYieldInitial(Environ* env, const Instruction* instr) {
  asmjit::x86::Builder* as = env->as;

  // Load tstate into RSI for call to JITRT_MakeGenObject*.
  // TODO(jbower) Avoid reloading tstate in from memory if it was already in a
  // register before spilling. Still needs to be in memory though so it can be
  // recovered after calling JITRT_MakeGenObject* which will trash it.
  int tstate_loc = instr->getInput(0)->getPhyRegOrStackSlot();
  JIT_CHECK(tstate_loc < 0, "__asm_tstate should be spilled");
  as->mov(x86::rsi, x86::ptr(x86::rbp, tstate_loc));

  // Make a generator object to be returned by the epilogue.
  as->lea(x86::rdi, x86::ptr(env->gen_resume_entry_label));
  JIT_CHECK(env->spill_size % kPointerSize == 0, "Bad spill alignment");
  as->mov(x86::rdx, (env->spill_size / kPointerSize) + 1);
  as->mov(x86::rcx, reinterpret_cast<uint64_t>(env->code_rt));
  JIT_CHECK(instr->origin()->IsInitialYield(), "expected InitialYield");
  PyCodeObject* code = static_cast<const hir::InitialYield*>(instr->origin())
                           ->frameState()
                           ->code;
  as->mov(x86::r8, reinterpret_cast<uint64_t>(code));
  if (code->co_flags & CO_COROUTINE) {
    emitCall(*env, reinterpret_cast<uint64_t>(JITRT_MakeGenObjectCoro), instr);
  } else if (code->co_flags & CO_ASYNC_GENERATOR) {
    emitCall(
        *env, reinterpret_cast<uint64_t>(JITRT_MakeGenObjectAsyncGen), instr);
  } else {
    emitCall(*env, reinterpret_cast<uint64_t>(JITRT_MakeGenObject), instr);
  }
  // Resulting generator is now in RAX for filling in below and epilogue return.
  const auto gen_reg = x86::rax;

  // Exit early if return from JITRT_MakeGenObject was NULL.
  as->test(gen_reg, gen_reg);
  as->jz(env->hard_exit_label);

  // Set RDI to gen->gi_jit_data for use in emitStoreGenYieldPoint() and data
  // copy using 'movsq' below.
  auto gi_jit_data_offset = offsetof(PyGenObject, gi_jit_data);
  as->mov(x86::rdi, x86::ptr(gen_reg, gi_jit_data_offset));

  // Arbitrary scratch register for use in emitStoreGenYieldPoint().
  auto scratch_r = x86::r9;
  asmjit::Label resume_label = as->newLabel();
  emitStoreGenYieldPoint(as, env, instr, resume_label, x86::rdi, scratch_r);

  // Store variables spilled by this point to generator.
  int frame_size = sizeof(FrameHeader);
  as->lea(x86::rsi, x86::ptr(x86::rbp, -frame_size));
  as->sub(x86::rdi, frame_size);
  int current_spill_bytes = env->initial_yield_spill_size_ - frame_size;
  JIT_CHECK(current_spill_bytes % kPointerSize == 0, "Bad spill alignment");
  as->mov(x86::rcx, (current_spill_bytes / kPointerSize) + 1);
  as->std();
  as->rep().movsq();
  as->cld();

  // Jump to bottom half of epilogue
  as->jmp(env->hard_exit_label);

  // Resumed execution in this generator begins here
  as->bind(resume_label);

  // Sent in value is in RSI, and tstate is in RCX from resume entry-point args
  emitLoadResumedYieldInputs(as, instr, PhyLocation::RSI, x86::rcx);
}

void translateYieldValue(Environ* env, const Instruction* instr) {
  asmjit::x86::Builder* as = env->as;

  // Make sure tstate is in RDI for use in epilogue.
  int tstate_loc = instr->getInput(0)->getPhyRegOrStackSlot();
  JIT_CHECK(tstate_loc < 0, "__asm_tstate should be spilled");
  as->mov(x86::rdi, x86::ptr(x86::rbp, tstate_loc));

  // Value to send goes to RAX so it can be yielded (returned) by epilogue.
  int value_out_loc = instr->getInput(1)->getPhyRegOrStackSlot();
  JIT_CHECK(value_out_loc < 0, "value to send out should be spilled");
  as->mov(x86::rax, x86::ptr(x86::rbp, value_out_loc));

  // Arbitrary scratch register for use in emitStoreGenYieldPoint()
  auto scratch_r = x86::r9;
  auto resume_label = as->newLabel();
  emitStoreGenYieldPoint(as, env, instr, resume_label, x86::rbp, scratch_r);

  // Jump to epilogue
  as->jmp(env->exit_for_yield_label);

  // Resumed execution in this generator begins here
  as->bind(resume_label);

  // Sent in value is in RSI, and tstate is in RCX from resume entry-point args
  emitLoadResumedYieldInputs(as, instr, PhyLocation::RSI, x86::rcx);
}

void translateYieldFrom(Environ* env, const Instruction* instr) {
  asmjit::x86::Builder* as = env->as;
  bool skip_initial_send = instr->isYieldFromSkipInitialSend();

  // Make sure tstate is in RDI for use in epilogue and here.
  int tstate_loc = instr->getInput(0)->getPhyRegOrStackSlot();
  JIT_CHECK(tstate_loc < 0, "__asm_tstate should be spilled");
  auto tstate_phys_reg = x86::rdi;
  as->mov(tstate_phys_reg, x86::ptr(x86::rbp, tstate_loc));

  // If we're skipping the initial send the send value is actually the first
  // value to yield and so needs to go into RAX to be returned. Otherwise,
  // put initial send value in RSI, the same location future send values will
  // be on resume.
  int send_value_loc = instr->getInput(1)->getPhyRegOrStackSlot();
  JIT_CHECK(send_value_loc < 0, "value to send out should be spilled");
  const auto send_value_phys_reg =
      skip_initial_send ? PhyLocation::RAX : PhyLocation::RSI;
  as->mov(x86::gpq(send_value_phys_reg), x86::ptr(x86::rbp, send_value_loc));

  asmjit::Label yield_label = as->newLabel();
  if (skip_initial_send) {
    as->jmp(yield_label);
  } else {
    // Setup call to JITRT_YieldFrom

    // Put tstate and the current generator into RCX and RDI respectively, and
    // set finish_yield_from (RDX) to 0. This register setup matches that when
    // `resume_label` is reached from the resume entry.
    auto gen_offs = offsetof(GenDataFooter, gen);
    as->mov(x86::rcx, tstate_phys_reg);
    as->mov(x86::rdi, x86::ptr(x86::rbp, gen_offs));
    as->xor_(x86::rdx, x86::rdx);
  }

  // Resumed execution begins here
  auto resume_label = as->newLabel();
  as->bind(resume_label);

  // Save tstate from resume to callee-saved reigster.
  as->mov(x86::rbx, x86::rcx);

  // 'send_value', and 'finish_yield_from' will already be in RSI and RCX
  // respectively, either from code above on initial start or from resume entry
  // point args.

  // Load sub-iterator into RDI
  int iter_loc = instr->getInput(2)->getPhyRegOrStackSlot();
  JIT_CHECK(
      iter_loc < 0,
      "Iter should be spilled. Instead it's in %s",
      PhyLocation(iter_loc).toString().c_str());
  as->mov(x86::rdi, x86::ptr(x86::rbp, iter_loc));

  uint64_t func = reinterpret_cast<uint64_t>(
      instr->isYieldFromHandleStopAsyncIteration()
          ? JITRT_YieldFromHandleStopAsyncIteration
          : JITRT_YieldFrom);
  emitCall(*env, func, instr);
  // Yielded or final result value now in RAX. If the result was NULL then
  // done will be set so we'll correctly jump to the following CheckExc.
  const auto yf_result_phys_reg = PhyLocation::RAX;
  const auto done_r = x86::rdx;

  // Restore tstate from callee-saved register.
  as->mov(tstate_phys_reg, x86::rbx);

  // If not done, jump to epilogue which will yield/return the value from
  // JITRT_YieldFrom in RAX.
  as->test(done_r, done_r);
  asmjit::Label done_label = as->newLabel();
  as->jnz(done_label);

  as->bind(yield_label);
  // Arbitrary scratch register for use in emitStoreGenYieldPoint()
  auto scratch_r = x86::r9;
  emitStoreGenYieldPoint(as, env, instr, resume_label, x86::rbp, scratch_r);
  as->jmp(env->exit_for_yield_label);

  as->bind(done_label);
  emitLoadResumedYieldInputs(as, instr, yf_result_phys_reg, tstate_phys_reg);
}

// ***********************************************************************
// The following templates and macros implement the auto generation table.
// The generator table defines a hash table, whose key is instruction type,
// and value is another hash table mapping instruction operand pattern and
// a function carrying out certain Actions for the instruction with the
// operand pattern.
// The list of Actions are encoded in the template class RuleActions as its
// template arguments. Currently, there are two types of Actions:
//   * AsmAction - generate an asm instruction
//   * CallAction - call a user defined instruction
// The Action classes are also templates, whose argument lists encode the
// parameters for the Action. For example, an AsmAction's argument list has
// the assembly instruction mnemonic and its operands.
// ***********************************************************************
template <int N>
const OperandBase* LIROperandMapper(const Instruction* instr) {
  auto num_outputs = instr->getNumOutputs();
  if (N < num_outputs) {
    return instr->output();
  } else {
    return instr->getInput(N - num_outputs);
  }
}

template <int N>
int LIROperandSizeMapper(const Instruction* instr) {
  auto size_type = InstrProperty::getProperties(instr).opnd_size_type;
  switch (size_type) {
    case kDefault:
      return LIROperandMapper<N>(instr)->sizeInBits();
    case kAlways64:
      return 64;
    case kOut:
      return LIROperandMapper<0>(instr)->sizeInBits();
  }

  JIT_CHECK(false, "Unknown size type");
}

template <int N>
struct ImmOperand {
  using asmjit_type = const asmjit::Imm&;

  static asmjit::Imm GetAsmOperand(Environ*, const Instruction* instr) {
    return asmjit::Imm(LIROperandMapper<N>(instr)->getConstant());
  }
};

template <typename T>
struct ImmOperandNegate {
  using asmjit_type = const asmjit::Imm&;

  static asmjit::Imm GetAsmOperand(Environ* env, const Instruction* instr) {
    return asmjit::Imm(
        -T::GetAsmOperand(env, instr).template valueAs<int64_t>());
  }
};

template <typename T>
struct ImmOperandInvert {
  using asmjit_type = const asmjit::Imm&;

  static asmjit::Imm GetAsmOperand(Environ* env, const Instruction* instr) {
    return asmjit::Imm(
        ~T::GetAsmOperand(env, instr).template valueAs<uint64_t>());
  }
};

template <int N, int Size = -1>
struct RegOperand {
  using asmjit_type = const asmjit::x86::Gp&;
  static asmjit::x86::Gp GetAsmOperand(Environ*, const Instruction* instr) {
    static_assert(
        Size == -1 || Size == 8 || Size == 16 || Size == 32 || Size == 64,
        "Invalid Size");

    int size = Size == -1 ? LIROperandSizeMapper<N>(instr) : Size;

    switch (size) {
      case 8:
        return asmjit::x86::gpb(LIROperandMapper<N>(instr)->getPhyRegister());
      case 16:
        return asmjit::x86::gpw(LIROperandMapper<N>(instr)->getPhyRegister());
      case 32:
        return asmjit::x86::gpd(LIROperandMapper<N>(instr)->getPhyRegister());
      case 64:
        return asmjit::x86::gpq(LIROperandMapper<N>(instr)->getPhyRegister());
    }
    JIT_CHECK(false, "Incorrect operand size.");
  }
};

template <int N>
struct XmmOperand {
  using asmjit_type = const asmjit::x86::Xmm&;
  static asmjit::x86::Xmm GetAsmOperand(Environ*, const Instruction* instr) {
    return asmjit::x86::xmm(
        LIROperandMapper<N>(instr)->getPhyRegister() -
        PhyLocation::XMM_REG_BASE);
  }
};

#define OP(v)                                       \
  typename std::conditional_t<                      \
      pattern[v] == 'i',                            \
      ImmOperand<v>,                                \
      std::conditional_t<                           \
          (pattern[v] == 'x' || pattern[v] == 'X'), \
          XmmOperand<v>,                            \
          RegOperand<v>>>

#define REG_OP(v, size) RegOperand<v, size>

asmjit::x86::Mem AsmIndirectOperandBuilder(const OperandBase* operand) {
  JIT_DCHECK(operand->isInd(), "operand should be an indirect reference");

  auto indirect = operand->getMemoryIndirect();

  OperandBase* base = indirect->getBaseRegOperand();
  OperandBase* index = indirect->getIndexRegOperand();

  if (index == nullptr) {
    return asmjit::x86::ptr(
        x86::gpq(base->getPhyRegister()), indirect->getOffset());
  } else {
    return asmjit::x86::ptr(
        x86::gpq(base->getPhyRegister()),
        x86::gpq(index->getPhyRegister()),
        indirect->getMultipiler(),
        indirect->getOffset());
  }
}

template <int N>
struct MemOperand {
  using asmjit_type = const asmjit::x86::Mem&;
  static asmjit::x86::Mem GetAsmOperand(Environ*, const Instruction* instr) {
    const OperandBase* operand = LIROperandMapper<N>(instr);
    auto size = LIROperandSizeMapper<N>(instr) / 8;
    asmjit::x86::Mem memptr;
    if (operand->isStack()) {
      memptr = asmjit::x86::ptr(asmjit::x86::rbp, operand->getStackSlot());
    } else if (operand->isMem()) {
      memptr = asmjit::x86::ptr(
          reinterpret_cast<uint64_t>(operand->getMemoryAddress()));
    } else if (operand->isInd()) {
      memptr = AsmIndirectOperandBuilder(operand);
    } else {
      JIT_CHECK(false, "Unsupported operand type.");
    }

    memptr.setSize(size);
    return memptr;
  }
};

#define MEM(m) MemOperand<m>
#define STK(v) MemOperand<v>

template <int N>
struct LabelOperand {
  using asmjit_type = const asmjit::Label&;
  static asmjit::Label GetAsmOperand(Environ* env, const Instruction* instr) {
    auto block = LIROperandMapper<N>(instr)->getBasicBlock();
    return map_get(env->block_label_map, block);
  }
};

#define LBL(v) LabelOperand<v>

template <typename... Args>
struct OperandList;

template <typename FuncType, FuncType func, typename OpndList>
struct AsmAction;

template <typename FuncType, FuncType func, typename... OpndTypes>
struct AsmAction<FuncType, func, OperandList<OpndTypes...>> {
  static void eval(Environ* env, const Instruction* instr) {
    static_cast<void>(instr);
    (env->as->*func)(OpndTypes::GetAsmOperand(env, instr)...);
  }
};

template <typename... Args>
struct AsminstructionType {
  using type =
      asmjit::Error (asmjit::x86::EmitterExplicitT<asmjit::x86::Builder>::*)(
          typename Args::asmjit_type...);
};

template <void (*func)(Environ*, const Instruction*)>
struct CallAction {
  static void eval(Environ* env, const Instruction* instr) {
    func(env, instr);
  }
};

template <typename... Actions>
struct RuleActions;

template <typename AAction, typename... Actions>
struct RuleActions<AAction, Actions...> {
  static void eval(Environ* env, const Instruction* instr) {
    AAction::eval(env, instr);
    RuleActions<Actions...>::eval(env, instr);
  }
};

template <>
struct RuleActions<> {
  static void eval(Environ*, const Instruction*) {}
};

struct AddDebugEntryAction {
  static void eval(Environ* env, const Instruction* instr) {
    asmjit::Label label = env->as->newLabel();
    env->as->bind(label);
    if (instr->origin()) {
      env->pending_debug_locs.emplace_back(label, instr->origin());
    }
  }
};

} // namespace

#define ASM(instr, args...)                    \
  AsmAction<                                   \
      typename AsminstructionType<args>::type, \
      &asmjit::x86::Builder::instr,            \
      OperandList<args>>

#define CALL(func) CallAction<func>

#define ADDDEBUGENTRY() AddDebugEntryAction

#define BEGIN_RULE_TABLE void AutoTranslator::initTable() {
#define END_RULE_TABLE }

#define BEGIN_RULES(__t)                                \
  {                                                     \
    auto& __rules = instr_rule_map_                     \
                        .emplace(                       \
                            std::piecewise_construct,   \
                            std::forward_as_tuple(__t), \
                            std::forward_as_tuple())    \
                        .first->second;

#define END_RULES }
#define GEN(s, actions...)                                  \
  {                                                         \
    UNUSED constexpr char pattern[] = s;                    \
    using rule_actions = RuleActions<actions>;              \
    auto gen = [](Environ* env, const Instruction* instr) { \
      rule_actions::eval(env, instr);                       \
    };                                                      \
    __rules = addPattern(std::move(__rules), s, gen);       \
  }

// ***********************************************************************
// Definition of Auto Generation Table
// The table consisting of multiple rules, and the rules for the same LIR
// instruction are grouped by BEGIN_RULES(LIR instruction type) and
// END_RULES.
// GEN defines a rule for a certain operand pattern of the LIR instruction,
// and maps it to a list of actions:
//   GEN(<operand pattern>, action1, action2, ...)
//
// TODO(tiansi): define macros for the operand pattern to make it more readable.
// The operand pattern is defined by a string, and each character in the string
// correpsonds to an operand of the instruction. The character can be one
// of the following:
//   * 'R' - general purpose register operand output
//   * 'r' - general purpose register operand input
//   * 'X' - XMM floating-point register operand output
//   * 'x' - XMM floating-point register operand input
//   * 'i' - immediate operand input
//   * 'M' - memory stack operand output
//   * 'm' - memory stack operand input
// Wildcards "?" and "*" can also be used in patterns, where "?" represents any
// one of the types listed above and "*" represents one or more above types.
// Please note that while "?" can appear anywhere in a pattern, "*" can only be
// used at the end of a pattern.
// The actions can be ASM and CALL, meaning generating an assembly instruction
// and call a user-defined function, respectively. The first argument of ASM
// action is the mnemonic of the instruction to be generated, and the following
// arguments are the operands to the instruction. Currently, we have four types
// of assembly instruction operands:
//   * OP  - either an immediate operand or register oeprand
//   * STK - a memory stack location [RBP - ?]
//   * LBL - a label to a basic block
//   * MEM - a memory operand. The size of the memory operand will be set to the
//           size of the LIR instruction operand specified by the first argument
//           of MEM.
// The assembly instruction operands are constructed from one or more LIR
// instruction operands. To specify the LIR operands, we use indices
// of the pattern string. For example:
//   GEN("Rri", ASM(mov, OP(0), MEM(0, 1, 2)))
// means generating a mov instruction, whose first operand is a
// register/immediate operand, constructed from the only output of the LIR
// instruction, and the second operand is memory operand, constructed from the
// register input and the immediate input of the LIR instruction. The size of
// the memory operand is set to the size of the output of the LIR instruction.
// ***********************************************************************

// clang-format off
BEGIN_RULE_TABLE

BEGIN_RULES(Instruction::kLea)
  GEN("Rm", ASM(lea, OP(0), MEM(1)))
END_RULES

BEGIN_RULES(Instruction::kCall)
  GEN("Ri", ASM(call, OP(1)), ADDDEBUGENTRY())
  GEN("Rr", ASM(call, OP(1)), ADDDEBUGENTRY())
  GEN("i", ASM(call, OP(0)), ADDDEBUGENTRY())
  GEN("r", ASM(call, OP(0)), ADDDEBUGENTRY())
  GEN("m", ASM(call, STK(0)), ADDDEBUGENTRY())
END_RULES

BEGIN_RULES(Instruction::kMove)
  GEN("Rr", ASM(mov, OP(0), OP(1)))
  GEN("Ri", ASM(mov, OP(0), OP(1)))
  GEN("Rm", ASM(mov, OP(0), MEM(1)))
  GEN("Mr", ASM(mov, MEM(0), OP(1)))
  GEN("Mi", ASM(mov, MEM(0), OP(1)))
  GEN("Xx", ASM(movsd, OP(0), OP(1)))
  GEN("Xm", ASM(movsd, OP(0), MEM(1)))
  GEN("Mx", ASM(movsd, MEM(0), OP(1)))
  GEN("Xr", ASM(movq, OP(0), OP(1)))
  GEN("Rx", ASM(movq, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kGuard)
  GEN(ANY, CALL(TranslateGuard));
END_RULES

BEGIN_RULES(Instruction::kDeoptPatchpoint)
  GEN(ANY, CALL(TranslateDeoptPatchpoint));
END_RULES

BEGIN_RULES(Instruction::kNegate)
  GEN("r", ASM(neg, OP(0)))
  GEN("Ri", ASM(mov, OP(0), ImmOperandNegate<OP(1)>))
  GEN("Rr", ASM(mov, OP(0), OP(1)), ASM(neg, OP(0)))
  GEN("Rm", ASM(mov, OP(0), STK(1)), ASM(neg, OP(0)))
END_RULES

BEGIN_RULES(Instruction::kInvert)
  GEN("Ri", ASM(mov, OP(0), ImmOperandInvert<OP(1)>))
  GEN("Rr", ASM(mov, OP(0), OP(1)), ASM(not_, OP(0)))
  GEN("Rm", ASM(mov, OP(0), STK(1)), ASM(not_, OP(0)))
END_RULES

BEGIN_RULES(Instruction::kMovZX)
  GEN("Rr", ASM(movzx, OP(0), OP(1)))
  GEN("Rm", ASM(movzx, OP(0), STK(1)))
END_RULES

BEGIN_RULES(Instruction::kMovSX)
  GEN("Rr", ASM(movsx, OP(0), OP(1)))
  GEN("Rm", ASM(movsx, OP(0), STK(1)))
END_RULES

BEGIN_RULES(Instruction::kMovSXD)
  GEN("Rr", ASM(movsxd, OP(0), OP(1)))
  GEN("Rm", ASM(movsxd, OP(0), STK(1)))
END_RULES

BEGIN_RULES(Instruction::kUnreachable)
  GEN(ANY, ASM(ud2))
END_RULES

#define DEF_BINARY_OP_RULES(name, instr) \
  BEGIN_RULES(Instruction::name) \
    GEN("ri", ASM(instr, OP(0), OP(1))) \
    GEN("rr", ASM(instr, OP(0), OP(1))) \
    GEN("rm", ASM(instr, OP(0), STK(1))) \
    /* rewriteBinaryOpInstrs() makes it safe to write the output before reading
     * all inputs without inputs_live_across being set for most binary ops; see
     * postalloc.cpp for details. */ \
    GEN("Rri", ASM(mov, OP(0), OP(1)), ASM(instr, OP(0), OP(2))) \
    GEN("Rrr", ASM(mov, OP(0), OP(1)), ASM(instr, OP(0), OP(2))) \
    GEN("Rrm", ASM(mov, OP(0), OP(1)), ASM(instr, OP(0), STK(2))) \
  END_RULES

DEF_BINARY_OP_RULES(kAdd, add)
DEF_BINARY_OP_RULES(kSub, sub)
DEF_BINARY_OP_RULES(kAnd, and_)
DEF_BINARY_OP_RULES(kOr, or_)
DEF_BINARY_OP_RULES(kXor, xor_)
DEF_BINARY_OP_RULES(kMul, imul)

BEGIN_RULES(Instruction::kDiv)
  GEN("rrr", ASM(idiv, OP(0), OP(1), OP(2)) )
  GEN("rrm", ASM(idiv, OP(0), OP(1), STK(2)) )
  GEN("rr", ASM(idiv, OP(0), OP(1)) )
  GEN("rm", ASM(idiv, OP(0), STK(1)) )
END_RULES

BEGIN_RULES(Instruction::kDivUn)
  GEN("rrr", ASM(div, OP(0), OP(1), OP(2)) )
  GEN("rrm", ASM(div, OP(0), OP(1), STK(2)) )
  GEN("rr", ASM(div, OP(0), OP(1)) )
  GEN("rm", ASM(div, OP(0), STK(1)) )
END_RULES

#undef DEF_BINARY_OP_RULES

BEGIN_RULES(Instruction::kFadd)
  /* rewriteBinaryOpInstrs() makes it safe to write the output before reading
   * all inputs without inputs_live_across being set for Fadd; see
   * postalloc.cpp for details. */
  GEN("Xxx", ASM(movsd, OP(0), OP(1)), ASM(addsd, OP(0), OP(2)))
  GEN("xx", ASM(addsd, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kFsub)
  GEN("Xxx", ASM(movsd, OP(0), OP(1)), ASM(subsd, OP(0), OP(2)))
  GEN("xx", ASM(subsd, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kFmul)
  /* rewriteBinaryOpInstrs() makes it safe to write the output before reading
   * all inputs without inputs_live_across being set for Fmul; see
   * postalloc.cpp for details. */
  GEN("Xxx", ASM(movsd, OP(0), OP(1)), ASM(mulsd, OP(0), OP(2)))
  GEN("xx", ASM(mulsd, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kFdiv)
  GEN("Xxx", ASM(movsd, OP(0), OP(1)), ASM(divsd, OP(0), OP(2)))
  GEN("xx", ASM(divsd, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kPush)
  GEN("r", ASM(push, OP(0)))
  GEN("m", ASM(push, STK(0)))
  GEN("i", ASM(push, OP(0)))
END_RULES

BEGIN_RULES(Instruction::kPop)
  GEN("R", ASM(pop, OP(0)))
  GEN("M", ASM(pop, STK(0)))
END_RULES

BEGIN_RULES(Instruction::kCdq)
  GEN("Rr", ASM(cdq, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kCwd)
  GEN("Rr", ASM(cwd, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kCqo)
  GEN("Rr", ASM(cqo, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kExchange)
  GEN("Rr", ASM(xchg, OP(0), OP(1)))
  GEN("Xx", ASM(pxor, OP(0), OP(1)),
            ASM(pxor, OP(1), OP(0)),
            ASM(pxor, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kCmp)
  GEN("rr", ASM(cmp, OP(0), OP(1)))
  GEN("ri", ASM(cmp, OP(0), OP(1)))
  GEN("xx", ASM(comisd, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kTest)
  GEN("rr", ASM(test, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kTest32)
  GEN("rr", ASM(test, REG_OP(0, 32), REG_OP(1, 32)))
END_RULES

BEGIN_RULES(Instruction::kBranch)
  GEN("b", ASM(jmp, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchZ)
  GEN("b", ASM(jz, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchNZ)
  GEN("b", ASM(jnz, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchA)
  GEN("b", ASM(ja, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchB)
  GEN("b", ASM(jb, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchAE)
  GEN("b", ASM(jae, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchBE)
  GEN("b", ASM(jbe, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchG)
  GEN("b", ASM(jg, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchL)
  GEN("b", ASM(jl, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchGE)
  GEN("b", ASM(jge, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchLE)
  GEN("b", ASM(jle, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchC)
  GEN("b", ASM(jc, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchNC)
  GEN("b", ASM(jnc, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchO)
  GEN("b", ASM(jo, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchNO)
  GEN("b", ASM(jno, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchS)
  GEN("b", ASM(js, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchNS)
  GEN("b", ASM(jns, LBL(0)))
END_RULES

BEGIN_RULES(Instruction::kBranchE)
  GEN("b", ASM(je, LBL(0)))
END_RULES


#define DEF_COMPARE_OP_RULES(name, fpcomp) \
BEGIN_RULES(Instruction::name) \
  GEN("Rrr", CALL(TranslateCompare)) \
  GEN("Rri", CALL(TranslateCompare)) \
  GEN("Rrm", CALL(TranslateCompare)) \
  if (fpcomp) { \
    GEN("Rxx", CALL(TranslateCompare)) \
  } \
END_RULES

DEF_COMPARE_OP_RULES(kEqual, true)
DEF_COMPARE_OP_RULES(kNotEqual, true)
DEF_COMPARE_OP_RULES(kGreaterThanUnsigned, true)
DEF_COMPARE_OP_RULES(kGreaterThanEqualUnsigned, true)
DEF_COMPARE_OP_RULES(kLessThanUnsigned, true)
DEF_COMPARE_OP_RULES(kLessThanEqualUnsigned, true)
DEF_COMPARE_OP_RULES(kGreaterThanSigned, false)
DEF_COMPARE_OP_RULES(kGreaterThanEqualSigned, false)
DEF_COMPARE_OP_RULES(kLessThanSigned, false)
DEF_COMPARE_OP_RULES(kLessThanEqualSigned, false)

#undef DEF_COMPARE_OP_RULES

BEGIN_RULES(Instruction::kInc)
  GEN("r", ASM(inc, OP(0)))
  GEN("m", ASM(inc, STK(0)))
END_RULES

BEGIN_RULES(Instruction::kDec)
  GEN("r", ASM(dec, OP(0)))
  GEN("m", ASM(dec, STK(0)))
END_RULES

BEGIN_RULES(Instruction::kBitTest)
  GEN("ri", ASM(bt, OP(0), OP(1)))
END_RULES

BEGIN_RULES(Instruction::kYieldInitial)
  GEN(ANY, CALL(translateYieldInitial))
END_RULES

BEGIN_RULES(Instruction::kYieldFrom)
  GEN(ANY, CALL(translateYieldFrom))
END_RULES

BEGIN_RULES(Instruction::kYieldFromSkipInitialSend)
  GEN(ANY, CALL(translateYieldFrom))
END_RULES

BEGIN_RULES(Instruction::kYieldFromHandleStopAsyncIteration)
  GEN(ANY, CALL(translateYieldFrom))
END_RULES

BEGIN_RULES(Instruction::kYieldValue)
  GEN(ANY, CALL(translateYieldValue))
END_RULES

BEGIN_RULES(Instruction::kSelect)
  GEN("Rrri", ASM(mov, OP(0), OP(3)),
              ASM(test, OP(1), OP(1)),
              ASM(cmovnz, OP(0), OP(2)))
END_RULES

END_RULE_TABLE
// clang-format on

} // namespace jit::codegen::autogen
