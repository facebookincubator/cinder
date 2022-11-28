// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/codegen/environ.h"
#include "Jit/lir/lir.h"
#include "Jit/util.h"

#include <memory>
#include <unordered_map>

namespace jit::codegen::autogen {

// this struct defines a trie tree node to support instruction
// operand type matching.
struct PatternNode {
  using func_t = void (*)(Environ*, const jit::lir::Instruction*);

  std::unordered_map<char, std::unique_ptr<PatternNode>> next;
  func_t func{nullptr};
};

// A machine code generator from LIR.
// This class generates machine code based on a set of user-defined rules.
// See autogen.cpp file for details.
class AutoTranslator {
 public:
  static AutoTranslator& getInstance() {
    static AutoTranslator autotrans;
    return autotrans;
  }

  void translateInstr(Environ* env, const jit::lir::Instruction* instr) const;

  static asmjit::x86::Gp getGp(
      const jit::lir::OperandBase* op,
      unsigned int reg) {
    auto data_type = op->dataType();
    switch (data_type) {
      case jit::lir::OperandBase::k8bit:
        return asmjit::x86::gpb(reg);
      case jit::lir::OperandBase::k16bit:
        return asmjit::x86::gpw(reg);
      case jit::lir::OperandBase::k32bit:
        return asmjit::x86::gpd(reg);
      case jit::lir::OperandBase::kObject:
      case jit::lir::OperandBase::k64bit:
        return asmjit::x86::gpq(reg);
      case jit::lir::OperandBase::kDouble:
        JIT_CHECK(false, "incorrect register type.");
    }
    Py_UNREACHABLE();
  }

  static asmjit::x86::Xmm getXmm(const jit::lir::OperandBase* op) {
    auto data_type = op->dataType();
    switch (data_type) {
      case jit::lir::OperandBase::kDouble:
        return asmjit::x86::xmm(
            op->getPhyRegister() - PhyLocation::XMM_REG_BASE);
      default:
        JIT_CHECK(false, "incorrect register type.");
    }
  }

  static asmjit::x86::Gp getGp(const jit::lir::OperandBase* op) {
    return getGp(op, op->getPhyRegister());
  }

 private:
  AutoTranslator() {
    initTable();
  }

  std::
      unordered_map<jit::lir::Instruction::Opcode, std::unique_ptr<PatternNode>>
          instr_rule_map_;

  void initTable();

  DISALLOW_COPY_AND_ASSIGN(AutoTranslator);
};

} // namespace jit::codegen::autogen
