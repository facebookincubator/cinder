// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/inliner.h"

#include "Jit/containers.h"
#include "Jit/lir/c_helper_translations.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/parser.h"

#include <shared_mutex>
#include <string_view>

using namespace jit::codegen;

namespace jit {

int g_disable_lir_inliner = 0;

namespace lir {

bool LIRInliner::inlineCalls(Function* func) {
  bool changed = false;
  std::vector<BasicBlock*>& blocks = func->basicblocks();

  for (size_t i = 0; i < blocks.size(); ++i) {
    BasicBlock* bb = blocks[i];

    for (auto& instr : bb->instructions()) {
      if (instr->isCall()) {
        LIRInliner inliner(instr.get());
        if (inliner.inlineCall()) {
          changed = true;
          // This block has been split,
          // so there's nothing left to process in it.
          break;
        }
      }
    }
  }

  return changed;
}

bool LIRInliner::inlineCall() {
  // Try to find function.
  Function* callee = findFunction();
  if (callee == nullptr) {
    // If function is not found, we cannot inline.
    return false;
  }

  if (!isInlineable(callee)) {
    JIT_DLOG("Found the callee, but cannot inline.");
    return false;
  }

  // Split basic blocks of caller.
  BasicBlock* block1 = call_instr_->basicblock();
  BasicBlock* block2 = block1->splitBefore(call_instr_);

  // Copy callee into caller.
  Function* caller = call_instr_->basicblock()->function();
  Function::CopyResult callee_bounds =
      caller->copyFrom(callee, block1, block2, call_instr_->origin());
  callee_start_ = callee_bounds.begin_bb;
  callee_end_ = callee_bounds.end_bb;

  resolveArguments();

  resolveReturnValue();
  JIT_DLOG("inlined function");
  return true;
}

bool LIRInliner::isInlineable(const Function* callee) {
  if (!checkEntryExitReturn(callee)) {
    return false;
  }
  if (!checkArguments()) {
    return false;
  }
  if (!checkLoadArg(callee)) {
    return false;
  }
  return true;
}

bool LIRInliner::checkEntryExitReturn(const Function* callee) {
  if (callee->basicblocks().empty()) {
    JIT_DLOG("Callee has no basic block.");
    return false;
  }
  const BasicBlock* entry_block = callee->getEntryBlock();
  if (!entry_block->predecessors().empty()) {
    JIT_DLOG("Expect entry block to have no predecessors.");
    return false;
  }
  BasicBlock* exit_block = callee->basicblocks().back();
  if (!exit_block->successors().empty()) {
    JIT_DLOG("Expect exit block to have no successors.");
    return false;
  }
  for (BasicBlock* bb : callee->basicblocks()) {
    if (bb->predecessors().empty() && bb != entry_block) {
      JIT_DLOG("Expect callee to have only 1 entry block.");
      return false;
    }
    if (bb->successors().empty() && bb != exit_block) {
      JIT_DLOG("Expect callee to have only 1 exit block.");
      return false;
    }
    for (auto& instr : bb->instructions()) {
      if (instr->isReturn()) {
        if (instr.get() != bb->getLastInstr() || bb->successors().size() != 1 ||
            bb->successors()[0] != exit_block) {
          JIT_DLOG(
              "Expect return to be last instruction of the predecessor of the "
              "exit block.");
          // Expect return to be the last instruction in the block.
          // Expect the successor to be the exit block.
          return false;
        }
      }
    }
  }
  if (!exit_block->instructions().empty()) {
    JIT_DLOG("Expect exit block to have no instructions.");
    return false;
  }
  return true;
}

bool LIRInliner::checkArguments() {
  size_t numInputs = call_instr_->getNumInputs();
  for (size_t i = 1; i < numInputs; ++i) {
    auto input = call_instr_->getInput(i);
    if (!input->isImm() && !input->isVreg()) {
      return false;
    }
    arguments_.emplace_back(input);
  }
  return true;
}

bool LIRInliner::checkLoadArg(const Function* callee) {
  // Subtract by 1 since first argument is callee address.
  size_t numInputs = call_instr_->getNumInputs() - 1;
  // Use check_load_arg to track if we are still in LoadArg instructions.
  bool check_load_arg = true;
  for (auto bb : callee->basicblocks()) {
    for (auto& instr : bb->instructions()) {
      if (check_load_arg) {
        if (instr->isLoadArg()) {
          if (instr->getNumInputs() < 1) {
            return false;
          }
          auto input = instr->getInput(0);
          if (!input->isImm()) {
            return false;
          }
          if (input->getConstant() >= numInputs) {
            return false;
          }
        } else {
          // No longer LoadArg instructions.
          check_load_arg = false;
        }
      } else {
        if (instr->isLoadArg()) {
          // kLoadArg instructions should only be at the
          // beginning of the callee.
          return false;
        }
      }
    }
  }
  return true;
}

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

  return parseFunction(addr);
}

lir::Function* LIRInliner::parseFunction(uint64_t addr) {
  // addr_to_function maps function address to parsed function
  static UnorderedMap<uint64_t, std::unique_ptr<Function>> addr_to_function;
  static std::shared_mutex addr_map_guard;

  {
    // Guard usage of addr_to_function
    std::shared_lock guard{addr_map_guard};

    // Check if function has already been parsed.
    auto iter = addr_to_function.find(addr);
    if (iter != addr_to_function.end()) {
      return iter->second.get();
    }
  }

  // Using function addr, try to get LIR text from kCHelperMapping.
  auto lir_text_iter = kCHelperMapping.find(addr);
  if (lir_text_iter == kCHelperMapping.end()) {
    // Guard usage of addr_to_function
    std::unique_lock guard{addr_map_guard};
    // Add nullptr to map in case same addr is used again.
    addr_to_function.emplace(addr, nullptr);
    return nullptr; // No LIR text for that address.
  }

  Parser parser;
  std::unique_ptr<Function> parsed_func;
  try {
    parsed_func = parser.parse(lir_text_iter->second);
  } catch (const ParserException&) {
    // Guard usage of addr_to_function
    std::unique_lock guard{addr_map_guard};
    // Add nullptr to map in case same addr is used again.
    addr_to_function.emplace(addr, nullptr);
    return nullptr;
  }

  // Guard usage of addr_to_function
  std::unique_lock guard{addr_map_guard};
  // Add function to map.
  addr_to_function.emplace(addr, std::move(parsed_func));
  // Return parsed function.
  return map_get_strict(addr_to_function, addr).get();
}

bool LIRInliner::resolveArguments() {
  // Remove load arg instructions and update virtual registers.
  UnorderedMap<OperandBase*, LinkedOperand*> vreg_map;
  auto caller_blocks = &call_instr_->basicblock()->function()->basicblocks();
  for (int i = callee_start_; i < callee_end_; i++) {
    auto bb = caller_blocks->at(i);
    auto it = bb->instructions().begin();
    // Use while loop since instructions may be removed.
    while (it != bb->instructions().end()) {
      if ((*it)->isLoadArg()) {
        resolveLoadArg(vreg_map, bb, it);
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
    UnorderedMap<OperandBase*, LinkedOperand*>& vreg_map,
    BasicBlock* bb,
    BasicBlock::InstrList::iterator& instr_it) {
  auto instr = instr_it->get();
  JIT_DCHECK(
      instr->getNumInputs() > 0 && instr->getInput(0)->isImm(),
      "LoadArg instruction should have at least 1 input.");

  // Get the corresponding parameter from the call instruction.
  auto argument = instr->getInput(0);
  auto param = arguments_.at(argument->getConstant());

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
    UnorderedMap<OperandBase*, LinkedOperand*>& vreg_map,
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

} // namespace lir
} // namespace jit
