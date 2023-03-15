// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/dataflow.h"
#include "Jit/hir/alias_class.h"
#include "Jit/hir/hir.h"
#include "Jit/util.h"

#include <iosfwd>

namespace jit::hir {

class BasicBlock;
class Function;
class Register;

using RegisterSet = std::unordered_set<Register*>;
extern const RegisterSet kEmptyRegSet;

std::ostream& operator<<(std::ostream& os, const RegisterSet& set);

// Return true if the given instruction returns an exact copy of its input "at
// runtime" (most passthrough instructions will be copy-propagated away in
// LIR). The output differs only in some HIR-level property that is erased in
// the generated code, usually its Type.
//
// This is used by modelReg() and optimizations that want to treat all
// HIR-level copies of a value as one combined entity (see the 'Value copies'
// section of Jit/hir/refcount_insertion.md for a concrete example).
bool isPassthrough(const Instr& instr);

// Trace through any passthrough instructions in the definition chain of the
// given value, returning the original source of the value.
Register* modelReg(Register* reg);

// Returns true if each instruction in func properly type-checks
// Writes to err if any failure occurs and returns false
bool funcTypeChecks(const Function& func, std::ostream& err);

// Returns true iff the constraint signifies that all of its instruction's
// operands must match
bool operandsMustMatch(OperandType op_type);

// Returns true if the type satisfies the passed in OperandType
bool registerTypeMatches(Type op_type, OperandType expected_type);

// Base class for dataflow analyses that compute facts about registers in the
// HIR.
//
// An analysis will typically inherit from either ForwardDataflowAnalysis or
// BackwardDataflowAnalysis and provide implementations of `ComputeGenKill`,
// `ComputeNewIn`, and `ComputeNewOut` that implement the analysis.
//
// TODO(mpage) - This is potentially a good candidate for CRTP if the
// performance overhead of the virtual functions becomes an issue.
class DataflowAnalysis {
 public:
  explicit DataflowAnalysis(const Function& irfunc)
      : irfunc_(irfunc),
        num_bits_(0),
        df_analyzer_(),
        df_entry_(),
        df_exit_(),
        df_blocks_() {}

  virtual ~DataflowAnalysis() {}

  virtual void Run() = 0;

  RegisterSet GetIn(const BasicBlock* block);
  RegisterSet GetOut(const BasicBlock* block);

 protected:
  virtual void ComputeGenKill(
      const BasicBlock* block,
      RegisterSet& gen,
      RegisterSet& kill) = 0;
  virtual jit::util::BitVector ComputeNewIn(
      const jit::optimizer::DataFlowBlock* block) = 0;
  virtual jit::util::BitVector ComputeNewOut(
      const jit::optimizer::DataFlowBlock* block) = 0;

  // Should be overridden by subclasses to set an appropriate uninitialized in-
  // or out-state on the given block, if it should be something other than all
  // zeros.
  virtual void setUninitialized(jit::optimizer::DataFlowBlock* block) = 0;

  virtual void Initialize();
  void AddBasicBlock(const BasicBlock* cfg_block);

  virtual std::string name() = 0;

  void dump();

  const Function& irfunc_;
  size_t num_bits_;
  jit::optimizer::DataFlowAnalyzer<Register*> df_analyzer_;
  jit::optimizer::DataFlowBlock df_entry_;
  jit::optimizer::DataFlowBlock df_exit_;
  std::unordered_map<const BasicBlock*, jit::optimizer::DataFlowBlock>
      df_blocks_;
};

class BackwardDataflowAnalysis : public DataflowAnalysis {
 public:
  explicit BackwardDataflowAnalysis(const Function& irfunc)
      : DataflowAnalysis(irfunc) {}

  void Run() override;
};

class ForwardDataflowAnalysis : public DataflowAnalysis {
 public:
  explicit ForwardDataflowAnalysis(const Function& irfunc)
      : DataflowAnalysis(irfunc) {}

  void Run() override;
};

class LivenessAnalysis : public BackwardDataflowAnalysis {
 public:
  explicit LivenessAnalysis(const Function& irfunc)
      : BackwardDataflowAnalysis(irfunc) {}

  bool IsLiveIn(const BasicBlock* cfg_block, Register* reg);
  bool IsLiveOut(const BasicBlock* cfg_block, Register* reg);

  using LastUses =
      std::unordered_map<const Instr*, std::unordered_set<Register*>>;

  // Compute and return a map indicating which values die after which
  // instructions. Must be called after Run().
  LastUses GetLastUses();

 protected:
  void ComputeGenKill(
      const BasicBlock* block,
      RegisterSet& gen,
      RegisterSet& kill) final;
  jit::util::BitVector ComputeNewIn(
      const jit::optimizer::DataFlowBlock* block) final;
  jit::util::BitVector ComputeNewOut(
      const jit::optimizer::DataFlowBlock* block) final;
  void setUninitialized(jit::optimizer::DataFlowBlock* block) override final;

  std::string name() final {
    return "LivenessAnalysis";
  }
};

// This computes which registers have been initialized at a basic block.
//
// A register is definitely assigned if it has been assigned to along all paths
// into a block. A register is maybe assigned if has been assigned along any
// path to the block.
//
// This information can be used to eliminate null checks for variables that are
// definitely assigned.
//
// NB: This doesn't support DEL_FAST yet (and probably never will).
//
// TODO(mpage): We probably don't need to run this over temporaries. They should
// always be assigned before being used.
//
// Each bit in the bit-vector represents whether or not the corresponding
// register has been assigned. Local summaries for each block are computed as
// follows:
//
//
//   foreach instruction I in B in order:
//      Gen(B) = Gen(B) U OutputRegister(I)
//
//   Kill(B) = {}  -- could extend this to handle DEL_FAST
//
// Dataflow information is propagated using the following equations:
//
// For definite assignment:
//   In(B) = And(Out(P) for P in Preds(B))
//
// For maybe assignment:
//   In(B) = Or(Out(P) for P in Preds(B))
//
// In both cases:
//   Out(B) = Gen(B) U (In(B) - Kill(B))
//
class AssignmentAnalysis : public ForwardDataflowAnalysis {
 public:
  AssignmentAnalysis(const Function& irfunc, bool is_definite);

  bool IsAssignedIn(const BasicBlock* cfg_block, Register* reg);
  bool IsAssignedOut(const BasicBlock* cfg_block, Register* reg);

 protected:
  void ComputeGenKill(
      const BasicBlock* block,
      RegisterSet& gen,
      RegisterSet& kill) final;
  jit::util::BitVector ComputeNewIn(
      const jit::optimizer::DataFlowBlock* block) final;
  jit::util::BitVector ComputeNewOut(
      const jit::optimizer::DataFlowBlock* block) final;
  void setUninitialized(jit::optimizer::DataFlowBlock* block) override final;

  std::string name() final {
    return fmt::format(
        "{}AssignmentAnalysis", is_definite_ ? "Definite" : "Maybe");
  }

  RegisterSet args_;

  bool is_definite_;
};

// Find the immediate dominator of each block, stored in a mapping from block
// ids to blocks. The mapping returns nullptr if the block has no dominator.
// This is the case for the entry block and any blocks not reachable from the
// entry block.
//
// This implementation is based off of HHVM's implementation, which itself uses
// Cooper, Harvey, and Kennedy's "A Simple, Fast Dominance Algorithm".
class DominatorAnalysis {
 public:
  DominatorAnalysis(const Function& irfunc);

  const BasicBlock* immediateDominator(const BasicBlock* block) {
    JIT_DCHECK(block != nullptr, "Block cannot be null");
    return idoms_[block->id];
  }

  const std::unordered_set<const BasicBlock*>& getBlocksDominatedBy(
      const BasicBlock* block) {
    JIT_DCHECK(block != nullptr, "Block cannot be null");
    return dom_sets_[block->id];
  }

 private:
  std::unordered_map<int, const BasicBlock*> idoms_;
  std::unordered_map<int, std::unordered_set<const BasicBlock*>> dom_sets_;
};

// Stores type information about registers that doesn't get stored in the
// Register's type. This currently means keeping track of `HintType`s and `Phi`s
// which can provide type hints
//
// Since type information might change throughout the program, the analysis
// exposes this type information by allowing users to query for the dominating
// type hint instruction. This gives users access to the potential types as
// well as where that type information was created. The querying is done at the
// BasicBlock level under the assumption that BasicBlocks should be small enough
// that type information that is learned later on in the block should still be
// valid earlier in the block.
class RegisterTypeHints {
 public:
  RegisterTypeHints(const Function& irfunc);

  const Instr* dominatingTypeHint(Register* reg, const BasicBlock* block);

 private:
  // Contains a mapping of Registers to a mapping of BasicBlock ids to type hint
  // instructions.
  // This allows users to query type hints for Registers in a
  // flow-sensitive way
  std::unordered_map<Register*, std::unordered_map<int, const Instr*>>
      dom_hint_;
  DominatorAnalysis doms_;
};

} // namespace jit::hir
