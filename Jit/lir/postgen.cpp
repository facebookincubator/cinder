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
    if (divisor->type() == OperandBase::kImm) {
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

  if (input0->type() != OperandBase::kImm) {
    return kUnchanged;
  }

  if (is_commutative && input1->type() != OperandBase::kImm) {
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
  if (instr->getInput(0)->type() == OperandBase::kImm) {
    // another rewrite will fix this later
    return kUnchanged;
  }

  JIT_CHECK(
      instr->getInput(0)->type() != OperandBase::kImm,
      "The first input operand of a binary op instruction should not be "
      "constant");

  auto in1 = instr->getInput(1);
  if (in1->type() != OperandBase::kImm || in1->sizeInBits() < 64) {
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

  auto input = instr->getInput(0);
  if (input->type() != OperandBase::kImm || fitsInt32(input->getConstant())) {
    return kUnchanged;
  }

  auto out = instr->output();
  if (!out->isInd()) {
    return kUnchanged;
  }

  auto constant = input->getConstant();

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
  if (!target_opnd->isImm()) {
    return kUnchanged;
  }

  auto target_imm = target_opnd->getConstant();
  if (fitsInt32(target_imm)) {
    return kUnchanged;
  }

  auto block = instr->basicblock();
  auto move = block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutVReg(),
      Imm(target_imm, OperandBase::k64bit));
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
      reinterpret_cast<uint64_t>(JITRT_BatchDecref)));
  return kChanged;
}
} // namespace jit::lir
