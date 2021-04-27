// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/block.h"

#include "Jit/lir/function.h"
#include "Jit/lir/printer.h"

#include <algorithm>
#include <iostream>

namespace jit {
namespace lir {

BasicBlock::BasicBlock(Function* func) : id_(func->allocateId()), func_(func) {}

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

} // namespace lir
} // namespace jit
