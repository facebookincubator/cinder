// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/lir/block.h"

#include <deque>
#include <iostream>
#include <memory>
#include <vector>

namespace jit::lir {

class Function {
 public:
  int allocateId() {
    return next_id_++;
  }

  struct CopyResult {
    int begin_bb;
    int end_bb;
  };

  // Deep copy function into dest_func.
  // Insert the blocks between prev_bb and next_bb.
  // Assumes that prev_bb and next_bb appear consecutively
  // in dest_func->basic_blocks_.
  // Returns the range of inserted blocks in dest_func->basic_blocks_.
  // The inserted blocks start at (inclusive) dest_func->basic_blocks_[begin_bb]
  // and end right before (exclusive) dest_func->basic_blocks_[begin_bb].
  CopyResult copyFrom(
      const Function* src_func,
      BasicBlock* prev_bb,
      BasicBlock* next_bb,
      const hir::Instr* origin);

  BasicBlock* allocateBasicBlock();

  BasicBlock* allocateBasicBlockAfter(BasicBlock* block);

  // Returns the list of all the basic blocks.
  // The basic blocks will be in RPO as long as the CFG has not been
  // modified since the last call to SortRPO().
  const std::vector<BasicBlock*>& basicblocks() const {
    return basic_blocks_;
  }
  std::vector<BasicBlock*>& basicblocks() {
    return basic_blocks_;
  }

  BasicBlock* getEntryBlock() {
    if (basic_blocks_.empty()) {
      return nullptr;
    }
    return basic_blocks_.front();
  }
  const BasicBlock* getEntryBlock() const {
    return const_cast<Function*>(this)->getEntryBlock();
  }

  size_t getNumBasicBlocks() const {
    return basic_blocks_.size();
  }

  void sortBasicBlocks();

  void print() const;

 private:
  // The containers below hold all the basic blocks for the Function. The deque
  // holds the actual data for blocks and the vector holds their (eventually)
  // sorted order.
  //
  // We use a deque for the data as it provides relatively cheap append
  // (compared to a list) while also keeping value locations in memory constant.
  // Note the basic_block_store_ may end up holding some dead blocks after
  // sorting. However this doesn't matter so much as the overall Function
  // object shouldn't hang around for too long.
  //
  // The other obvious way to implement this would be to have just basic_blocks_
  // as std::vector<unique_ptr<BasicBlock>>, or std::list<BasicBlock>. However,
  // both of these proved to have surprisingly bad performance in practice.
  // This approach gave a roughly 33% perf improvement over the vector of
  // unique_ptrs for a pathalogically large function.
  std::deque<BasicBlock> basic_block_store_;
  // NOTE: The first basic block should always be the entry basic block,
  // where the function starts. The last basic block should be the exit block,
  // where the function ends.
  std::vector<BasicBlock*> basic_blocks_;

  // The next id to assign to a BasicBlock or Instruction.
  int next_id_{0};

  // used in parser
  void setNextId(int id) {
    next_id_ = id;
  }

  friend class Parser;
};

} // namespace jit::lir
