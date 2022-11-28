// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/containers.h"
#include "Jit/hir/hir.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/lir.h"

#include <memory>
#include <string>

namespace jit::lir {

class BasicBlockBuilder;

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

  const jit::hir::Function* func_;

  jit::codegen::Environ* env_;

  BasicBlock* entry_block_;
  BasicBlock* exit_block_;

  std::vector<BasicBlock*> basic_blocks_;

  // Borrowed pointers so the caches can be looked up by index; they're
  // allocated from and owned by Runtime.
  std::vector<LoadTypeAttrCache*> load_type_attr_caches_;

  void AnalyzeCopies();
  BasicBlock* GenerateEntryBlock();
  BasicBlock* GenerateExitBlock();

  void AppendGuard(
      BasicBlockBuilder& bbb,
      std::string_view kind,
      const hir::DeoptBase& instr,
      std::string_view guard_var = std::string_view());

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

  int temp_id;
  int label_id;
  std::string GetSafeTempName();
  std::string GetSafeLabelName();

  void FixPhiNodes(
      UnorderedMap<const hir::BasicBlock*, TranslatedBlock>& bb_map);
  void FixOperands();
  void emitExceptionCheck(
      const jit::hir::DeoptBase& i,
      jit::lir::BasicBlockBuilder& bbb);

  Function* lir_func_{nullptr};
};

} // namespace jit::lir
