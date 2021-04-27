// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/function.h"
#include "Jit/lir/blocksorter.h"
#include "Jit/lir/printer.h"

#include <stack>
#include <unordered_map>
#include <unordered_set>

namespace jit {
namespace lir {

void Function::sortBasicBlocks() {
  BasicBlockSorter sorter(basic_blocks_);
  basic_blocks_ = sorter.getSortedBlocks();
}

BasicBlock* Function::allocateBasicBlock() {
  basic_block_store_.emplace_back(this);
  BasicBlock* new_block = &basic_block_store_.back();
  basic_blocks_.emplace_back(new_block);
  return new_block;
}

BasicBlock* Function::allocateBasicBlockAfter(BasicBlock* block) {
  auto iter = std::find_if(
      basic_blocks_.begin(),
      basic_blocks_.end(),
      [block](const BasicBlock* a) -> bool { return block == a; });
  ++iter;
  basic_block_store_.emplace_back(this);
  BasicBlock* new_block = &basic_block_store_.back();
  basic_blocks_.emplace(iter, new_block);
  return new_block;
}

void Function::print() const {
  std::cerr << *this;
}

} // namespace lir
} // namespace jit
