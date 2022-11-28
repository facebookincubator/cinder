// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/block.h"
#include "Jit/lir/printer.h"

#include <iomanip>
#include <utility>

namespace jit::lir {

Instruction::Instruction(
    BasicBlock* basic_block,
    Opcode opcode,
    const hir::Instr* origin)
    : id_(basic_block->function()->allocateId()),
      opcode_(opcode),
      output_(this),
      basic_block_(basic_block),
      origin_(origin) {}

Instruction::Instruction(
    BasicBlock* bb,
    Instruction* instr,
    const hir::Instr* origin)
    : id_(bb->function()->allocateId()),
      opcode_(instr->opcode_),
      output_(this, &instr->output_),
      basic_block_(bb),
      origin_(origin) {}

Operand* Instruction::allocateImmediateInput(
    uint64_t n,
    OperandBase::DataType data_type) {
  auto operand =
      std::make_unique<Operand>(this, data_type, OperandBase::kImm, n);
  auto opnd = operand.get();
  inputs_.push_back(std::move(operand));

  return opnd;
}

Operand* Instruction::allocateFPImmediateInput(double n) {
  auto operand = std::make_unique<Operand>(this, OperandBase::kImm, n);
  auto opnd = operand.get();
  inputs_.push_back(std::move(operand));

  return opnd;
}

LinkedOperand* Instruction::allocateLinkedInput(Instruction* def_instr) {
  auto operand = std::make_unique<LinkedOperand>(this, def_instr);
  auto opnd = operand.get();
  inputs_.push_back(std::move(operand));
  return opnd;
}

bool Instruction::getOutputPhyRegUse() const {
  return InstrProperty::getProperties(opcode_).output_phy_use;
}

bool Instruction::getInputPhyRegUse(size_t i) const {
  // If the output of a move instruction is a memory location, then its input
  // needs to be a physical register. Otherwise we might generate a mem->mem
  // move, which we can't safely handle for all bit widths in codegen (since
  // push/pop aren't available for all bit widths).
  if (isMove() && output_.type() == OperandBase::kInd) {
    return true;
  }

  auto& uses = InstrProperty::getProperties(opcode_).input_phy_uses;
  if (i >= uses.size()) {
    return false;
  }

  return uses.at(i);
}

bool Instruction::inputsLiveAcross() const {
  return InstrProperty::getProperties(opcode_).inputs_live_across;
}

void Instruction::print() const {
  std::cerr << *this << std::endl;
}

InstrProperty::InstrInfo& InstrProperty::getProperties(
    Instruction::Opcode opcode) {
  return prop_map_.at(opcode);
}

#define BEGIN_INSTR_PROPERTY \
  std::vector<InstrProperty::InstrInfo> InstrProperty::prop_map_ = {
#define END_INSTR_PROPERTY \
  }                        \
  ;

#define PROPERTY(__t, __p...) {#__t, __p},

// clang-format off
// This table contains definitions of all the properties for each instruction type.
BEGIN_INSTR_PROPERTY
  FOREACH_INSTR_TYPE(PROPERTY)
END_INSTR_PROPERTY
// clang-format on

} // namespace jit::lir
