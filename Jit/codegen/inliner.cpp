// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/codegen/inliner.h"
#include <dlfcn.h>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include "Jit/lir/lir.h"
#include "Jit/lir/parser.h"

using namespace jit::lir;

namespace jit {
namespace codegen {

lir::Function* LIRInliner::findFunction() {
  // Get the address.
  if (call_instr_->getNumInputs() < 1) {
    return nullptr;
  }
  OperandBase* dest_operand = call_instr_->getInput(0);
  if (!dest_operand->isImm()) {
    return nullptr;
  }
  uint64_t addr = dest_operand->getConstant();

  // Resolve the addres to a name.
  Dl_info helper_info;
  if (dladdr(reinterpret_cast<void*>(addr), &helper_info) == 0 ||
      helper_info.dli_sname == NULL) {
    return nullptr;
  }
  const std::string& name = helper_info.dli_sname;
  return parseFunction(name);
}

lir::Function* LIRInliner::parseFunction(const std::string& name) {
  static std::unordered_map<std::string, std::unique_ptr<Function>>
      name_to_function;

  // Check if function is in map, return function if in map.
  auto iter = name_to_function.find(name);
  if (iter != name_to_function.end()) {
    return iter->second.get();
  }

  // Using function name, try to open and parse text file with function.
  std::ifstream lir_text(
      fmt::format("Jit/lir/c_helper_translations/{}.lir", name));
  if (!lir_text.good()) {
    return nullptr;
  }
  std::stringstream buffer;
  buffer << lir_text.rdbuf();
  Parser parser;
  std::unique_ptr<Function> parsed_func = parser.parse(buffer.str());
  // Add function to map.
  name_to_function.emplace(name, std::move(parsed_func));
  // Return parsed function.
  return map_get_strict(name_to_function, name).get();
}

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

void LIRInliner::resolveReturnValue() {
  auto epilogue =
      call_instr_->basicblock()->function()->basicblocks().at(callee_end_ - 1);

  // Create phi instruction.
  auto phi_instr =
      epilogue->allocateInstr(Instruction::kPhi, nullptr, OutVReg());

  // Find return instructions from predecessor of epilogue.
  for (auto pred : epilogue->predecessors()) {
    auto lastInstr = pred->getLastInstr();
    if (lastInstr != nullptr && lastInstr->isReturn()) {
      phi_instr->allocateLabelInput(pred);
      JIT_CHECK(
          lastInstr->getNumInputs() > 0,
          "Return instruction should have at least 1 input operand.");
      phi_instr->appendInputOperand(lastInstr->releaseInputOperand(0));
      pred->removeInstr(pred->getLastInstrIter());
    }
  }

  if (phi_instr->getNumInputs() == 0) {
    // Callee has no return statements.
    // Remove phi instruction.
    epilogue->removeInstr(epilogue->getLastInstrIter());
    call_instr_->setOpcode(Instruction::kNop);
  } else {
    call_instr_->setOpcode(Instruction::kMove);
    // Remove all inputs.
    while (call_instr_->getNumInputs() > 0) {
      call_instr_->removeInputOperand(0);
    }
    call_instr_->allocateLinkedInput(phi_instr);
  }
}

} // namespace codegen
} // namespace jit
