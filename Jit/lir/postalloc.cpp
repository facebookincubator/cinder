// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/postalloc.h"

#include "Jit/codegen/x86_64.h"
#include "Jit/containers.h"
#include "Jit/lir/operand.h"

#include <optional>

using namespace jit::codegen;

namespace jit::lir {

void PostRegAllocRewrite::registerRewrites() {
  registerOneRewriteFunction(rewriteCallInstrs);
  registerOneRewriteFunction(rewriteBitExtensionInstrs);
  registerOneRewriteFunction(rewriteBranchInstrs);
  registerOneRewriteFunction(rewriteLoadInstrs);
  registerOneRewriteFunction(rewriteCondBranch);
  registerOneRewriteFunction(rewriteBinaryOpInstrs);
  registerOneRewriteFunction(removePhiInstructions);
  registerOneRewriteFunction(rewriteByteMultiply);

  registerOneRewriteFunction(optimizeMoveSequence, 1);
  registerOneRewriteFunction(optimizeMoveInstrs, 1);
  registerOneRewriteFunction(rewriteDivide);
}

Rewrite::RewriteResult PostRegAllocRewrite::removePhiInstructions(
    instr_iter_t instr_iter) {
  auto& instr = *instr_iter;

  if (instr->opcode() == Instruction::kPhi) {
    auto block = instr->basicblock();
    block->removeInstr(instr_iter);
    return kRemoved;
  }

  return kUnchanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteCallInstrs(
    instr_iter_t instr_iter,
    Environ* env) {
  auto instr = instr_iter->get();
  if (!instr->isCall() && !instr->isVectorCall()) {
    return kUnchanged;
  }

  auto output = instr->output();
  if (instr->isCall() && instr->getNumInputs() == 1 && output->isNone()) {
    return kUnchanged;
  }

  int rsp_sub = 0;
  auto block = instr->basicblock();

  if (instr->isVectorCall()) {
    rsp_sub = rewriteVectorCallFunctions(instr_iter);
  } else if (instr->getInput(0)->isImm()) {
    void* func = reinterpret_cast<void*>(instr->getInput(0)->getConstant());
    if (func == JITRT_BatchDecref) {
      rsp_sub = rewriteBatchDecrefFunction(instr_iter);
    } else {
      rsp_sub = rewriteRegularFunction(instr_iter);
    }
  } else {
    rsp_sub = rewriteRegularFunction(instr_iter);
  }

  instr->setNumInputs(1); // leave function self operand only
  instr->setOpcode(Instruction::kCall);

  // change
  //   call immediate_addr
  // to
  //   mov rax, immediate_addr
  //   call rax
  // this is because asmjit would make call to immediate to
  //   call [address]
  // where *address == immediate_addr
  if (instr->getInput(0)->isImm()) {
    auto imm = instr->getInput(0)->getConstant();

    block->allocateInstrBefore(
        instr_iter, Instruction::kMove, OutPhyReg(PhyLocation::RAX), Imm(imm));
    instr->setNumInputs(0);
    instr->addOperands(PhyReg(PhyLocation::RAX));
  }

  auto next_iter = std::next(instr_iter);

  env->max_arg_buffer_size = std::max<int>(env->max_arg_buffer_size, rsp_sub);

  if (output->isNone()) {
    return kChanged;
  }

  const PhyLocation kReturnRegister =
      output->isFp() ? PhyLocation::XMM0 : PhyLocation::RAX;

  if (!output->isReg() || output->getPhyRegister() != kReturnRegister) {
    block->allocateInstrBefore(
        next_iter,
        Instruction::kMove,
        OutPhyRegStack(output->getPhyRegOrStackSlot(), output->dataType()),
        PhyReg(kReturnRegister, output->dataType()));
  }
  output->setNone();

  return kChanged;
}

int PostRegAllocRewrite::rewriteRegularFunction(instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  auto block = instr->basicblock();

  auto num_inputs = instr->getNumInputs();
  size_t arg_reg = 0;
  size_t fp_arg_reg = 0;
  int stack_arg_size = 0;

  for (size_t i = 1; i < num_inputs; i++) {
    auto operand = instr->getInput(i);
    bool operand_imm = operand->isImm();

    if (operand->isFp()) {
      if (fp_arg_reg < FP_ARGUMENT_REGS.size()) {
        if (operand_imm) {
          block->allocateInstrBefore(
              instr_iter,
              Instruction::kMove,
              OutPhyReg(PhyLocation::RAX),
              Imm(operand->getConstant()));
        }
        auto move = block->allocateInstrBefore(instr_iter, Instruction::kMove);
        move->output()->setPhyRegister(FP_ARGUMENT_REGS[fp_arg_reg++]);
        move->output()->setDataType(OperandBase::kDouble);

        if (operand_imm) {
          move->allocatePhyRegisterInput(PhyLocation::RAX);
        } else {
          move->appendInputOperand(instr->releaseInputOperand(i));
        }
      } else {
        insertMoveToMemoryLocation(
            block, instr_iter, PhyLocation::RSP, stack_arg_size, operand);
        stack_arg_size += sizeof(void*);
      }
      continue;
    }

    if (arg_reg < ARGUMENT_REGS.size()) {
      auto move = block->allocateInstrBefore(instr_iter, Instruction::kMove);
      move->output()->setPhyRegister(ARGUMENT_REGS[arg_reg++]);
      move->appendInputOperand(instr->releaseInputOperand(i));
    } else {
      insertMoveToMemoryLocation(
          block, instr_iter, PhyLocation::RSP, stack_arg_size, operand);
      stack_arg_size += sizeof(void*);
    }
  }

  return stack_arg_size;
}

int PostRegAllocRewrite::rewriteVectorCallFunctions(instr_iter_t instr_iter) {
  auto instr = instr_iter->get();

  // For vector calls there are 4 fixed arguments:
  // * #0   - runtime helper function
  // * #1   - flags to be added to nargsf
  // * #2   - callable
  // * #n-1 - kwnames
  constexpr int kFirstArg = 3;
  const int kVectorcallArgsOffset = 1;

  auto flag = instr->getInput(1)->getConstant();
  auto num_args = instr->getNumInputs() - kFirstArg - 1;
  auto num_allocs = num_args + kVectorcallArgsOffset;

  constexpr size_t PTR_SIZE = sizeof(void*);
  int rsp_sub = ((num_allocs % 2) ? num_allocs + 1 : num_allocs) * PTR_SIZE;

  auto block = instr->basicblock();

  // // lea rsi, [rsp + kVectorcallArgsOffset * PTR_SIZE]
  const PhyLocation kArgBaseReg = PhyLocation::RSI;
  block->allocateInstrBefore(
      instr_iter,
      Instruction::kLea,
      OutPhyReg(kArgBaseReg),
      Ind(PhyLocation::RSP, kVectorcallArgsOffset * PTR_SIZE));

  // mov rdx, num_args
  block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutPhyReg(PhyLocation::RDX),
      Imm(num_args | flag | PY_VECTORCALL_ARGUMENTS_OFFSET));

  // first argument - set rdi
  auto move = block->allocateInstrBefore(instr_iter, Instruction::kMove);
  move->output()->setPhyRegister(PhyLocation::RDI);
  move->appendInputOperand(instr->releaseInputOperand(2)); // self

  constexpr PhyLocation TMP_REG = PhyLocation::RAX;
  for (size_t i = kFirstArg; i < kFirstArg + num_args; i++) {
    auto arg = instr->getInput(i);
    int arg_offset = (i - kFirstArg) * PTR_SIZE;
    insertMoveToMemoryLocation(
        block, instr_iter, kArgBaseReg, arg_offset, arg, TMP_REG);
  }

  // check if kwnames is provided
  auto last_input = instr->releaseInputOperand(instr->getNumInputs() - 1);
  if (last_input->isImm()) {
    JIT_DCHECK(last_input->getConstant() == 0, "kwnames must be 0 or variable");
    block->allocateInstrBefore(
        instr_iter,
        Instruction::kXor,
        PhyReg(PhyLocation::RCX),
        PhyReg(PhyLocation::RCX));
  } else {
    auto move = block->allocateInstrBefore(
        instr_iter, Instruction::kMove, OutPhyReg(PhyLocation::RCX));
    move->appendInputOperand(std::move(last_input));

    // Subtract the length of kwnames (always a tuple) from nargsf (rdx)
    size_t ob_size_offs = offsetof(PyVarObject, ob_size);
    block->allocateInstrBefore(
        instr_iter,
        Instruction::kMove,
        OutPhyReg(TMP_REG),
        Ind(PhyLocation::RCX, ob_size_offs));

    block->allocateInstrBefore(
        instr_iter,
        Instruction::kSub,
        PhyReg(PhyLocation::RDX),
        PhyReg(TMP_REG));
  }

  return rsp_sub;
}

int PostRegAllocRewrite::rewriteBatchDecrefFunction(instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  auto block = instr->basicblock();
  constexpr int kArgStart = 1;
  constexpr int kCallMethodSpSlot = 1;
  constexpr PhyLocation kArgBaseReg = PhyLocation::RDI;
  const int num_arguments =
      instr->getNumInputs() - kArgStart + kCallMethodSpSlot;
  const int rsp_sub =
      ((num_arguments % 2) ? num_arguments + 1 : num_arguments) *
      sizeof(PyObject*);

  block->allocateInstrBefore(
      instr_iter,
      Instruction::kLea,
      OutPhyReg(kArgBaseReg),
      Ind(PhyLocation::RSP, sizeof(void*) * kCallMethodSpSlot));

  constexpr PhyLocation TMP_REG = PhyLocation::RAX;
  for (size_t i = kArgStart; i < instr->getNumInputs(); i++) {
    auto arg = instr->getInput(i);
    auto arg_offset = (i - kArgStart) * sizeof(PyObject*);
    insertMoveToMemoryLocation(
        block, instr_iter, kArgBaseReg, arg_offset, arg, TMP_REG);
  }

  block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutPhyReg(PhyLocation::RSI, lir::OperandBase::k32bit),
      Imm(instr->getNumInputs() - kArgStart, lir::OperandBase::k32bit));

  return rsp_sub;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteBitExtensionInstrs(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();

  bool is_sext = instr->opcode() == Instruction::kSext;
  bool is_zext = instr->opcode() == Instruction::kZext;

  if (!is_sext && !is_zext) {
    return kUnchanged;
  }

  auto in = instr->getInput(0);
  auto out = instr->output();
  auto out_size = out->dataType();
  if (in->type() == OperandBase::kImm) {
    long mask = 0;
    if (out_size == OperandBase::k32bit) {
      mask = 0xffffffffl;
    } else if (out_size == OperandBase::k16bit) {
      mask = 0xffffl;
    } else if (out_size == OperandBase::k8bit) {
      mask = 0xffl;
    } else {
      mask = 0xffffffffffffffffl;
    }
    static_cast<Operand*>(in)->setConstant(in->getConstant() & mask, out_size);
    instr->setOpcode(Instruction::kMove);
    return kChanged;
  }

  auto in_size = in->dataType();
  if (in_size >= out_size) {
    instr->setOpcode(Instruction::kMove);
    return kChanged;
  }

  switch (in_size) {
    case OperandBase::k8bit:
    case OperandBase::k16bit:
      instr->setOpcode(is_sext ? Instruction::kMovSX : Instruction::kMovZX);
      break;
    case OperandBase::k32bit:
      if (is_sext) {
        instr->setOpcode(Instruction::kMovSXD);
      } else {
        // must be unsigned extension from 32 bits to 64 bits.
        // in this case, a 32-bit move will do the work.
        instr->setOpcode(Instruction::kMove);
        instr->output()->setDataType(lir::OperandBase::k32bit);
      }
      break;
    case OperandBase::k64bit:
    case OperandBase::kObject:
      JIT_CHECK(false, "can't be smaller than the maximum size");
      break;
    case OperandBase::kDouble:
      JIT_CHECK(
          false,
          "a float point number cannot be the input of the instruction.");
  }

  return kChanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteBranchInstrs(
    Function* function) {
  auto& blocks = function->basicblocks();
  bool changed = false;

  for (auto iter = blocks.begin(); iter != blocks.end();) {
    BasicBlock* block = *iter;
    ++iter;

    BasicBlock* next_block = iter == blocks.end() ? nullptr : *iter;

    auto& succs = block->successors();

    if (succs.size() != 1) {
      // skip conditional branches for now.
      continue;
    }

    auto last_instr = block->getLastInstr();
    auto last_opcode =
        last_instr != nullptr ? last_instr->opcode() : Instruction::kNone;
    if (last_opcode == Instruction::kReturn) {
      continue;
    }

    auto successor = succs[0];
    if (successor == next_block && next_block->section() == block->section()) {
      continue;
    }

    if (last_opcode == Instruction::kBranch) {
      continue;
    }

    auto branch = block->allocateInstr(
        Instruction::kBranch,
        last_instr != nullptr ? last_instr->origin() : nullptr);
    branch->allocateLabelInput(succs[0]);

    changed = true;
  }

  return changed ? kChanged : kUnchanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::optimizeMoveInstrs(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  auto instr_opcode = instr->opcode();
  if (instr_opcode != Instruction::kMove) {
    return kUnchanged;
  }

  auto out = instr->output();
  auto in = instr->getInput(0);

  // if the input and the output are the same
  if ((out->type() == OperandBase::kReg ||
       out->type() == OperandBase::kStack) &&
      in->type() == out->type() &&
      in->getPhyRegOrStackSlot() == out->getPhyRegOrStackSlot()) {
    instr->basicblock()->removeInstr(instr_iter);
    return kRemoved;
  }

  Operand* in_opnd = nullptr;
  auto inp = instr->getInput(0);
  if (inp->isImm() && !inp->isFp() && inp->getConstant() == 0 &&
      out->type() == OperandBase::kReg &&
      (in_opnd = dynamic_cast<Operand*>(inp))) {
    instr->setOpcode(Instruction::kXor);
    auto reg = out->getPhyRegister();
    in_opnd->setPhyRegister(reg);
    instr->allocatePhyRegisterInput(reg);
    out->setNone();
    return kChanged;
  }

  return kUnchanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteLoadInstrs(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();

  if (!instr->isMove() || instr->getNumInputs() != 1 ||
      !instr->getInput(0)->isMem()) {
    return kUnchanged;
  }

  auto out = instr->output();
  JIT_DCHECK(
      out->type() == OperandBase::kReg,
      "Unable to load to a non-register location.");
  if (out->getPhyRegister() == PhyLocation::RAX) {
    return kUnchanged;
  }

  auto in = instr->getInput(0);
  auto mem_addr = reinterpret_cast<intptr_t>(in->getMemoryAddress());
  if (fitsInt32(mem_addr)) {
    return kUnchanged;
  }

  auto block = instr->basicblock();
  block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutPhyReg(out->getPhyRegister()),
      Imm(mem_addr, in->dataType()));

  static_cast<Operand*>(in)->setMemoryIndirect(out->getPhyRegister());

  return kChanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteCondBranch(
    jit::lir::Function* function) {
  auto& blocks = function->basicblocks();

  bool changed = false;
  for (auto iter = blocks.begin(); iter != blocks.end();) {
    BasicBlock* block = *iter;
    ++iter;

    auto instr_iter = block->getLastInstrIter();
    if (instr_iter == block->instructions().end()) {
      continue;
    }

    BasicBlock* next_block = (iter != blocks.end() ? *iter : nullptr);

    auto instr = instr_iter->get();

    if (instr->isCondBranch()) {
      doRewriteCondBranch(instr_iter, next_block);
      changed = true;
    } else if (instr->isBranchCC() && instr->getNumInputs() == 0) {
      doRewriteBranchCC(instr_iter, next_block);
      changed = true;
    }
  }

  return changed ? kChanged : kUnchanged;
}

void PostRegAllocRewrite::doRewriteCondBranch(
    instr_iter_t instr_iter,
    BasicBlock* next_block) {
  auto instr = instr_iter->get();

  auto input = instr->getInput(0);
  auto block = instr->basicblock();

  // insert test Reg, Reg instruction
  auto insert_test = [&]() {
    auto size = input->dataType();
    block->allocateInstrBefore(
        instr_iter,
        Instruction::kTest,
        PhyReg(input->getPhyRegister(), size),
        PhyReg(input->getPhyRegister(), size));
  };

  // convert the current CondBranch instruction to a BranchCC instruction
  auto convert_to_branchcc = [&](Instruction::Opcode opcode) {
    auto true_block = block->getTrueSuccessor();
    auto false_block = block->getFalseSuccessor();

    BasicBlock* target_block = nullptr;
    BasicBlock* fallthrough_block = nullptr;

    if (true_block == next_block) {
      opcode = Instruction::negateBranchCC(opcode);
      target_block = false_block;
      fallthrough_block = true_block;
    } else {
      target_block = true_block;
      fallthrough_block = false_block;
    }

    instr->setOpcode(opcode);
    instr->setNumInputs(0);

    instr->allocateLabelInput(target_block);

    if (fallthrough_block != next_block ||
        block->section() != next_block->section()) {
      auto fallthrough_branch =
          block->allocateInstr(Instruction::kBranch, instr->origin());
      fallthrough_branch->allocateLabelInput(fallthrough_block);
    }
  };

  auto flag_affecting_instr = findRecentFlagAffectingInstr(instr_iter);
  if (flag_affecting_instr == nullptr) {
    insert_test();
    convert_to_branchcc(Instruction::kBranchNZ);
    return;
  }

  if (flag_affecting_instr->isCompare()) {
    // for compare opcodes
    auto cmp_opcode = flag_affecting_instr->opcode();
    auto branchcc_opcode = Instruction::compareToBranchCC(cmp_opcode);

    auto cmp0 = flag_affecting_instr->getInput(0);
    auto cmp1 = flag_affecting_instr->getInput(1);

    // if the comparison output is otherwise unused, we can simplify the compare
    if (flag_affecting_instr->output()->type() == OperandBase::kNone) {
      if (cmp1->type() == OperandBase::kImm && cmp1->getConstant() == 0) {
        // compare with 0 case - generate test Reg, Reg
        auto loc = cmp0->getPhyRegister();
        flag_affecting_instr->setOpcode(Instruction::kTest);
        flag_affecting_instr->setNumInputs(0);
        flag_affecting_instr->allocatePhyRegisterInput(loc);
        flag_affecting_instr->allocatePhyRegisterInput(loc);
      } else {
        flag_affecting_instr->setOpcode(Instruction::kCmp);
      }
    }

    convert_to_branchcc(branchcc_opcode);
    return;
  }

  // for opcodes like Add, Sub, ...
  Instruction* def_instr = nullptr;
  // search between the conditional branch and flag_affecting_instr for the
  // instruction defining the condition operand.
  // The instruction can be in a different basic block, but we don't consider
  // this case. If this happens, we always add a "test cond, cond" instruction
  // conservatively.
  for (auto iter = std::prev(instr_iter); iter->get() != flag_affecting_instr;
       --iter) {
    auto i = iter->get();

    // TODO (tiansi): it is sufficient to only check output here, because all
    // the instructions that inplace write to the first operand also affect
    // flags. Need to add an inplace version for all the inplace write
    // instructions (e.g., InpAdd for Add) so that this check gets more explicit
    // and rigorous.
    if (i->output()->type() == OperandBase::kReg &&
        i->output()->getPhyRegister() == instr->getInput(0)->getPhyRegister()) {
      def_instr = i;
      break;
    }
  }

  if (def_instr != nullptr) {
    insert_test();
    convert_to_branchcc(Instruction::kBranchNZ);
    return;
  }

  PhyLocation loc = PhyLocation::REG_INVALID;
  if (flag_affecting_instr->output()->type() == OperandBase::kNone) {
    auto in0 = flag_affecting_instr->getInput(0);
    if (in0->type() == OperandBase::kReg) {
      loc = flag_affecting_instr->getInput(0)->getPhyRegister();
    }
  } else {
    // output must be a phy register for now.
    loc = flag_affecting_instr->output()->getPhyRegister();
  }

  if (loc != instr->getInput(0)->getPhyRegister()) {
    insert_test();
  }
  convert_to_branchcc(Instruction::kBranchNZ);
}

void PostRegAllocRewrite::doRewriteBranchCC(
    instr_iter_t instr_iter,
    BasicBlock* next_block) {
  auto instr = instr_iter->get();
  auto block = instr->basicblock();

  auto true_bb = block->getTrueSuccessor();
  auto false_bb = block->getFalseSuccessor();
  BasicBlock* fallthrough_bb = nullptr;

  if (true_bb == next_block) {
    instr->setOpcode(Instruction::negateBranchCC(instr->opcode()));
    instr->allocateLabelInput(false_bb);
    fallthrough_bb = true_bb;
  } else {
    instr->allocateLabelInput(true_bb);
    fallthrough_bb = false_bb;
  }

  if (fallthrough_bb != next_block ||
      block->section() != next_block->section()) {
    auto fallthrough_branch =
        block->allocateInstr(Instruction::kBranch, instr->origin());
    fallthrough_branch->allocateLabelInput(fallthrough_bb);
  }
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteBinaryOpInstrs(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();

  // For a binary operation:
  //
  //   OutReg = BinOp Reg0, Reg1
  //
  // find if OutReg == Reg0 or OutReg == Reg1, so we can rewrite to the
  // two-operand form and save a move in autogen.cpp.
  //
  // Performing this rewrite also makes it safe to not set inputs_live_across
  // on binary ops that write their output before reading all of their inputs:
  // if the output is the same register as one of the inputs, it will be
  // rewritten into the two-operand form here.
  //
  // Subtraction is anticommutative, so we could in theory support it here by
  // negating the output in the (OutReg == Reg1) case. But the Move we're
  // trying to avoid is probably going to be cheaper than the negation anyway,
  // so skip that case. And since we're skipping that case, we have to set
  // inputs_live_across for Sub and Fsub, meaning they can be left out of this
  // rewrite entirely.

  if (!instr->isAdd() && !instr->isXor() && !instr->isAnd() && !instr->isOr() &&
      !instr->isMul() && !instr->isFadd() && !instr->isFmul()) {
    return kUnchanged;
  }

  if (instr->output()->type() != OperandBase::kReg) {
    return kUnchanged;
  }

  auto out_reg = instr->output()->getPhyRegister();
  auto in0_reg = instr->getInput(0)->getPhyRegister();

  if (out_reg == in0_reg) {
    // Remove the output. The code generator will use the first input as the
    // output (and also the first input).
    instr->output()->setNone();
    return kChanged;
  }

  auto in1 = instr->getInput(1);
  auto in1_reg = in1->type() == OperandBase::kReg ? in1->getPhyRegister()
                                                  : PhyLocation::REG_INVALID;
  if (out_reg == in1_reg) {
    instr->output()->setNone();

    auto opnd0 = instr->removeInputOperand(0);
    instr->appendInputOperand(std::move(opnd0));
    return kChanged;
  }

  return kUnchanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteByteMultiply(
    instr_iter_t instr_iter) {
  Instruction* instr = instr_iter->get();

  if (!instr->isMul() || instr->getNumInputs() < 2) {
    return kUnchanged;
  }

  Operand* input0 = static_cast<Operand*>(instr->getInput(0));

  if (input0->dataType() > OperandBase::k8bit) {
    return kUnchanged;
  }

  Operand* output = static_cast<Operand*>(instr->output());
  PhyLocation in_reg = input0->getPhyRegister();
  PhyLocation out_reg = in_reg;

  if (output->type() == OperandBase::kReg) {
    out_reg = output->getPhyRegister();
  }

  BasicBlock* block = instr->basicblock();
  if (in_reg != PhyLocation::RAX) {
    block->allocateInstrBefore(
        instr_iter,
        Instruction::kMove,
        OutPhyReg(PhyLocation::RAX, OperandBase::k8bit),
        PhyReg(in_reg, OperandBase::k8bit));
    input0->setPhyRegister(PhyLocation::RAX);
  }
  // asmjit only recognizes 8-bit imul if RAX is passed as 16-bit.
  input0->setDataType(OperandBase::k16bit);
  output->setNone(); // no output means first input is also output
  if (out_reg != PhyLocation::RAX) {
    block->allocateInstrBefore(
        std::next(instr_iter),
        Instruction::kMove,
        OutPhyReg(out_reg, OperandBase::k8bit),
        PhyReg(PhyLocation::RAX, OperandBase::k8bit));
  }
  return kChanged;
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteDivide(
    instr_iter_t instr_iter) {
  Instruction* instr = instr_iter->get();
  if (!instr->isDiv() && !instr->isDivUn()) {
    return kUnchanged;
  }

  bool changed = false;
  Operand* output = static_cast<Operand*>(instr->output());

  BasicBlock* block = instr->basicblock();

  Operand* dividend_upper = nullptr;
  Operand* dividend_lower;
  if (instr->getNumInputs() == 3) {
    dividend_upper = static_cast<Operand*>(instr->getInput(0));
    dividend_lower = static_cast<Operand*>(instr->getInput(1));
  } else {
    dividend_lower = static_cast<Operand*>(instr->getInput(0));
  }

  PhyLocation out_reg = PhyLocation::RAX;
  if (output->type() != OperandBase::kNone) {
    out_reg = output->getPhyRegister();
  } else {
    JIT_CHECK(
        dividend_lower->type() == OperandBase::kReg,
        "input should be in register");
    out_reg = dividend_lower->getPhyRegister();
  }

  if (dividend_lower->dataType() == OperandBase::k8bit) {
    // 8-bit division uses 16-bits from ax instead of using
    // dx as the upper word, so we need to sign extend it to
    // be a 16-bit input (we'll use the size from the divisor
    // as the size of the instruction, setting the size on
    // divided_lower here is just tracking that we've done
    // the transformation).  When we do this we'll re-write
    // it down to the 2 input form and make dividend_lower
    // be 16-bit.
    JIT_CHECK(
        instr->getNumInputs() == 3,
        "8-bit should always start with 3 operands");
    auto move = block->allocateInstrBefore(
        instr_iter,
        dividend_lower->type() == OperandBase::kImm ? Instruction::kMove
            : instr->isDiv()                        ? Instruction::kMovSX
                                                    : Instruction::kMovZX,
        OutPhyReg(PhyLocation::RAX, OperandBase::k16bit));

    if (dividend_lower->type() == OperandBase::kImm) {
      dividend_lower->setDataType(OperandBase::k16bit);
    }

    auto divisor_removed = instr->removeInputOperand(2);
    auto div_lower_removed = instr->removeInputOperand(1);
    move->appendInputOperand(std::move(div_lower_removed));

    instr->removeInputOperand(0); // Imm/rdx, no longer used

    instr->addOperands(PhyReg(PhyLocation::RAX, OperandBase::k16bit));
    instr->appendInputOperand(std::move(divisor_removed));
    changed = true;
  } else {
    // dividend lower needs to be in rax, we reserved the register
    // in reg_alloc.
    changed |= insertMoveToRegister(
        block, instr_iter, dividend_lower, PhyLocation::RAX);

    if (dividend_upper != nullptr &&
        (dividend_upper->type() != OperandBase::kReg ||
         dividend_upper->getPhyRegister() != PhyLocation::RDX)) {
      JIT_CHECK(
          (dividend_upper->type() == OperandBase::kImm &&
           dividend_upper->getConstant() == 0),
          "only immediate 0 is supported");

      if (instr->isDiv()) {
        // extend rax into rdx
        Instruction::Opcode extend;
        switch (dividend_lower->sizeInBits()) {
          case 16:
            extend = Instruction::kCwd;
            break;
          case 32:
            extend = Instruction::kCdq;
            break;
          case 64:
            extend = Instruction::kCqo;
            break;
          default:
            Py_UNREACHABLE();
        }
        block->allocateInstrBefore(
            instr_iter,
            extend,
            OutPhyReg(PhyLocation::RDX),
            PhyReg(PhyLocation::RAX));
      } else {
        // zero rdx
        block->allocateInstrBefore(
            instr_iter,
            Instruction::kXor,
            PhyReg(PhyLocation::RDX),
            PhyReg(PhyLocation::RDX));
      }

      dividend_upper->setPhyRegister(PhyLocation::RDX);
      dividend_upper->setDataType(dividend_lower->dataType());
      changed = true;
    }
  }

  if (out_reg != PhyLocation::RAX) {
    block->allocateInstrBefore(
        std::next(instr_iter),
        Instruction::kMove,
        OutPhyReg(out_reg, dividend_lower->dataType()),
        PhyReg(PhyLocation::RAX, dividend_lower->dataType()));
    changed = true;
  }
  output->setNone();

  return changed ? kChanged : kUnchanged;
}

bool PostRegAllocRewrite::insertMoveToRegister(
    lir::BasicBlock* block,
    instr_iter_t instr_iter,
    Operand* op,
    PhyLocation location) {
  if (op->type() != OperandBase::kReg || op->getPhyRegister() != location) {
    auto move = block->allocateInstrBefore(
        instr_iter, Instruction::kMove, OutPhyReg(location, op->dataType()));

    switch (op->type()) {
      case OperandBase::kReg:
        move->addOperands(PhyReg(op->getPhyRegister(), op->dataType()));
        break;
      case OperandBase::kImm:
        move->addOperands(Imm(op->getConstant()));
        break;
      case OperandBase::kStack:
        move->addOperands(Stk(op->getPhyRegOrStackSlot(), op->dataType()));
        break;
      case OperandBase::kMem:
        JIT_CHECK(false, "unsupported: div from mem");
        break;
      case OperandBase::kVreg:
      case OperandBase::kLabel:
      case OperandBase::kInd:
      case OperandBase::kNone:
        JIT_CHECK(false, "unexpected operand base");
        break;
    }

    op->setPhyRegister(location);
    return true;
  }
  return false;
}

void PostRegAllocRewrite::insertMoveToMemoryLocation(
    BasicBlock* block,
    instr_iter_t instr_iter,
    PhyLocation base,
    int index,
    const OperandBase* operand,
    PhyLocation temp) {
  if (operand->isImm()) {
    auto constant = operand->getConstant();
    if (!fitsInt32(constant) || operand->isFp()) {
      block->allocateInstrBefore(
          instr_iter, Instruction::kMove, OutPhyReg(temp), Imm(constant));
      block->allocateInstrBefore(
          instr_iter, Instruction::kMove, OutInd(base, index), PhyReg(temp));
    } else {
      block->allocateInstrBefore(
          instr_iter, Instruction::kMove, OutInd(base, index), Imm(constant));
    }
    return;
  }

  PhyLocation loc = operand->getPhyRegOrStackSlot();
  if (loc.is_memory()) {
    block->allocateInstrBefore(
        instr_iter, Instruction::kMove, OutPhyReg(temp), Stk(loc));
    block->allocateInstrBefore(
        instr_iter, Instruction::kMove, OutInd(base, index), PhyReg(temp));
    return;
  }

  block->allocateInstrBefore(
      instr_iter, Instruction::kMove, OutInd(base, index), PhyReg(loc));
}

// record register-to-memory moves and map between them.
class RegisterToMemoryMoves {
 public:
  void addRegisterToMemoryMove(
      PhyLocation from,
      PhyLocation to,
      Rewrite::instr_iter_t instr_iter) {
    JIT_DCHECK(
        from.is_register() && to.is_memory(),
        "Must be a move from register to memory");
    invalidateMemory(to);
    invalidateRegister(from);

    reg_to_mem_[from] = to;
    mem_to_reg_[to] = {from, instr_iter};
  }

  void invalidate(PhyLocation loc) {
    if (loc.is_register()) {
      invalidateRegister(loc);
    } else {
      invalidateMemory(loc);
    }
  }

  PhyLocation getRegisterFromMemory(PhyLocation mem) {
    auto iter = mem_to_reg_.find(mem);
    if (iter != mem_to_reg_.end()) {
      return iter->second.first;
    }

    return PhyLocation::REG_INVALID;
  }

  std::optional<Rewrite::instr_iter_t> getInstrFromMemory(PhyLocation mem) {
    auto iter = mem_to_reg_.find(mem);
    if (iter == mem_to_reg_.end()) {
      return std::nullopt;
    }

    return iter->second.second;
  }

  void clear() {
    reg_to_mem_.clear();
    mem_to_reg_.clear();
  }

  bool isEmpty() {
    return reg_to_mem_.empty();
  }

 private:
  UnorderedMap<PhyLocation, PhyLocation> reg_to_mem_;
  UnorderedMap<PhyLocation, std::pair<PhyLocation, Rewrite::instr_iter_t>>
      mem_to_reg_;

  void invalidateRegister(PhyLocation reg) {
    auto iter = reg_to_mem_.find(reg);
    if (iter != reg_to_mem_.end()) {
      mem_to_reg_.erase(iter->second);
      reg_to_mem_.erase(iter);
    }
  }
  void invalidateMemory(PhyLocation mem) {
    auto iter = mem_to_reg_.find(mem);
    if (iter != mem_to_reg_.end()) {
      reg_to_mem_.erase(iter->second.first);
      mem_to_reg_.erase(iter);
    }
  }
};

Rewrite::RewriteResult PostRegAllocRewrite::optimizeMoveSequence(
    BasicBlock* basicblock) {
  auto changed = kUnchanged;
  RegisterToMemoryMoves registerMemoryMoves;

  for (auto instr_iter = basicblock->instructions().begin();
       instr_iter != basicblock->instructions().end();
       ++instr_iter) {
    auto& instr = *instr_iter;
    // TODO: do not optimize for yield for now. They need to be special cased.
    if (!instr->isAnyYield()) {
      auto out_reg = instr->output()->type() == OperandBase::kReg
          ? instr->output()->getPhyRegister()
          : PhyLocation::REG_INVALID;
      // for moves only we can generate A = Move A, which will get optimized out
      if (instr->isMove()) {
        out_reg = PhyLocation::REG_INVALID;
      }
      instr->foreachInputOperand([&](OperandBase* operand) {
        if (!operand->isStack()) {
          return;
        }

        PhyLocation stack_slot = operand->getStackSlot();
        auto reg = registerMemoryMoves.getRegisterFromMemory(stack_slot);
        if (reg == PhyLocation::REG_INVALID || reg == out_reg) {
          return;
        }

        auto opnd = static_cast<Operand*>(operand);
        opnd->setPhyRegister(reg);
        changed = kChanged;

        // if the stack location operand can be replaced by the register it came
        // from and this is the last use of the operand, we can remove the move
        // instruction moving from the register to the stack location.
        if (opnd->isLastUse()) {
          auto opt_iter = registerMemoryMoves.getInstrFromMemory(stack_slot);
          JIT_CHECK(opt_iter.has_value(), "There must be a def instruction.");
          basicblock->instructions().erase(*opt_iter);
        }
      });
    }

    auto invalidateOperand = [&](const OperandBase* opnd) {
      if (opnd->isStack() || opnd->isReg()) {
        registerMemoryMoves.invalidate(opnd->getPhyRegOrStackSlot());
      }
    };

    if (instr->isMove() || instr->isPush() || instr->isPop()) {
      if (instr->isMove()) {
        Operand* out = instr->output();
        OperandBase* in = instr->getInput(0);
        if (out->isStack() && in->isReg()) {
          registerMemoryMoves.addRegisterToMemoryMove(
              in->getPhyRegister(), out->getStackSlot(), instr_iter);
        } else {
          invalidateOperand(out);
        }
      } else if (instr->isPop()) {
        auto opnd = instr->output();
        invalidateOperand(opnd);
      }
    } else {
      // TODO: for now, we always clear the cache when we hit an instruction
      // other than MOVE, PUSH, and POP, since our main goal is to optimize the
      // operand copies before a function call. Consider a more fine-grained
      // control of what to invalidate for better results.
      registerMemoryMoves.clear();
    }
  }
  return changed;
}
} // namespace jit::lir
