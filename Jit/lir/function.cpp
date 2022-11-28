// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/function.h"

#include "Jit/containers.h"
#include "Jit/lir/blocksorter.h"
#include "Jit/lir/printer.h"

#include <stack>

namespace jit::lir {

// Helper for copyOperand.
static void copyIndirect(
    UnorderedMap<LinkedOperand*, int>& instr_refs,
    Operand* dest_op,
    MemoryIndirect* source_op) {
  auto base = source_op->getBaseRegOperand();
  auto index = source_op->getIndexRegOperand();
  std::variant<Instruction*, PhyLocation> dest_base;
  std::variant<Instruction*, PhyLocation> dest_index;
  if (base->isLinked()) {
    dest_base = dest_op->instr();
  } else {
    // Otherwise, it must be physical register.
    dest_base = base->getPhyRegister();
  }
  if (index != nullptr) {
    if (index->isLinked()) {
      dest_index = dest_op->instr();
    } else {
      // Otherwise, it must be physical register.
      dest_index = index->getPhyRegister();
    }
  }

  dest_op->setMemoryIndirect(
      dest_base,
      dest_index,
      source_op->getMultipiler(),
      source_op->getOffset());

  // add linked operands to instr_refs
  auto memInd = dest_op->getMemoryIndirect();
  if (base->isLinked()) {
    auto base_linked = static_cast<const LinkedOperand*>(base);
    auto base_linked_id = base_linked->getLinkedOperand()->instr()->id();
    instr_refs.emplace(
        static_cast<LinkedOperand*>(memInd->getBaseRegOperand()),
        base_linked_id);
  }

  if (index != nullptr && index->isLinked()) {
    auto index_linked = static_cast<const LinkedOperand*>(index);
    auto index_linked_id = index_linked->getLinkedOperand()->instr()->id();
    instr_refs.emplace(
        static_cast<LinkedOperand*>(memInd->getIndexRegOperand()),
        index_linked_id);
  }
}

// Helper for copyOperandBase.
// Assume that type and data type are already be set.
static void copyOperand(
    UnorderedMap<int, BasicBlock*>& block_index_map,
    UnorderedMap<LinkedOperand*, int>& instr_refs,
    Operand* operand,
    Operand* operand_copy) {
  switch (operand->type()) {
    case OperandBase::kReg: {
      operand_copy->setPhyRegister(operand->getPhyRegister());
      break;
    }
    case OperandBase::kStack: {
      operand_copy->setStackSlot(operand->getStackSlot());
      break;
    }
    case OperandBase::kMem: {
      operand_copy->setMemoryAddress(operand->getMemoryAddress());
      break;
    }
    case OperandBase::kImm: {
      operand_copy->setConstant(operand->getConstant());
      break;
    }
    case OperandBase::kLabel: {
      operand_copy->setBasicBlock(
          map_get_strict(block_index_map, operand->getBasicBlock()->id()));
      break;
    }
    case OperandBase::kInd: {
      copyIndirect(instr_refs, operand_copy, operand->getMemoryIndirect());
      break;
    }
    case OperandBase::kNone:
    case OperandBase::kVreg:
      // operand_copy should already be type kVreg.
      break;
  }
}

// Helper for deepCopyBasicBlocks.
static void copyInput(
    UnorderedMap<int, BasicBlock*>& block_index_map,
    UnorderedMap<LinkedOperand*, int>& instr_refs,
    OperandBase* input,
    Instruction* instr_copy) {
  if (input->isLinked()) {
    LinkedOperand* linked_opnd = instr_copy->allocateLinkedInput(nullptr);
    instr_refs.emplace(
        linked_opnd,
        static_cast<LinkedOperand*>(input)->getDefine()->instr()->id());
  } else {
    // Allocate temporary input and set value_ using copyOperand.
    Operand* input_copy = instr_copy->allocateImmediateInput(0);
    copyOperand(
        block_index_map, instr_refs, static_cast<Operand*>(input), input_copy);
    input_copy->setDataType(input->dataType());
  }
}

// Helper for deepCopyBasicBlocks.
static void connectLinkedOperands(
    UnorderedMap<int, Instruction*>& output_index_map_,
    UnorderedMap<LinkedOperand*, int>& instr_refs_) {
  for (auto& [operand, instr_index] : instr_refs_) {
    auto instr = map_get_strict(output_index_map_, instr_index);
    instr->output()->addUse(operand);
  }
}

// Helper used in copyFrom.
// Expects blocks to be initialized into block_index_map_.
// Copies the instructions and successors from src_blocks.
static void deepCopyBasicBlocks(
    const std::vector<BasicBlock*>& src_blocks,
    UnorderedMap<int, BasicBlock*>& block_index_map_,
    const hir::Instr* origin) {
  UnorderedMap<int, Instruction*> output_index_map;
  UnorderedMap<LinkedOperand*, int> instr_refs;

  for (auto bb : src_blocks) {
    BasicBlock* bb_copy = map_get_strict(block_index_map_, bb->id());
    for (auto succ : bb->successors()) {
      bb_copy->addSuccessor(map_get_strict(block_index_map_, succ->id()));
    }
    for (auto& instr : bb->instructions()) {
      // Copying the instruction will also copy the output
      // (including the output type and data type).
      bb_copy->instructions().emplace_back(
          std::make_unique<Instruction>(bb_copy, instr.get(), origin));
      Instruction* instr_copy = bb_copy->instructions().back().get();
      output_index_map.emplace(instr->id(), instr_copy);
      // Copy output.
      Operand* output = instr->output();
      Operand* output_copy = instr_copy->output();
      copyOperand(block_index_map_, instr_refs, output, output_copy);
      // Copy inputs.
      for (size_t i = 0, n = instr->getNumInputs(); i < n; ++i) {
        OperandBase* input = instr->getInput(i);
        copyInput(block_index_map_, instr_refs, input, instr_copy);
      }
    }
  }

  connectLinkedOperands(output_index_map, instr_refs);
}

Function::CopyResult Function::copyFrom(
    const Function* src_func,
    BasicBlock* prev_bb,
    BasicBlock* next_bb,
    const hir::Instr* origin) {
  JIT_CHECK(
      prev_bb->successors().size() == 1 && prev_bb->successors()[0] == next_bb,
      "prev_bb should only have 1 successor which should be next_bb.");

  UnorderedMap<int, BasicBlock*> block_index_map;

  // Initialize the basic blocks.
  for (auto bb : src_func->basicblocks()) {
    BasicBlock* bb_copy = &basic_block_store_.emplace_back(this);
    block_index_map.emplace(bb->id(), bb_copy);
    // Insert basic block before the last block.
    basic_blocks_.emplace(std::prev(basic_blocks_.end()), bb_copy);
  }

  deepCopyBasicBlocks(src_func->basicblocks(), block_index_map, origin);

  int end = basic_blocks_.size() - 1;
  int start = end - src_func->basic_blocks_.size();
  BasicBlock* dest_start = basic_blocks_.at(start);
  BasicBlock* dest_end = basic_blocks_.at(end - 1);
  prev_bb->setSuccessor(0, dest_start);
  JIT_CHECK(
      dest_end->successors().empty(),
      "Last block of function should have no successors.");
  dest_end->addSuccessor(next_bb);

  return CopyResult{start, end};
}

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

} // namespace jit::lir
