#ifndef __LIR_BLOCKSORTER_H__
#define __LIR_BLOCKSORTER_H__

#include "Jit/lir/block.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace jit {
namespace lir {

// this struct represents a group of basic blocks that
// are strongly conncted to each other.
struct SCCBasicBlocks {
  // only one entry basic block. Otherwise, the CFG
  // is irreducible, which we don't support.
  BasicBlock* entry{nullptr};
  std::unordered_set<BasicBlock*> basic_blocks;
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
  BasicBlockSorter(
      const std::unordered_set<BasicBlock*>& blocks,
      BasicBlock* entry);

  BasicBlock* entry_{nullptr};
  BasicBlock* exit_{nullptr};
  std::unordered_set<BasicBlock*> basic_blocks_;

  std::vector<BasicBlock*> scc_stack_;
  std::unordered_set<BasicBlock*> scc_in_stack_;
  std::unordered_map<BasicBlock*, int> scc_visited_;
  int index_;

  std::unordered_map<BasicBlock*, SCCBasicBlocks*> block_to_scc_map_;
  std::vector<std::unique_ptr<SCCBasicBlocks>> scc_blocks_;
  void calculateSCC();
  int dfsSearch(BasicBlock* block);

  void calcEntryBlocks();
  void sortRPO();
};

} // namespace lir
} // namespace jit

#endif
