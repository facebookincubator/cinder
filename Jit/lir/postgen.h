// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/lir/rewrite.h"

using namespace jit::codegen;

namespace jit::lir {
// Rewrites after LIR generation
class PostGenerationRewrite : public Rewrite {
 public:
  PostGenerationRewrite(lir::Function* func, Environ* env)
      : Rewrite(func, env) {
    // rewriteInlineHelper should occur before other rewrites.
    registerOneRewriteFunction(rewriteInlineHelper, 0);
    registerOneRewriteFunction(rewriteBatchDecrefInstrs, 0);

    registerOneRewriteFunction(rewriteBinaryOpConstantPosition, 1);
    registerOneRewriteFunction(rewriteBinaryOpLargeConstant, 1);
    registerOneRewriteFunction(rewriteGuardLargeConstant, 1);
    registerOneRewriteFunction(rewriteLoadArg, 1);
    registerOneRewriteFunction(rewriteMoveToMemoryLargeConstant, 1);
  }

 private:
  // Inline C helper functions.
  static RewriteResult rewriteInlineHelper(function_rewrite_arg_t func);

  // Fix constant input position
  // If a binary operation has a constant input, always put it as the
  // second operand (or move the 2nd to a register for div instructions)
  static RewriteResult rewriteBinaryOpConstantPosition(instr_iter_t instr_iter);

  // Rewrite binary instructions with > 32-bit constant.
  static RewriteResult rewriteBinaryOpLargeConstant(instr_iter_t instr_iter);

  // Rewrite Guard instructions with > 32-bit constant.
  static RewriteResult rewriteGuardLargeConstant(instr_iter_t instr_iter);

  // Rewrite storing a large immediate to a memory location
  static RewriteResult rewriteMoveToMemoryLargeConstant(
      instr_iter_t instr_iter);

  // Rewrite LoadArg to Bind and allocate a physical register for its input.
  static RewriteResult rewriteLoadArg(instr_iter_t instr_iter, Environ* env);

  // rewrite BatchDecref instructions
  static RewriteResult rewriteBatchDecrefInstrs(instr_iter_t instr_iter);

  FRIEND_TEST(LIRRewriteTest, RewriteCondBranchTest);
};
} // namespace jit::lir
