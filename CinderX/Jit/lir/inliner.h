// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/containers.h"
#include "Jit/lir/function.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/operand.h"
#include "Jit/util.h"

namespace jit {

extern int g_disable_lir_inliner;

namespace lir {

class LIRInliner {
 public:
  explicit LIRInliner(lir::Instruction* instr) : call_instr_(instr) {}

  // Public function for inlining call_instr_.
  // Return true if inlining succeeds.
  // Return false if inlining cannot be completed
  // and don't modify call_instr_ and its function.
  // NOTE: Assume that callee and caller don't have relative jumps or stack
  // allocation instructions. These instructions should be very infrequent, but
  // we may want to add a check for this later.
  bool inlineCall();

  // Given a function, try to inline all calls.
  // Return true if one or more calls have been inlined (i.e. the function has
  // been modified). Otherwise, return false.
  static bool inlineCalls(lir::Function* function);

 private:
  // The call instruction that we want to inline.
  lir::Instruction* call_instr_;
  // After copying the callee into the caller,
  // callee_start is the index of the first callee block (i.e. the entry block)
  // and callee_end is the index of the last callee block (i.e. the exit block)
  // in caller->basic_blocks_.
  int callee_start_;
  int callee_end_;
  // List of arguments from call_instr_.
  std::vector<lir::OperandBase*> arguments_;

  // Checks if call instruction and callee are inlineable.
  // Calls checkEntryExitReturn, checkArguments, checkLoadArg.
  // Return true if they are inlineable, otherwise return false.
  // NOTE: We may want to extract some of these checks, so that we can apply
  // them as a general pass across all functions.
  bool isInlineable(const lir::Function* callee);

  // Check that there is exactly 1 entry and 1 exit block.
  // Check that these blocks are found at the ends of basic_blocks_.
  // Check that return statements only appear in
  // the predecesoors of the exit block.
  bool checkEntryExitReturn(const lir::Function* callee);

  // Check that call inputs are immediate or virtual registers.
  // Add the inputs to arguments_.
  bool checkArguments();

  // Check that kLoadArg instructions occur at the beginning.
  // Check that kLoadArg instructions don't exceed the number of arguments.
  bool checkLoadArg(const lir::Function* callee);

  // Find corresponding function body.
  // Returns nullptr if function cannot be found.
  lir::Function* findFunction();

  // Given the address of the function, try to find the corresponding LIR text
  // and parse it.
  lir::Function* parseFunction(uint64_t addr);

  // Assume that kLoadArg instructions are only found
  // at the beginning of callee_.
  bool resolveArguments();

  // Assume that instr_it corresponds to a kLoadArg instruction.
  // Assume that arguments are immediate or linked.
  void resolveLoadArg(
      UnorderedMap<lir::OperandBase*, lir::LinkedOperand*>& vreg_map,
      lir::BasicBlock* bb,
      lir::BasicBlock::InstrList::iterator& instr_it);

  // For instr_it that aren't kLoadArg,
  // fix up linked arguments that refer to outputs of kLoadArg instructions.
  void resolveLinkedArgumentsUses(
      UnorderedMap<lir::OperandBase*, lir::LinkedOperand*>& vreg_map,
      std::list<std::unique_ptr<lir::Instruction>>::iterator& instr_it);

  // Expects callee to have one empty epilogue block.
  // Expects return instructions to only appear as
  // the last statement in the predecessors of the epilogue blocks.
  void resolveReturnValue();

  FRIEND_TEST(LIRInlinerTest, ResolveArgumentsTest);
  FRIEND_TEST(LIRInlinerTest, ResolveReturnWithPhiTest);
  FRIEND_TEST(LIRInlinerTest, ResolveReturnWithoutPhiTest);
  FRIEND_TEST(LIRInlinerTest, FindFunctionSuccessTest);
  FRIEND_TEST(LIRInlinerTest, FindFunctionFailureTest);
};

} // namespace lir
} // namespace jit
