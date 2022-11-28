// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/lir/block.h"
#include "Jit/lir/rewrite.h"

namespace jit::lir {

// Rewrites after register allocation
class PostRegAllocRewrite : public Rewrite {
 public:
  PostRegAllocRewrite(jit::lir::Function* func, jit::codegen::Environ* env)
      : Rewrite(func, env) {
    registerRewrites();
  }

 private:
  void registerRewrites();

  static RewriteResult removePhiInstructions(instr_iter_t instr_iter);

  // rewrite call instructions:
  //   - move function arguments to the right registers.
  //   - handle special cases such as JITRT_(Call|Invoke)Function,
  //   JITRT_(Call|Get)Method, etc.
  static RewriteResult rewriteCallInstrs(
      instr_iter_t instr_iter,
      jit::codegen::Environ* env);

  static int rewriteRegularFunction(instr_iter_t instr_iter);
  static int rewriteVectorCallFunctions(instr_iter_t instr_iter);
  static int rewriteBatchDecrefFunction(instr_iter_t instr_iter);

  // replaces ZEXT and SEXT with appropriate MOVE instructions
  static RewriteResult rewriteBitExtensionInstrs(instr_iter_t instr_iter);

  // rewrite move instructions
  // optimimize move instruction in the following cases:
  //   1. remove the move instruction when source and destination are the same
  //   2. rewrite move instruction to xor when the source operand is 0.
  static RewriteResult optimizeMoveInstrs(instr_iter_t instr_iter);

  // Add (conditional) branch instructions to the end of each basic blocks when
  // necessary.
  // TODO (tiansi): currently, condition to the conditional branches are always
  // comparing against 0, so they are translated directly into machine code,
  // and we don't need to take care of them here right now. But once we start
  // to support different conditions (as we already did in static compiler),
  // we need to also rewrite conditional branches into Jcc instructions.
  // I'll do this in one of the following a few diffs.
  static RewriteResult rewriteBranchInstrs(jit::lir::Function* func);

  // rewrite > 32-bit immediate addressing load
  static RewriteResult rewriteLoadInstrs(instr_iter_t instr_iter);

  // convert CondBranch and BranchCC instructions
  static RewriteResult rewriteCondBranch(jit::lir::Function* function);
  // convert CondBranch to Test and BranchCC instructions
  static void doRewriteCondBranch(
      instr_iter_t instr_iter,
      jit::lir::BasicBlock* next_block);
  // negate BranchCC instructions based on the next (fallthrough) basic block
  static void doRewriteBranchCC(
      instr_iter_t instr_iter,
      jit::lir::BasicBlock* next_block);

  // rewrite Binary Op instructions
  static RewriteResult rewriteBinaryOpInstrs(instr_iter_t instr_iter);

  // rewrite 8-bit multiply to use single-operand imul
  static RewriteResult rewriteByteMultiply(instr_iter_t instr_iter);

  // replace memory input with register when possible within a basic block
  // and remove the unnecessary moves after the replacement
  static RewriteResult optimizeMoveSequence(jit::lir::BasicBlock* basicblock);

  // rewrite division instructions to use correct registers
  static RewriteResult rewriteDivide(instr_iter_t instr_iter);

  // insert a move from an operand to a memory location given by base + index.
  // this function handles cases where operand is a >32-bit immediate and
  // operand is a stack location.
  static void insertMoveToMemoryLocation(
      lir::BasicBlock* block,
      instr_iter_t instr_iter,
      PhyLocation base,
      int index,
      const lir::OperandBase* operand,
      PhyLocation temp = PhyLocation::RAX);

  static bool insertMoveToRegister(
      lir::BasicBlock* block,
      instr_iter_t instr_iter,
      lir::Operand* op,
      PhyLocation location);
};

} // namespace jit::lir
