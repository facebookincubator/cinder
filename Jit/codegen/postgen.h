#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/codegen/rewrite.h"

namespace jit::codegen {
// Rewrites after LIR generation
class PostGenerationRewrite : public Rewrite {
 public:
  PostGenerationRewrite(lir::Function* func, Environ* env)
      : Rewrite(func, env) {
    registerOneRewriteFunction(rewriteBinaryOpConstantPosition);
    registerOneRewriteFunction(rewriteBinaryOpLargeConstant);
    registerOneRewriteFunction(rewriteCondBranch);
  }

 private:
  // Fix constant input position
  // If a binary operation has a constant input, always put it as the
  // second operand (or move the 2nd to a register for div instructions)
  static RewriteResult rewriteBinaryOpConstantPosition(instr_iter_t instr_iter);

  // Rewrite binary instructions with > 32-bit constant.
  static RewriteResult rewriteBinaryOpLargeConstant(instr_iter_t instr_iter);

  // Rewrite CondBranch instruction so that in some cases, we don't have
  // to allocate a register for it.
  static RewriteResult rewriteCondBranch(instr_iter_t instr_iter);
};
} // namespace jit::codegen
