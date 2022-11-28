// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/containers.h"
#include "Jit/lir/block.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace jit::lir {

// this struct represents a group of basic blocks that
// are strongly conncted to each other.
struct SCCBasicBlocks {
  // only one entry basic block. Otherwise, the CFG
  // is irreducible, which we don't support.
  BasicBlock* entry{nullptr};
  UnorderedSet<BasicBlock*> basic_blocks;
  std::vector<SCCBasicBlocks*> successors;

  bool hasBasicBlock(BasicBlock* block) const {
    return basic_blocks.count(block);
  }
};

class BasicBlockSorter {
 public:
  // The first entry of blocks is the entry block, and the last entry is the
  // exit block. The position of both will be maintained after sorting.
  explicit BasicBlockSorter(const std::vector<BasicBlock*>& blocks);

  std::vector<BasicBlock*> getSortedBlocks();

 private:
  BasicBlockSorter(const UnorderedSet<BasicBlock*>& blocks, BasicBlock* entry);

  BasicBlock* entry_{nullptr};
  BasicBlock* exit_{nullptr};
  UnorderedSet<BasicBlock*> basic_blocks_store_;
  // This is a ref to either basic_blocks_store_ or the basic_blocks field of
  // the SCCBasicBlocks being processed. This allows us to avoid some copying
  // since this set is never modified.
  const UnorderedSet<BasicBlock*>& basic_blocks_;

  std::vector<BasicBlock*> scc_stack_;
  UnorderedSet<BasicBlock*> scc_in_stack_;
  std::unordered_map<BasicBlock*, int> scc_visited_;
  int index_;

  UnorderedMap<BasicBlock*, SCCBasicBlocks*> block_to_scc_map_;
  std::vector<std::unique_ptr<SCCBasicBlocks>> scc_blocks_;
  void calculateSCC();
  int dfsSearch(BasicBlock* block);

  void calcEntryBlocks();
  void sortRPO();
};

} // namespace jit::lir
