// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/hir/printer.h"
#include "Jit/lir/block.h"
#include "Jit/lir/function.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/operand.h"

#include <iostream>
#include <unordered_map>

namespace jit::lir {

class Printer {
 public:
  void print(std::ostream& out, const Function& func);
  void print(std::ostream& out, const BasicBlock& block);
  void print(std::ostream& out, const Instruction& instr);
  void print(std::ostream& out, const OperandBase& operand);
  void print(std::ostream& out, const MemoryIndirect& memind);

 private:
  const Function* getFunction(const BasicBlock& block) {
    return block.function();
  }
  const Function* getFunction(const Instruction& instr) {
    return getFunction(*instr.basicblock());
  }
  const Function* getFunction(const OperandBase& opnd) {
    return getFunction(*opnd.instr());
  }

  hir::HIRPrinter hir_printer_{false, "# "};
};

class JSONPrinter {
 public:
  nlohmann::json print(const Function& func, const char* pass_name);
  nlohmann::json print(const BasicBlock& block);
  nlohmann::json print(const Instruction& instr);
  std::string print(const OperandBase& operand);
};

inline std::ostream& operator<<(std::ostream& out, const Function& func) {
  Printer().print(out, func);
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const BasicBlock& block) {
  Printer().print(out, block);
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const Instruction& instr) {
  Printer().print(out, instr);
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const OperandBase& operand) {
  Printer().print(out, operand);
  return out;
}

inline std::ostream& operator<<(
    std::ostream& out,
    const MemoryIndirect& memind) {
  Printer().print(out, memind);
  return out;
}

} // namespace jit::lir
