// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/containers.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/operand.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace jit::lir {

std::unordered_set<std::string>& GetStringLiterals();

class ParserException : public std::exception {
 public:
  ParserException(const std::string& message) noexcept : message(message) {}

  const char* what() const noexcept override {
    return message.c_str();
  }

 private:
  std::string const message;
};

class Parser {
 public:
  // Parse the code and generate a Function object.
  // The first and the last basic block of the code must be an entry block
  // and an exit block, respectively.
  // TODO(tiansi): Add another mode where the entry block and exit block
  // are not required in the code but generated automatically.
  // The returned basic blocks are in the same order as they apppear in the
  // code.
  // Throws an exception if parsing error occurs.
  std::unique_ptr<Function> parse(const std::string& code);

  const UnorderedMap<int, Instruction*> getOutputInstrMap() const {
    return output_index_map_;
  }

 private:
  void setSection(const std::string& bbdef, BasicBlock* bb);
  void setSuccessorBlocks(const std::string& bbdef, BasicBlock* bb);

  OperandBase::DataType getOperandDataType(const std::string& name) const;

  Instruction::Opcode getInstrOpcode(const std::string& name) const;

  enum TokenType {
    kFunctionStart,
    kBasicBlockStart,
    kNewLine,
    kComment,
    kVReg,
    kPhyReg,
    kStack,
    kAddress,
    kImmediate,
    kId,
    kEqual,
    kComma,
    kParLeft,
    kParRight,
    kBasicBlockRef,
    kError,
    kDataType,
    kIndirect,
    kStringLiteral
  };

  struct Token {
    TokenType type{kError};
    ssize_t length{0};
    int64_t data{0};
  };

  Token getNextToken(const char* s);
  void parseInput(const Token& token, const char* code);
  void parseIndirect(Operand* opnd, std::string_view token, const char* code);
  void fixOperands();
  void connectBasicBlocks();
  void fixUnknownIds();

  // current function, basic block and instruction
  Function* func_{nullptr};
  BasicBlock* block_{nullptr};
  Instruction* instr_{nullptr};

  // mapping basic block indices to basic block objects
  UnorderedMap<int, BasicBlock*> block_index_map_;
  // mapping output vreg number to the instruction generating the output
  UnorderedMap<int, Instruction*> output_index_map_;

  // basic block and instruction references to be fixed
  UnorderedMap<Operand*, int> basic_block_refs_;
  UnorderedMap<LinkedOperand*, int> instr_refs_;

  // succesors that need to be linked
  // Note - the order of pairs matters for conditional branching
  std::vector<std::pair<BasicBlock*, int>> basic_block_succs_;
};

} // namespace jit::lir
