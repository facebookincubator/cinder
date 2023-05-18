// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/postgen.h"

#include "Jit/lir/inliner.h"

using namespace jit::codegen;

namespace jit::lir {

Rewrite::RewriteResult PostGenerationRewrite::rewriteInlineHelper(
    function_rewrite_arg_t func) {
  if (g_disable_lir_inliner) {
    return kUnchanged;
  }

  return LIRInliner::inlineCalls(func) ? kChanged : kUnchanged;
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteBinaryOpConstantPosition(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  auto block = instr->basicblock();

  if (instr->isDiv() || instr->isDivUn()) {
    auto divisor = instr->getInput(2);
    if (divisor->isImm()) {
      // div doesn't support an immediate as the divisor
      auto constant = divisor->getConstant();
      auto constant_size = divisor->dataType();

      auto move = block->allocateInstrBefore(
          instr_iter,
          Instruction::kMove,
          OutVReg(constant_size),
          Imm(constant, constant_size));

      instr->removeInputOperand(2);
      instr->allocateLinkedInput(move);
      return kChanged;
    }
    return kUnchanged;
  }

  if (!instr->isAdd() && !instr->isSub() && !instr->isXor() &&
      !instr->isAnd() && !instr->isOr() && !instr->isMul() &&
      !instr->isCompare()) {
    return kUnchanged;
  }

  bool is_commutative = !instr->isSub();
  auto input0 = instr->getInput(0);
  auto input1 = instr->getInput(1);

  if (!input0->isImm()) {
    return kUnchanged;
  }

  if (is_commutative && !input1->isImm()) {
    // if the operation is commutative and the second input is not also an
    // immediate, just swap the operands
    if (instr->isCompare()) {
      instr->setOpcode(Instruction::flipComparisonDirection(instr->opcode()));
    }
    auto imm = instr->removeInputOperand(0);
    instr->appendInputOperand(std::move(imm));
    return kChanged;
  }

  // otherwise, need to insert a move instruction
  auto constant = input0->getConstant();
  auto constant_size = input0->dataType();

  auto move = block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutVReg(constant_size),
      Imm(constant, constant_size));

  instr->allocateLinkedInput(move);
  auto new_input = instr->removeInputOperand(instr->getNumInputs() - 1);
  instr->replaceInputOperand(0, std::move(new_input));

  return kChanged;
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteBinaryOpLargeConstant(
    instr_iter_t instr_iter) {
  // rewrite
  //     Vreg2 = BinOp Vreg1, Imm64
  // to
  //     Vreg0 = Mov Imm64
  //     Vreg2 = BinOP Vreg1, VReg0

  auto instr = instr_iter->get();

  if (!instr->isAdd() && !instr->isSub() && !instr->isXor() &&
      !instr->isAnd() && !instr->isOr() && !instr->isMul() &&
      !instr->isCompare()) {
    return kUnchanged;
  }

  // if first operand is an immediate, we need to swap the operands
  if (instr->getInput(0)->isImm()) {
    // another rewrite will fix this later
    return kUnchanged;
  }

  JIT_CHECK(
      !instr->getInput(0)->isImm(),
      "The first input operand of a binary op instruction should not be "
      "constant");

  auto in1 = instr->getInput(1);
  if (!in1->isImm() || in1->sizeInBits() < 64) {
    return kUnchanged;
  }

  auto constant = in1->getConstant();

  if (fitsInt32(constant)) {
    return kUnchanged;
  }

  auto block = instr->basicblock();
  auto move = block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutVReg(),
      Imm(constant, in1->dataType()));

  // remove the constant input
  instr->setNumInputs(instr->getNumInputs() - 1);
  instr->allocateLinkedInput(move);
  return kChanged;
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteMoveToMemoryLargeConstant(
    instr_iter_t instr_iter) {
  // rewrite
  //     [Vreg0 + offset] = Imm64
  // to
  //     Vreg1 = Mov Imm64
  //     [Vreg0 + offset] = Vreg1

  auto instr = instr_iter->get();

  if (!instr->isMove()) {
    return kUnchanged;
  }

  auto out = instr->output();
  if (!out->isInd()) {
    return kUnchanged;
  }

  auto input = instr->getInput(0);
  if (!input->isImm() && !input->isMem()) {
    return kUnchanged;
  }

  auto constant = input->getConstantOrAddress();
  if (fitsInt32(constant)) {
    return kUnchanged;
  }

  auto block = instr->basicblock();
  auto move = block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutVReg(),
      Imm(constant, input->dataType()));

  // remove the constant input
  instr->setNumInputs(instr->getNumInputs() - 1);
  instr->allocateLinkedInput(move);
  return kChanged;
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteGuardLargeConstant(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  if (!instr->isGuard()) {
    return kUnchanged;
  }

  constexpr size_t kTargetIndex = 3;
  auto target_opnd = instr->getInput(kTargetIndex);
  if (!target_opnd->isImm() && !target_opnd->isMem()) {
    return kUnchanged;
  }

  auto target_imm = target_opnd->getConstantOrAddress();
  if (fitsInt32(target_imm)) {
    return kUnchanged;
  }

  auto block = instr->basicblock();
  auto move = block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutVReg(),
      Imm(target_imm, target_opnd->dataType()));
  auto instr_in = std::make_unique<LinkedOperand>(instr, move);
  instr->replaceInputOperand(kTargetIndex, std::move(instr_in));
  return kChanged;
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteLoadArg(
    instr_iter_t instr_iter,
    Environ* env) {
  auto instr = instr_iter->get();
  if (!instr->isLoadArg()) {
    return kUnchanged;
  }
  instr->setOpcode(Instruction::kBind);
  JIT_CHECK(instr->getNumInputs() == 1, "expected one input");
  OperandBase* input = instr->getInput(0);
  JIT_CHECK(input->isImm(), "expected constant arg index as input");
  auto arg_idx = input->getConstant();
  auto loc = env->arg_locations[arg_idx];
  static_cast<Operand*>(input)->setPhyRegOrStackSlot(loc);
  static_cast<Operand*>(input)->setDataType(instr->output()->dataType());
  return kChanged;
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteBatchDecrefInstrs(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  if (!instr->isBatchDecref()) {
    return kUnchanged;
  }

  // we translate BatchDecref by converting it to a Call instruction
  instr->setOpcode(Instruction::kCall);

  instr->prependInputOperand(std::make_unique<Operand>(
      nullptr,
      Operand::k64bit,
      Operand::kImm,
      reinterpret_cast<uint64_t>(FUNC_MARKER_BATCHDECREF)));
  return kChanged;
}

static void populateLoadSecondCallResultPhi(
    OperandBase::DataType data_type,
    Instruction* phi1,
    Instruction* phi2,
    UnorderedMap<Operand*, Instruction*>& seen_srcs);

// Return an Instruction* (which may already exist) defining the second call
// result for src, with the given DataType.
//
// instr, if given, will be reused rather than inserting a new instruction (to
// preserve its vreg identity).
//
// seen_srcs is used to ensure only one Move is inserted for each root Call
// instruction in the presence of loops or repeated Phi uses of the same vreg.
static Instruction* getSecondCallResult(
    OperandBase::DataType data_type,
    Operand* src,
    Instruction* instr,
    UnorderedMap<Operand*, Instruction*>& seen_srcs) {
  auto it = seen_srcs.find(src);
  if (it != seen_srcs.end()) {
    return it->second;
  }
  Instruction* src_instr = src->instr();
  BasicBlock* src_block = src_instr->basicblock();
  auto src_it = src_block->iterator_to(src_instr);
  JIT_CHECK(
      src_instr->isCall() || src_instr->isPhi(),
      "LoadSecondCallResult input must come from Call or Phi, not '%s'",
      *src_instr);

  if (src_instr->isCall()) {
    // Check that this Call hasn't already been handled on behalf of another
    // LoadSecondCallResult. If we need to support this pattern in the future,
    // this rewrite function should probably become a standalone pass, with the
    // scope of seen_srcs expanded to the whole function.
    auto next_it = std::next(src_it);
    if (next_it != src_block->instructions().end()) {
      Instruction* next_instr = next_it->get();
      JIT_CHECK(
          !(next_instr->isMove() && next_instr->getNumInputs() == 1 &&
            next_instr->getInput(0)->isReg() &&
            next_instr->getInput(0)->getPhyRegister() == RETURN_REGS[1]),
          "Call output consumed by multiple LoadSecondCallResult instructions");
    }
  }

  if (instr) {
    // We want to keep using the vreg defined by instr, so move it to after
    // src_instr, rather than allocating a new one.
    BasicBlock* instr_block = instr->basicblock();
    auto instr_it = instr_block->iterator_to(instr);
    auto instr_owner = instr_block->removeInstr(instr_it);
    src_block->instructions().insert(std::next(src_it), std::move(instr_owner));
    instr->setNumInputs(0);
  }

  Instruction::Opcode new_op =
      src_instr->isCall() ? Instruction::kMove : Instruction::kPhi;
  if (instr) {
    instr->setOpcode(new_op);
  } else {
    instr = src_block->allocateInstrBefore(
        std::next(src_it), new_op, OutVReg(data_type));
  }
  seen_srcs[src] = instr;
  if (new_op == Instruction::kMove) {
    instr->addOperands(PhyReg(RETURN_REGS[1], data_type));
  } else {
    // instr is now a Phi (either newly-created or a replacement for
    // instr). Recursively populate its inputs with the second result of all
    // original Calls.
    populateLoadSecondCallResultPhi(data_type, src_instr, instr, seen_srcs);
  }

  return instr;
}

// Given a Phi that joins the outputs of multiple Calls (or more Phis that
// ultimately join the outputs of Calls), populate a second, parallel Phi to
// join the second result of all original Calls.
static void populateLoadSecondCallResultPhi(
    OperandBase::DataType data_type,
    Instruction* phi1,
    Instruction* phi2,
    UnorderedMap<Operand*, Instruction*>& seen_srcs) {
  for (size_t i = 1; i < phi1->getNumInputs(); i += 2) {
    Operand* src1 = phi1->getInput(i)->getDefine();
    Instruction* instr2 =
        getSecondCallResult(data_type, src1, nullptr, seen_srcs);
    phi2->addOperands(
        Lbl(phi1->getInput(i - 1)->getBasicBlock()), VReg(instr2));
  }
}

Rewrite::RewriteResult PostGenerationRewrite::rewriteLoadSecondCallResult(
    instr_iter_t instr_iter) {
  // Replace "%x = LoadSecondCallResult %y" with "%x = Move RDX" immediately
  // after the call that defines %y. If necessary, trace through Phis,
  // inserting multiple Moves and a new Phi to reconcile them.

  Instruction* instr = instr_iter->get();
  if (!instr->isLoadSecondCallResult()) {
    return kUnchanged;
  }

  Operand* src = instr->getInput(0)->getDefine();
  UnorderedMap<Operand*, Instruction*> seen_srcs;
  getSecondCallResult(instr->output()->dataType(), src, instr, seen_srcs);
  return kRemoved;
}

} // namespace jit::lir
