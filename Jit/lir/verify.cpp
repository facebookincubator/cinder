// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "Jit/lir/dce.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/printer.h"
#include "Jit/util.h"

namespace jit::lir {

bool verifyPostRegAllocInvariants(Function* func, std::ostream& err) {
  auto& blocks = func->basicblocks();
  for (auto iter = blocks.begin(); iter != blocks.end();) {
    auto& block = *iter;
    ++iter;
    auto& succs = block->successors();
    BasicBlock* next_block = iter == blocks.end() ? nullptr : *iter;
    std::unordered_set<BasicBlock*> branched_blocks;
    for (auto& instr : block->instructions()) {
      if (instr->isBranch() || instr->isBranchCC()) {
        JIT_DCHECK(
            instr->getNumInputs() == 1, "Branch must have a single input.");
        auto operand = instr->getInput(0);
        JIT_DCHECK(
            operand->type() == OperandBase::kLabel,
            "Branch must jump to a label.");
        branched_blocks.insert(operand->getBasicBlock());
      }
    }

    for (const auto& succ : succs) {
      // Go through the instructions and ensure that each successor has a
      // matching jump.
      if (succ == next_block && next_block->section() == block->section()) {
        // If a successor is physically the next block in the block order and
        // the blocks are emitted to the same section, we don't need a branch.
        continue;
      }
      // Ensure that a jump to the successor exists.
      if (!branched_blocks.count(succ)) {
        fmt::print(
            err,
            "ERROR: Basic block {} does not contain a jump to non-immediate "
            "successor {}.\n",
            block->id(),
            succ->id());
        return false;
      }
    }
  }
  return true;
}

} // namespace jit::lir
