// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/block.h"

#include "Jit/codegen/code_section.h"
#include "Jit/lir/function.h"
#include "Jit/lir/printer.h"

#include <algorithm>
#include <iostream>

namespace jit::lir {

BasicBlock::BasicBlock(Function* func)
    : id_(func->allocateId()),
      func_(func),
      section_(jit::codegen::CodeSection::kHot) {}

BasicBlock* BasicBlock::insertBasicBlockBetween(BasicBlock* block) {
  auto i = std::find(successors_.begin(), successors_.end(), block);
  JIT_DCHECK(i != successors_.end(), "block must be one of the successors.");

  auto new_block = func_->allocateBasicBlockAfter(this);
  *i = new_block;
  new_block->predecessors_.push_back(this);

  auto& old_preds = block->predecessors_;
  old_preds.erase(std::find(old_preds.begin(), old_preds.end(), this));

  new_block->addSuccessor(block);

  return new_block;
}

void BasicBlock::print() const {
  std::cerr << *this;
}

BasicBlock* BasicBlock::splitBefore(Instruction* instr) {
  JIT_CHECK(
      func_ != nullptr, "cannot split block that doesn't belong to a function");
  JIT_CHECK(
      instr->opcode() != Instruction::kPhi, "cannot split block at a phi node");

  // find the instruction
  InstrList::iterator it = instrs_.begin();
  while (it != instrs_.end()) {
    if (it->get() == instr) {
      break;
    } else {
      ++it;
    }
  }

  // the instruction should be in the basic block, otherwise we cannot split
  if (it == instrs_.end()) {
    return nullptr;
  }

  auto second_block = func_->allocateBasicBlockAfter(this);
  // move all instructions after iterator
  while (it != instrs_.end()) {
    it->get()->setbasicblock(second_block);
    second_block->appendInstr(std::move(*it));
    it = instrs_.erase(it);
  }

  // fix up successors
  for (auto bb : successors_) {
    // fix up phis in successors
    bb->fixupPhis(this, second_block);
    // update successors of second block
    second_block->successors().push_back(bb);
    replace(
        bb->predecessors().begin(),
        bb->predecessors().end(),
        this,
        second_block);
  }

  // update successors of first block
  successors_.clear();
  // addSuccessor also fixes predecessors of second block
  addSuccessor(second_block);
  return second_block;
}

void BasicBlock::fixupPhis(BasicBlock* old_pred, BasicBlock* new_pred) {
  foreachPhiInstr([&](Instruction* instr) {
    for (size_t i = 0, n = instr->getNumInputs(); i < n; ++i) {
      auto block = instr->getInput(i);
      if (block->type() == Operand::kLabel) {
        if (block->getBasicBlock() == old_pred) {
          static_cast<Operand*>(block)->setBasicBlock(new_pred);
        }
      }
    }
  });
}

void BasicBlock::setSuccessor(size_t index, BasicBlock* bb) {
  JIT_CHECK(index < successors_.size(), "Index out of range");
  BasicBlock* old_bb = successors_.at(index);
  std::vector<BasicBlock*>& old_preds = old_bb->predecessors_;
  old_preds.erase(std::find(old_preds.begin(), old_preds.end(), this));

  successors_.at(index) = bb;
  bb->predecessors_.push_back(this);
}

BasicBlock::InstrList::iterator BasicBlock::iterator_to(Instruction* instr) {
  for (auto it = instrs_.begin(); it != instrs_.end(); ++it) {
    if (it->get() == instr) {
      return it;
    }
  }
  JIT_CHECK(false, "Instruction not found in list");
}

} // namespace jit::lir
