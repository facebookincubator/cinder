#include "Jit/codegen/postalloc.h"
#include "Jit/codegen/environ.h"
#include "Jit/codegen/regalloc.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/operand.h"
#include "Jit/util.h"

using namespace jit::lir;

namespace jit::codegen {

void PostRegAllocRewrite::registerRewrites() {
  registerOneRewriteFunction(rewriteCallInstrs);
  registerOneRewriteFunction(rewriteBitExtensionInstrs);
  registerOneRewriteFunction(rewriteBranchInstrs);
  registerOneRewriteFunction(rewriteLoadInstrs);
  registerOneRewriteFunction(rewriteCondBranch);
  registerOneRewriteFunction(rewriteBinaryOpInstrs);
  registerOneRewriteFunction(removePhiInstructions);
  registerOneRewriteFunction(rewriteByteMultiply);
  registerOneRewriteFunction(optimizeMoveInstrs);
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
    if (instr->getInput(0)->isImm()) {
      void* func = reinterpret_cast<void*>(instr->getInput(0)->getConstant());
      if (func == JITRT_CallMethod) {
        // mov r8, [rsp]
        block->allocateInstrBefore(
            instr_iter,
            Instruction::kMove,
            OutPhyReg(PhyLocation::R8),
            Ind(PhyLocation::RSP));
      }
    }
    rsp_sub = rewriteVectorCallFunctions(instr_iter);
  } else if (instr->getInput(0)->isImm()) {
    void* func = reinterpret_cast<void*>(instr->getInput(0)->getConstant());
    if (func == JITRT_GetMethod) {
      rsp_sub = rewriteGetMethodFunction(instr_iter);
    } else if (func == JITRT_GetMethodFromSuper) {
      rsp_sub = rewriteGetSuperMethodFunction(instr_iter);
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

  if (output->type() == OperandBase::kNone ||
      output->getPhyRegOrStackSlot() == PhyLocation::RAX) {
    return kChanged;
  }

  block->allocateInstrBefore(
      next_iter,
      Instruction::kMove,
      OutPhyRegStack(output->getPhyRegOrStackSlot(), output->dataType()),
      PhyReg(PhyLocation::RAX));
  output->setNone();

  return kChanged;
}

int PostRegAllocRewrite::rewriteRegularFunction(instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  auto block = instr->basicblock();

  constexpr int num_arg_regs = sizeof(ARGUMENT_REGS) / sizeof(ARGUMENT_REGS[0]);
  constexpr int num_fp_arg_regs =
      sizeof(FP_ARGUMENT_REGS) / sizeof(FP_ARGUMENT_REGS[0]);

  auto num_inputs = instr->getNumInputs();
  int arg_reg = 0;
  int fp_arg_reg = 0;
  int stack_arg_size = 0;

  for (size_t i = 1; i < num_inputs; i++) {
    auto operand = instr->getInput(i);
    bool operand_imm = operand->isImm();

    if (operand->isFp()) {
      if (fp_arg_reg < num_fp_arg_regs) {
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
          move->allocatePhyRegOrStackInput(operand->getPhyRegOrStackSlot())
              ->setDataType(OperandBase::kDouble);
        }
      } else {
        insertMoveToMemoryLocation(
            block, instr_iter, PhyLocation::RSP, stack_arg_size, operand);
        stack_arg_size += sizeof(void*);
      }
      continue;
    }

    if (arg_reg < num_arg_regs) {
      auto move = block->allocateInstrBefore(instr_iter, Instruction::kMove);
      move->output()->setPhyRegister(ARGUMENT_REGS[arg_reg++]);
      if (operand_imm) {
        move->allocateImmediateInput(
            operand->getConstant(), operand->dataType());
      } else {
        move->allocatePhyRegOrStackInput(operand->getPhyRegOrStackSlot());
      }
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

  // For vector calls there are 4 fixed areguments:
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
  auto self = instr->getInput(2);
  auto move = block->allocateInstrBefore(instr_iter, Instruction::kMove);
  move->output()->setPhyRegister(PhyLocation::RDI);
  if (self->type() == OperandBase::kImm) {
    move->allocateImmediateInput(self->getConstant());
  } else {
    move->allocatePhyRegOrStackInput(self->getPhyRegOrStackSlot());
  }

  constexpr PhyLocation TMP_REG = PhyLocation::RAX;
  for (size_t i = kFirstArg; i < kFirstArg + num_args; i++) {
    auto arg = instr->getInput(i);
    int arg_offset = (i - kFirstArg) * PTR_SIZE;
    insertMoveToMemoryLocation(
        block, instr_iter, kArgBaseReg, arg_offset, arg, TMP_REG);
  }

  // check if kwnames is provided
  auto last_input = instr->getInput(instr->getNumInputs() - 1);
  if (last_input->isImm()) {
    JIT_DCHECK(last_input->getConstant() == 0, "kwnames must be 0 or variable");
    block->allocateInstrBefore(
        instr_iter,
        Instruction::kXor,
        PhyReg(PhyLocation::RCX),
        PhyReg(PhyLocation::RCX));
  } else {
    block->allocateInstrBefore(
        instr_iter,
        Instruction::kMove,
        OutPhyReg(PhyLocation::RCX),
        PhyRegStack(last_input->getPhyRegOrStackSlot()));

    // Subtract the length of kwnames (always a tuple) from nargsf (rdx)
    size_t ob_size_offs = GET_STRUCT_MEMBER_OFFSET(PyVarObject, ob_size);
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

int PostRegAllocRewrite::rewriteGetMethodFunctionWorker(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();
  auto block = instr->basicblock();
  // the first input operand is always JITRT_GetMethod/JITRT_GetMethodFromSuper
  constexpr int CALL_OPERAND_ARG_START = 1;
  auto num_inputs = instr->getNumInputs();

  JIT_DCHECK(
      num_inputs <= std::size(ARGUMENT_REGS),
      "Number of inputs is greater than available ARGUMENT_REGS");

  for (size_t i = CALL_OPERAND_ARG_START; i < num_inputs; i++) {
    auto arg = instr->getInput(i);
    auto reg = ARGUMENT_REGS[i - 1];

    auto move = block->allocateInstrBefore(instr_iter, Instruction::kMove);
    move->output()->setPhyRegister(reg);
    if (arg->type() == OperandBase::kImm) {
      move->allocateImmediateInput(arg->getConstant(), arg->dataType());
    } else {
      move->allocatePhyRegOrStackInput(arg->getPhyRegOrStackSlot());
    }
  }

  block->allocateInstrBefore(
      instr_iter,
      Instruction::kMove,
      OutPhyReg(ARGUMENT_REGS[num_inputs - 1]),
      PhyReg(PhyLocation::RSP));

  return 0;
}

int PostRegAllocRewrite::rewriteGetMethodFunction(instr_iter_t instr_iter) {
  JIT_DCHECK(
      instr_iter->get()->getNumInputs() == 4,
      "signature for JITRT_GetMethod changed");
  return rewriteGetMethodFunctionWorker(instr_iter);
}

int PostRegAllocRewrite::rewriteGetSuperMethodFunction(
    instr_iter_t instr_iter) {
  JIT_DCHECK(
      instr_iter->get()->getNumInputs() == 6,
      "signature for JITRT_GetMethodFromSuper changed");
  return rewriteGetMethodFunctionWorker(instr_iter);
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
    if (successor == next_block) {
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

  auto in_opnd = dynamic_cast<Operand*>(instr->getInput(0));
  if (in_opnd != nullptr && in_opnd->isImm() && !in_opnd->isFp() &&
      in_opnd->getConstant() == 0 && out->type() == OperandBase::kReg) {
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

    if (true_block == next_block) {
      opcode = Instruction::negateBranchCC(opcode);
      target_block = false_block;
    } else if (false_block == next_block) {
      target_block = true_block;
    } else {
      JIT_CHECK(
          false,
          "At least one successor basic block should be placed as the next "
          "basic block.")
    }

    instr->setOpcode(opcode);
    instr->setNumInputs(0);

    instr->allocateLabelInput(target_block);
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
    if (i->output()->type() != OperandBase::kNone &&
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

  JIT_CHECK(
      true_bb == next_block || false_bb == next_block,
      "Either the true basic block or the false basic block needs to come "
      "next.");

  if (true_bb == next_block) {
    instr->setOpcode(Instruction::negateBranchCC(instr->opcode()));
    instr->allocateLabelInput(false_bb);
  } else {
    instr->allocateLabelInput(true_bb);
  }
}

Rewrite::RewriteResult PostRegAllocRewrite::rewriteBinaryOpInstrs(
    instr_iter_t instr_iter) {
  auto instr = instr_iter->get();

  if (!instr->isAdd() && !instr->isSub() && !instr->isXor() &&
      !instr->isAnd() && !instr->isOr() && !instr->isMul()) {
    return kUnchanged;
  }

  // for a binary operation:
  //   Reg2 = BinOp Reg1, Reg0
  // find if Reg2 = Reg1 or Reg2 = Reg0 for commutable operations, so that we
  // can save a move.
  if (instr->output()->type() != OperandBase::kReg) {
    return kUnchanged;
  }

  bool is_commutative = !instr->isSub();

  auto out_reg = instr->output()->getPhyRegister();
  auto in0_reg = instr->getInput(0)->getPhyRegister();

  if (out_reg == in0_reg) {
    // remove the output. the code generator will use the first input
    // as the output (and also the first input).
    instr->output()->setNone();
    return kChanged;
  }

  auto in1 = instr->getInput(1);

  if (is_commutative) {
    auto in1_reg = in1->type() == OperandBase::kReg ? in1->getPhyRegister()
                                                    : PhyLocation::REG_INVALID;
    if (out_reg == in1_reg) {
      instr->output()->setNone();

      auto opnd0 = instr->removeInputOperand(0);
      instr->appendInputOperand(std::move(opnd0));

      return kChanged;
    }
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
        OutPhyReg(PhyLocation::RAX),
        PhyReg(input0->getPhyRegister()));
    input0->setPhyRegister(PhyLocation::RAX);
  }
  // asmjit only recognizes 8-bit imul if RAX is passed as 16-bit.
  input0->setDataType(OperandBase::k16bit);
  output->setNone(); // no output means first input is also output
  if (out_reg != PhyLocation::RAX) {
    block->allocateInstrBefore(
        std::next(instr_iter),
        Instruction::kMove,
        OutPhyReg(out_reg),
        PhyReg(PhyLocation::RAX));
  }
  return kChanged;
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

} // namespace jit::codegen
