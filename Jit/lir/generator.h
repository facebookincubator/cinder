// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __LIR_GEN_H__
#define __LIR_GEN_H__

#include "Jit/codegen/environ.h"
#include "Jit/hir/hir.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/lir.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace jit {
namespace lir {

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
      jit::codegen::Environ* env)
      : func_(func),
        env_(env),
        entry_block_(nullptr),
        exit_block_(nullptr),
        temp_id(0),
        label_id(0) {}
  virtual ~LIRGenerator() {}

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

  void AnalyzeCopies();
  BasicBlock* GenerateEntryBlock();
  BasicBlock* GenerateExitBlock();

  std::string MakeGuard(
      const std::string& kind,
      const hir::DeoptBase& instr,
      const std::string& guard_var = std::string());

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
      std::unordered_map<const hir::BasicBlock*, TranslatedBlock>& bb_map);
  void FixOperands();
  void emitExceptionCheck(
      const jit::hir::DeoptBase& i,
      jit::lir::BasicBlockBuilder& bbb);

  Function* lir_func_{nullptr};
};

} // namespace lir
} // namespace jit

#endif
