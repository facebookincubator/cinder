// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/codegen/rewrite.h"

namespace jit::codegen {
// Rewrites after LIR generation
class PostGenerationRewrite : public Rewrite {
 public:
  PostGenerationRewrite(lir::Function* func, Environ* env)
      : Rewrite(func, env) {
    // rewriteInlineHelper should occur before other rewrites.
    registerOneRewriteFunction(rewriteInlineHelper, 0);
    registerOneRewriteFunction(rewriteBinaryOpConstantPosition, 1);
    registerOneRewriteFunction(rewriteBinaryOpLargeConstant, 1);
    registerOneRewriteFunction(rewriteCondBranch, 1);
    registerOneRewriteFunction(rewriteLoadArg, 1);
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

  // Rewrite CondBranch instruction so that in some cases, we don't have
  // to allocate a register for it.
  static RewriteResult rewriteCondBranch(instr_iter_t instr_iter);

  // Rewrite LoadArg to Bind and allocate a physical register for its input.
  static RewriteResult rewriteLoadArg(instr_iter_t instr_iter, Environ* env);
};
} // namespace jit::codegen
