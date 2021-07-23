// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/codegen/inliner.h"
#include <unordered_map>
#include "Jit/lir/lir.h"
#include "Jit/lir/operand.h"

using namespace jit::lir;

namespace jit {
namespace codegen {

bool LIRInliner::resolveArguments() {
  // Map index to arguments of call_instr_.
  std::vector<OperandBase*> argument_list;
  size_t num_inputs = call_instr_->getNumInputs();
  argument_list.reserve(num_inputs);
  for (size_t i = 1; i < num_inputs; ++i) {
    argument_list.emplace_back(call_instr_->getInput(i));
  }

  // Remove load arg instructions and update virtual registers.
  std::unordered_map<OperandBase*, LinkedOperand*> vreg_map;
  auto caller_blocks = &call_instr_->basicblock()->function()->basicblocks();
  for (int i = callee_start_; i < callee_end_; i++) {
    auto bb = caller_blocks->at(i);
    auto it = bb->instructions().begin();
    // Use while loop since instructions may be removed.
    while (it != bb->instructions().end()) {
      if ((*it)->isLoadArg()) {
        resolveLoadArg(argument_list, vreg_map, bb, it);
      } else {
        // When instruction is not kLoadArg,
        // fix any inputs are linked to output registers from kLoadArg.
        resolveLinkedArgumentsUses(vreg_map, it);
      }
    }
  }

  return true;
}

void LIRInliner::resolveLoadArg(
    std::vector<OperandBase*>& argument_list,
    std::unordered_map<OperandBase*, LinkedOperand*>& vreg_map,
    BasicBlock* bb,
    BasicBlock::InstrList::iterator& instr_it) {
  auto instr = instr_it->get();
  JIT_CHECK(
      instr->getNumInputs() > 0,
      "LoadArg instruction should have at least 1 input.");

  // Get the corresponding parameter from the call instruction.
  auto argument = instr->getInput(0);
  auto param = argument_list.at(argument->getConstant());

  // Based on the parameter type, resolve the kLoadArg.
  if (param->isImm()) {
    // For immediate values, change kLoadArg to kMove.
    instr->setOpcode(Instruction::kMove);
    auto param_copy =
        std::make_unique<Operand>(instr, static_cast<Operand*>(param));
    param_copy->setConstant(param->getConstant());
    instr->replaceInputOperand(0, std::move(param_copy));
    ++instr_it;
  } else {
    JIT_DCHECK(
        param->isLinked(), "Inlined arguments must be immediate or linked.");
    // Otherwise, output of kLoadArg should be a virtual register.
    // For virtual registers, delete kLoadArg and replace uses.
    vreg_map.emplace(instr->output(), static_cast<LinkedOperand*>(param));
    instr_it = bb->instructions().erase(instr_it);
  }
}

void LIRInliner::resolveLinkedArgumentsUses(
    std::unordered_map<OperandBase*, LinkedOperand*>& vreg_map,
    std::list<std::unique_ptr<Instruction>>::iterator& instr_it) {
  auto setLinkedOperand = [&](OperandBase* opnd) {
    auto new_def = map_get(vreg_map, opnd->getDefine(), nullptr);
    if (new_def != nullptr) {
      auto opnd_linked = static_cast<LinkedOperand*>(opnd);
      opnd_linked->setLinkedInstr(new_def->getLinkedOperand()->instr());
    }
  };
  auto instr = instr_it->get();
  for (size_t i = 0, n = instr->getNumInputs(); i < n; i++) {
    auto input = instr->getInput(i);
    if (input->isLinked()) {
      setLinkedOperand(input);
    } else if (input->isInd()) {
      // For indirect operands, check if base or index registers are linked.
      auto memInd = input->getMemoryIndirect();
      auto base = memInd->getBaseRegOperand();
      auto index = memInd->getIndexRegOperand();
      if (base->isLinked()) {
        setLinkedOperand(base);
      }
      if (index && index->isLinked()) {
        setLinkedOperand(index);
      }
    }
  }
  ++instr_it;
}

} // namespace codegen
} // namespace jit
