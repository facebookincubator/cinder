// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/lir/block.h"
#include "Jit/lir/lir.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace jit::lir {

// this class implements a framework for backend rewrites
class Rewrite {
 public:
  Rewrite(jit::lir::Function* func, jit::codegen::Environ* env)
      : function_(func), env_(env) {}

  jit::lir::Function* function() {
    return function_;
  }
  const jit::lir::Function* function() const {
    return function_;
  }

  jit::codegen::Environ* environment() {
    return env_;
  }

  const jit::codegen::Environ* environment() const {
    return env_;
  }

  // Rewrite routine types:
  //  - kFunction: rewrite the whole function
  //  - kBasicBlock: rewrite one of the basic blocks
  //  - kInstruction: rewrite one or more instructions
  enum RewriteType { kFunction, kBasicBlock, kInstruction };

  enum RewriteResult {
    kUnchanged, // the element to be rewritten has not been changed
    kChanged, // the element to be rewritten has been changed
    kRemoved // the element to be rewritten has been removed
  };

  using function_rewrite_arg_t = lir::Function*;
  using basic_block_rewrite_arg_t = lir::BasicBlock*;
  using instr_iter_t = jit::lir::BasicBlock::InstrList::iterator;
  using instruction_rewrite_arg_t = instr_iter_t;

  template <typename T>
  using function_type_t = std::function<RewriteResult(T)>;
  using function_rewrite_t = function_type_t<function_rewrite_arg_t>;
  using basic_block_rewrite_t = function_type_t<basic_block_rewrite_arg_t>;
  using instruction_rewrite_t = function_type_t<instruction_rewrite_arg_t>;

  template <typename T>
  void registerOneRewriteFunction(RewriteResult (*rewrite)(T), int stage = 0) {
    registerOneRewriteFunction(std::function{rewrite}, stage);
  }

  template <typename T>
  void registerOneRewriteFunction(
      RewriteResult (*rewrite)(T, jit::codegen::Environ*),
      int stage = 0) {
    registerOneRewriteFunction(
        function_type_t<T>(
            std::bind(rewrite, std::placeholders::_1, environment())),
        stage);
  }

  template <typename T>
  void registerOneRewriteFunction(
      const std::function<RewriteResult(T)>& rewrite,
      int stage = 0) {
    if constexpr (std::is_same_v<T, function_rewrite_arg_t>) {
      function_rewrites_[stage].push_back(rewrite);
    } else if constexpr (std::is_same_v<T, basic_block_rewrite_arg_t>) {
      basic_block_rewrites_[stage].push_back(rewrite);
    } else if constexpr (std::is_same_v<T, instruction_rewrite_arg_t>) {
      instruction_rewrites_[stage].push_back(rewrite);
    } else {
      static_assert(!sizeof(T*), "Bad rewrite function type.");
    }
  }

  void run();

 protected:
  // find the most recent instruction affecting flags within the
  // basic block. returns nullptr if not found.
  static jit::lir::Instruction* findRecentFlagAffectingInstr(
      instr_iter_t instr_iter);

 private:
  template <typename T>
  std::pair<bool, const T*> getStageRewrites(
      const std::unordered_map<int, T>& rewrites,
      int stage) {
    auto iter = rewrites.find(stage);
    if (iter == rewrites.end()) {
      return std::make_pair(false, nullptr);
    }

    return std::make_pair(true, &(iter->second));
  }

  void runOneStage(int stage);

  // Keeps doing one type of rewrites until the fixed point is reached.
  // Returns true if the original function has been changed by the rewrites,
  // indicating that all the rewrites have to be run again.
  // Returns false if nothing has been changed in the original function.
  template <typename T, typename V>
  std::enable_if_t<
      std::is_same_v<T, function_rewrite_t> ||
          std::is_same_v<T, basic_block_rewrite_t> ||
          std::is_same_v<T, instruction_rewrite_t>,
      bool>
  runOneTypeRewrites(const std::vector<T>& rewrites, V&& arg) {
    bool changed = false;
    bool loop_changed = false;

    do {
      loop_changed = false;
      for (auto& rewrite : rewrites) {
        auto r = rewrite(std::forward<V>(arg));
        loop_changed |= (r != kUnchanged);

        if (r == kRemoved) {
          return true;
        }
      }

      changed |= loop_changed;
    } while (loop_changed);

    return changed;
  }

  jit::lir::Function* function_;
  jit::codegen::Environ* env_;

  std::unordered_map<int, std::vector<function_type_t<function_rewrite_arg_t>>>
      function_rewrites_;
  std::unordered_map<
      int,
      std::vector<function_type_t<basic_block_rewrite_arg_t>>>
      basic_block_rewrites_;
  std::unordered_map<
      int,
      std::vector<function_type_t<instruction_rewrite_arg_t>>>
      instruction_rewrites_;
};

} // namespace jit::lir
