// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/blocksorter.h"

#include "Jit/log.h"
#include "Jit/util.h"

#include <algorithm>
#include <limits>
#include <stack>

namespace jit::lir {

BasicBlockSorter::BasicBlockSorter(const std::vector<BasicBlock*>& blocks)
    : entry_(blocks.empty() ? nullptr : blocks[0]),
      exit_(blocks.empty() ? nullptr : blocks.back()),
      basic_blocks_store_(blocks.begin(), blocks.end()),
      basic_blocks_(basic_blocks_store_) {}

BasicBlockSorter::BasicBlockSorter(
    const UnorderedSet<BasicBlock*>& blocks,
    BasicBlock* entry)
    : entry_(entry), basic_blocks_store_(), basic_blocks_(blocks) {
  JIT_DCHECK(blocks.count(entry), "Entry basic block is not in blocks");
}

std::vector<BasicBlock*> BasicBlockSorter::getSortedBlocks() {
  calculateSCC();

  // find the entry block for each SCC block
  // there cannot be multiple entry blocks in a SCC block, otherwise
  // the CFG is irreducible.
  calcEntryBlocks();

  // sort all the SCC blocks in RPO
  sortRPO();

  // expand SCC's to basic blocks
  std::vector<BasicBlock*> result;
  result.reserve(basic_blocks_.size());
  for (auto& sccblock : scc_blocks_) {
    JIT_DCHECK(
        !sccblock->basic_blocks.empty(),
        "Cannot have an SCC with no basic blocks");

    if (sccblock->basic_blocks.size() == 1) {
      JIT_DCHECK(
          sccblock->entry == nullptr ||
              sccblock->hasBasicBlock(sccblock->entry),
          "sccblock is not consistent.");

      result.emplace_back(*(sccblock->basic_blocks.begin()));
    } else {
      // more than one basic blocks - need to sort again within the SCC
      BasicBlockSorter sorter(sccblock->basic_blocks, sccblock->entry);
      auto res = sorter.getSortedBlocks();
      result.insert(result.end(), res.begin(), res.end());
    }
  }

  return result;
}

void BasicBlockSorter::calculateSCC() {
  scc_stack_.clear();
  scc_in_stack_.clear();
  scc_visited_.clear();
  scc_blocks_.clear();
  index_ = 0;

  for (auto& block : basic_blocks_) {
    dfsSearch(block);
  }
}

int BasicBlockSorter::dfsSearch(BasicBlock* block) {
  int block_index = map_get(scc_visited_, block, -1);
  if (block_index >= 0) {
    if (scc_in_stack_.count(block)) {
      return block_index;
    } else {
      return std::numeric_limits<int>::max();
    }
  }

  auto block_index_pair = scc_visited_.emplace(block, index_).first;
  int cur_index = index_++;

  scc_stack_.push_back(block);
  scc_in_stack_.insert(block);

  for (auto& succ : block->successors()) {
    if (!basic_blocks_.count(succ) || succ == entry_) {
      continue;
    }
    int min_index = dfsSearch(succ);
    block_index_pair->second = std::min(block_index_pair->second, min_index);
  }

  if (cur_index == block_index_pair->second) {
    auto sccblock = std::make_unique<SCCBasicBlocks>();
    BasicBlock* bb = nullptr;
    do {
      bb = scc_stack_.back();
      scc_stack_.pop_back();
      scc_in_stack_.erase(bb);

      sccblock->basic_blocks.insert(bb);
      block_to_scc_map_.emplace(bb, sccblock.get());
    } while (block != bb);

    JIT_DCHECK(
        !sccblock->basic_blocks.empty(), "Should not create an empty SCC.");
    scc_blocks_.push_back(std::move(sccblock));
  }

  return block_index_pair->second;
}

void BasicBlockSorter::calcEntryBlocks() {
  for (auto block : basic_blocks_) {
    auto cur_scc = map_get(block_to_scc_map_, block);
    for (auto succ : block->successors()) {
      if (!basic_blocks_.count(succ) || succ == entry_) {
        continue;
      }

      auto succ_scc = map_get(block_to_scc_map_, succ);
      if (cur_scc == succ_scc) {
        continue;
      }

      JIT_CHECK(
          succ_scc->entry == nullptr || succ_scc->entry == succ,
          "Irreducible CFG.");
      succ_scc->entry = succ;

      // One successor can be added multiple times here, which should not matter
      // because in sortPRO() function, every block is guarenteed to be visited
      // only once, and the duplicated successors will be ignored.
      // Note that we could use an unordered_map instead of a vector
      // for cur_scc->successors, but in that case, the sorted result will not
      // be stable because the order that successors are traversed is not
      // fixed in an unordered_map.
      cur_scc->successors.push_back(succ_scc);
    }
  }
}

void BasicBlockSorter::sortRPO() {
  if (scc_blocks_.empty()) {
    return;
  }

  auto sccblocks = std::move(scc_blocks_);
  scc_blocks_.clear();

  // maps a SCC to its index in sccblocks
  UnorderedMap<SCCBasicBlocks*, size_t> block_index_map;

  for (size_t i = 0; i < sccblocks.size(); i++) {
    block_index_map.emplace(sccblocks.at(i).get(), i);
  }

  auto entry = map_get(block_to_scc_map_, entry_);

  UnorderedSet<SCCBasicBlocks*> visited_blocks;
  std::stack<std::pair<std::unique_ptr<SCCBasicBlocks>, size_t>> stack;

  // If we encounter the exit block in the traversal below, it's stashed here
  // and appended to the end of the result, rather than inserted where it would
  // naturally fall. This is still a valid reverse postorder sort, since we
  // verify that it has no successors.
  std::unique_ptr<SCCBasicBlocks> exit_scc;

  visited_blocks.insert(entry);
  auto entry_index = block_index_map.at(entry);
  stack.emplace(std::move(sccblocks.at(entry_index)), 0);

  while (!stack.empty()) {
    auto& top = stack.top();
    auto& bb = top.first;
    auto& bb_succ = bb->successors;
    auto& next_succ_index = top.second;

    if (next_succ_index == bb_succ.size()) {
      scc_blocks_.emplace_back(std::move(bb));
      stack.pop();
      continue;
    }

    auto next_succ = bb_succ.at(next_succ_index++);
    if (visited_blocks.insert(next_succ).second) {
      auto index = map_get(block_index_map, next_succ);
      auto& succ_bb = sccblocks.at(index);

      if (succ_bb->entry == exit_) {
        JIT_CHECK(
            succ_bb->basic_blocks.size() == 1,
            "Exit SCC should have a single block");
        JIT_CHECK(
            succ_bb->successors.empty(),
            "Exit block should have no successors");
        exit_scc = std::move(succ_bb);
        continue;
      }
      stack.emplace(std::move(succ_bb), 0);
    }
  }

  std::reverse(scc_blocks_.begin(), scc_blocks_.end());
  if (exit_scc != nullptr) {
    scc_blocks_.emplace_back(std::move(exit_scc));
  }
}

} // namespace jit::lir
