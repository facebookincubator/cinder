// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __HIR_SSA_H__
#define __HIR_SSA_H__

#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"

#include <fmt/format.h>

#include <iosfwd>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jit {
namespace hir {

// Check that func's CFG is well-formed and that its Register uses and defs are
// vald SSA, returning true iff no errors were found. Details of any errors
// will be written to err.
bool checkFunc(const Function& func, std::ostream& err);

// Compute and return the output type of the given instruction, ignoring the
// current type of its output Register.
Type outputType(const Instr& instr);

// Re-derive all Register types in the given function. Meant to be called after
// SSAify and any optimizations that could refine the output type of an
// instruction.
void reflowTypes(Function& func);

struct SSABasicBlock {
  BasicBlock* block;
  int unsealed_preds;

  std::unordered_set<SSABasicBlock*> preds;
  std::unordered_set<SSABasicBlock*> succs;

  std::unordered_map<Register*, Register*>
      local_defs; // register -> current value
  std::unordered_map<Register*, Phi*>
      phi_nodes; // value -> phi that produced it
  std::vector<std::pair<Register*, Register*>>
      incomplete_phis; // register -> phi output

  explicit SSABasicBlock(BasicBlock* b = nullptr)
      : block(b), unsealed_preds(0) {}
};

class SSAify : public Pass {
 public:
  SSAify() : Pass("SSAify"), irfunc_(nullptr) {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<SSAify> Factory() {
    return std::make_unique<SSAify>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SSAify);

  Register* GetDefine(SSABasicBlock* ssa_block, Register* reg);

  // check if the defs going to phi function is trivial
  // return a replacement register if it is trivial
  // return nullptr otherwise.
  Register* GetCommonPredValue(
      const Register* out_reg,
      const std::unordered_map<BasicBlock*, Register*>& defs);

  void FixIncompletePhis(SSABasicBlock* ssa_block);

  void FixRegisters(
      std::unordered_map<BasicBlock*, SSABasicBlock*>& ssa_basic_blocks);
  std::unordered_map<BasicBlock*, SSABasicBlock*> InitSSABasicBlocks(
      std::vector<BasicBlock*>& blocks);

  Register* getReplacement(Register* reg);
  void maybeAddPhi(SSABasicBlock* ssa_block, Register* reg, Register* out);
  void removeTrivialPhi(
      SSABasicBlock* ssa_block,
      Register* reg,
      Register* from,
      Register* to);
  Function* irfunc_;
  std::unordered_map<Register*, Register*> reg_replacements_;
  std::unordered_map<Register*, std::unordered_map<Phi*, SSABasicBlock*>>
      phi_uses_;
  Register* null_reg_{nullptr};
};

} // namespace hir
} // namespace jit

#endif
