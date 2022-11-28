// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "Jit/lir/dce.h"

#include "Jit/containers.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/operand.h"
#include "Jit/util.h"

namespace jit::lir {

// For the purposes of dead code elimination, we consider writes to physical
// registers as affecting memory.
static inline bool operandAffectsMemory(const OperandBase* operand) {
  return (
      operand->isReg() || operand->isStack() || operand->isMem() ||
      operand->isInd());
}

// This function determines whether an instruction should be part of the root
// live set - that is, whether it contains control flow or memory effects which
// mean that we should unconditionally keep this instruction.
static bool isUseful(const Instruction* instruction) {
  const InstrProperty::InstrInfo& properties =
      InstrProperty::getProperties(instruction);
  JIT_CHECK(
      instruction->output() != nullptr || properties.is_essential,
      "Any instruction without an output must be marked as essential.");
  return (
      instruction->isAnyBranch() || instruction->isTerminator() ||
      properties.flag_effects != FlagEffects::kNone ||
      properties.is_essential || operandAffectsMemory(instruction->output()));
}

void eliminateDeadCode(Function* function) {
  Worklist<Instruction*> worklist;
  UnorderedSet<Instruction*> live_set;
  auto mark_live = [&](Instruction* instruction) {
    if (live_set.insert(instruction).second) {
      worklist.push(instruction);
    }
  };
  for (auto& block : function->basicblocks()) {
    for (auto& instruction : block->instructions()) {
      if (isUseful(instruction.get())) {
        mark_live(instruction.get());
      }
    }
  }
  auto add_linked_instruction_to_worklist = [&](OperandBase* operand) {
    if (operand == nullptr || !operand->isLinked()) {
      return;
    }
    Instruction* linked_instruction =
        static_cast<LinkedOperand*>(operand)->getLinkedInstr();
    mark_live(linked_instruction);
  };
  auto add_all_operand_registers_to_worklist = [&](OperandBase* operand) {
    if (operand->isInd()) {
      MemoryIndirect* indirect = operand->getMemoryIndirect();
      add_linked_instruction_to_worklist(indirect->getBaseRegOperand());
      add_linked_instruction_to_worklist(indirect->getIndexRegOperand());
    } else {
      add_linked_instruction_to_worklist(operand);
    };
  };
  // Compute the live set by adding all operands referenced by already live
  // elements.
  while (!worklist.empty()) {
    auto live_op = worklist.front();
    worklist.pop();
    live_op->foreachInputOperand(add_all_operand_registers_to_worklist);
    // We also need to ensure that any registers referenced by the output are
    // visited.
    if (live_op->output() != nullptr) {
      add_all_operand_registers_to_worklist(live_op->output());
    }
  }
  // Filter anything not in the live set out.
  for (auto& block : function->basicblocks()) {
    for (auto instruction_iterator = block->instructions().begin();
         instruction_iterator != block->instructions().end();) {
      // The LIR APIs for removing instructions takes an iterator. We keep a
      // reference to the iterator before incrementing to ensure we can remove
      // instructions as needed and keep looping.
      auto iterator_to_remove = instruction_iterator;
      ++instruction_iterator;

      if (live_set.count(iterator_to_remove->get()) == 0) {
        block->removeInstr(iterator_to_remove);
      }
    }
  }
}

} // namespace jit::lir
