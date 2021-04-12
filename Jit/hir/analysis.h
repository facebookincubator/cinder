#ifndef HIR_ANALYSIS_H
#define HIR_ANALYSIS_H

#include "Jit/dataflow.h"
#include "Jit/hir/alias_class.h"
#include "Jit/hir/hir.h"
#include "Jit/util.h"

#include <iosfwd>

namespace jit {
namespace hir {

class BasicBlock;
class Function;
class Register;

using RegisterSet = std::unordered_set<Register*>;
extern const RegisterSet kEmptyRegSet;

std::ostream& operator<<(std::ostream& os, const RegisterSet& set);

// Return true if the given instruction returns a copy of its input (usually
// with a refined Type).
bool isPassthrough(const Instr& instr);

// Trace through any passthrough instructions in the definition chain of the
// given value, returning the original source of the value.
Register* modelReg(Register* reg);

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

} // namespace hir
} // namespace jit

#endif
