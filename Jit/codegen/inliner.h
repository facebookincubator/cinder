// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __INLINER_H__
#define __INLINER_H__

#include "Jit/lir/function.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/operand.h"

#include "Jit/util.h"

namespace jit {
namespace codegen {

class LIRInliner {
 public:
  explicit LIRInliner(lir::Instruction* instr) : call_instr_(instr) {}

 private:
  // The call instruction that we want to inline.
  lir::Instruction* call_instr_;
  // After copying the callee into the caller,
  // callee_start is the index of the first callee block (i.e. the entry block)
  // and callee_end is the index of the last callee block (i.e. the exit block)
  // in caller->basic_blocks_.
  int callee_start_;
  int callee_end_;

  // Find corresponding function body.
  // Returns nullptr if function cannot be found.
  lir::Function* findFunction();

  // Given the name of the function, try to find the corresponding LIR text
  // and parse it.
  lir::Function* parseFunction(const std::string& name);

  // Assume that kLoadArg instructions are only found
  // at the beginning of callee_.
  bool resolveArguments();

  // Assume that instr_it corresponds to a kLoadArg instruction.
  // Assume that arguments are immediate or linked.
  void resolveLoadArg(
      std::vector<lir::OperandBase*>& argument_list,
      std::unordered_map<lir::OperandBase*, lir::LinkedOperand*>& vreg_map,
      lir::BasicBlock* bb,
      lir::BasicBlock::InstrList::iterator& instr_it);

  // For instr_it that aren't kLoadArg,
  // fix up linked arguments that refer to outputs of kLoadArg instructions.
  void resolveLinkedArgumentsUses(
      std::unordered_map<lir::OperandBase*, lir::LinkedOperand*>& vreg_map,
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

} // namespace codegen
} // namespace jit

#endif
