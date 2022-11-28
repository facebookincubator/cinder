// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/rewrite.h"

#include "Jit/codegen/x86_64.h"
#include "Jit/lir/block.h"
#include "Jit/lir/operand.h"
#include "Jit/log.h"
#include "Jit/util.h"

#include <set>
#include <unordered_set>

using namespace jit::codegen;

namespace jit::lir {

void Rewrite::run() {
  // collect all stages
  std::set<int> stages;

  for (auto& rewrite : function_rewrites_) {
    stages.insert(rewrite.first);
  }

  for (auto& rewrite : basic_block_rewrites_) {
    stages.insert(rewrite.first);
  }

  for (auto& rewrite : instruction_rewrites_) {
    stages.insert(rewrite.first);
  }

  for (int stage : stages) {
    runOneStage(stage);
  }
}

void Rewrite::runOneStage(int stage) {
  auto [has_function_rewrites, function_rewrites] =
      getStageRewrites(function_rewrites_, stage);
  auto [has_basic_block_rewrites, basic_block_rewrites] =
      getStageRewrites(basic_block_rewrites_, stage);
  auto [has_instruction_rewrites, instruction_rewrites] =
      getStageRewrites(instruction_rewrites_, stage);

  bool changed = false;
  do {
    changed = false;
    if (has_function_rewrites) {
      changed |= runOneTypeRewrites(*function_rewrites, function_);
    }

    if (has_basic_block_rewrites) {
      for (auto& bb : function_->basicblocks()) {
        changed |= runOneTypeRewrites(*basic_block_rewrites, bb);
      }
    }

    if (has_instruction_rewrites) {
      for (auto& bb : function_->basicblocks()) {
        auto& instrs = bb->instructions();

        auto iter = instrs.begin();
        for (iter = instrs.begin(); iter != instrs.end();) {
          auto cur_iter = iter++;

          changed |= runOneTypeRewrites(*instruction_rewrites, cur_iter);
        }
      }
    }
  } while (changed);
}

Instruction* Rewrite::findRecentFlagAffectingInstr(instr_iter_t instr_iter) {
  Instruction* condbranch = instr_iter->get();
  JIT_CHECK(
      condbranch->isCondBranch(), "Input must be a CondBranch instruction.");

  BasicBlock* block = condbranch->basicblock();
  while (instr_iter != block->instructions().begin()) {
    --instr_iter;
    Instruction* instr = instr_iter->get();
    switch (InstrProperty::getProperties(instr).flag_effects) {
      case FlagEffects::kInvalidate:
        return nullptr;
      case FlagEffects::kSet:
        return instr;
      case FlagEffects::kNone:
        continue;
    }
  }
  return nullptr;
}

} // namespace jit::lir
