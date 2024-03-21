// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "cinderx/Jit/codegen/environ.h"
#include "cinderx/Jit/containers.h"
#include "cinderx/Jit/hir/hir.h"
#include "cinderx/Jit/jit_rt.h"
#include "cinderx/Jit/lir/block_builder.h"
#include "cinderx/Jit/lir/function.h"

#include <memory>
#include <string>

namespace jit::lir {

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// codegen.h with some interfaces changes so that it works with the new
// LIR.

class BasicBlockBuilder;

class LIRGenerator {
 public:
  explicit LIRGenerator(
      const jit::hir::Function* func,
      jit::codegen::Environ* env);

  std::unique_ptr<jit::lir::Function> TranslateFunction();

  const jit::hir::Function* GetHIRFunction() const {
    return func_;
  }

 private:
  // The result of translating one HIR block: the entry and exit LIR blocks.
  struct TranslatedBlock {
    BasicBlock* first;
    BasicBlock* last;
  };

  const jit::hir::Function* func_{nullptr};

  jit::codegen::Environ* env_{nullptr};

  BasicBlock* entry_block_{nullptr};
  BasicBlock* exit_block_{nullptr};

  std::vector<BasicBlock*> basic_blocks_;

  // Borrowed pointers so the caches can be looked up by index; they're
  // allocated from and owned by Runtime.
  std::vector<LoadTypeAttrCache*> load_type_attr_caches_;
  std::vector<LoadTypeMethodCache*> load_type_method_caches_;

  void AnalyzeCopies();
  BasicBlock* GenerateEntryBlock();
  BasicBlock* GenerateExitBlock();

  void appendGuardAlwaysFail(
      BasicBlockBuilder& bbb,
      const hir::DeoptBase& instr);

  template <class TOperand>
  void appendGuard(
      BasicBlockBuilder& bbb,
      InstrGuardKind kind,
      const hir::DeoptBase& hir_instr,
      TOperand&& guard_var) {
    JIT_CHECK(kind != InstrGuardKind::kAlwaysFail, "Use appendGuardAlwaysFail");
    auto deopt_id = bbb.makeDeoptMetadata();
    auto instr = bbb.appendInstr(
        Instruction::kGuard,
        Imm{kind},
        Imm{deopt_id},
        std::forward<TOperand>(guard_var));

    if (hir_instr.IsGuardIs()) {
      const auto& guard = static_cast<const hir::GuardIs&>(hir_instr);
      env_->code_rt->addReference(guard.target());
      instr->addOperands(MemImm{guard.target()});
    } else if (hir_instr.IsGuardType()) {
      const auto& guard = static_cast<const hir::GuardType&>(hir_instr);
      // TODO(T101999851): Handle non-Exact types
      JIT_CHECK(
          guard.target().isExact(), "Only exact type guards are supported");
      PyTypeObject* guard_type = guard.target().uniquePyType();
      JIT_CHECK(guard_type != nullptr, "Ensure unique representation exists");
      env_->code_rt->addReference(reinterpret_cast<PyObject*>(guard_type));
      instr->addOperands(MemImm{guard_type});
    } else {
      instr->addOperands(Imm{0});
    }

    addLiveRegOperands(bbb, instr, hir_instr);
  }

  void addLiveRegOperands(
      BasicBlockBuilder& bbb,
      Instruction* instr,
      const hir::DeoptBase& hir_instr);

  void MakeIncref(
      BasicBlockBuilder& bbb,
      const jit::hir::Instr& instr,
      bool xincref);
  void MakeDecref(
      BasicBlockBuilder& bbb,
      const jit::hir::Instr& instr,
      bool xdecref);

  bool TranslateSpecializedCall(
      BasicBlockBuilder& bbb,
      const jit::hir::VectorCallBase& instr);

  TranslatedBlock TranslateOneBasicBlock(const hir::BasicBlock* bb);

  // Fill in operands for phi instructions.  This is executed after LIR
  // instructions have been generated for all values in the control flow graph.
  void resolvePhiOperands(
      UnorderedMap<const hir::BasicBlock*, TranslatedBlock>& bb_map);

  void emitExceptionCheck(
      const jit::hir::DeoptBase& i,
      jit::lir::BasicBlockBuilder& bbb);

  // Load a string name from an HIR instruction that contains a name index.
  Instruction* getNameFromIdx(
      BasicBlockBuilder& bbb,
      const hir::DeoptBaseWithNameIdx* instr);

  Function* lir_func_{nullptr};
};

} // namespace jit::lir
