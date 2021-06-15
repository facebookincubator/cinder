// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/parser.h"
#include "Jit/codegen/x86_64.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <string>
#include <utility>

namespace jit {
namespace lir {

Parser::Token Parser::getNextToken(const char* str) {
  struct PatternType {
    PatternType(const char* pattern, TokenType t)
        : re(std::string("^") + pattern), type(t) {}
    std::regex re;
    TokenType type;
  };

  static const std::vector<PatternType> patterns{
      {"Function:.*\n", kFunctionStart},
      {"BB %(\\d+)( - .*)?\n", kBasicBlockStart},
      {"\n", kNewLine},
      {"%(\\d+)", kVReg},
      {"R[0-9A-Z]+", kPhyReg},
      {"\\[RBP[ ]?-[ ]?(\\d+)\\]", kStack},
      {"\\[(0x[0-9a-fA-F]+)\\]", kAddress},
      {"(\\d+)(\\(0x[0-9a-fA-F]+\\))?", kImmediate},
      {"BB%(\\d+)", kBasicBlockRef},
      {"[A-Za-z][A-Za-z0-9]+", kId},
      {"=", kEqual},
      {",", kComma},
      {"\\(", kParLeft},
      {"\\)", kParRight},
      {"#.*\n", kComment},
      {":[A-Za-z0-9]+", kDataType}};

  std::cmatch m;
  for (auto& pattern : patterns) {
    if (!std::regex_search(str, m, pattern.re)) {
      continue;
    }

    if (m.size() > 1) {
      return {pattern.type, m.length(), strtoll(m.str(1).c_str(), NULL, 0)};
    }
    return {pattern.type, m.length()};
  }

  return {kError};
}

static void expect(bool cond, const char* cur, const char* msg = "") {
  if (cond) {
    return;
  }

  JIT_LOG("Unable to parse - %s", msg);
  if (strlen(cur) > 64) {
    std::string m(cur, cur + 64);
    m += "...";
    JIT_LOG("String from %s", m);
  } else {
    JIT_LOG("Starting from %s", cur);
  }
  JIT_CHECK(false, "Parsing terminated.");
}

std::unique_ptr<Function> Parser::parse(const std::string& code) {
  enum {
    FUNCTION,
    BASIC_BLOCK,
    INSTR_OUTPUT,
    INSTR_OUTPUT_TYPE,
    INSTR_EQUAL,
    INSTR_NAME,
    INSTR_INPUT_START,
    INSTR_INPUT,
    INSTR_INPUT_TYPE,
    INSTR_INPUT_COMMA,
    PHI_INPUT_FIRST,
    PHI_INPUT_COMMA,
    PHI_INPUT_SECOND,
    PHI_INPUT_SECOND_TYPE,
    PHI_INPUT_PAR,
  } state = FUNCTION;

  std::unique_ptr<Function> func;
  const char* codestr = code.c_str();
  const char* cur = codestr;
  const char* end = codestr + code.size();

  while (cur != end) {
    auto token = getNextToken(cur);
    auto type = token.type;

    while (true) {
      switch (state) {
        case FUNCTION: {
          // expect a function start
          if (type == kNewLine) {
            break;
          }

          expect(type == kFunctionStart, cur, "Expect a function start.");
          func = std::make_unique<Function>();
          func_ = func.get();
          state = BASIC_BLOCK;
          break;
        }
        case BASIC_BLOCK: {
          // expect a basic block start
          if (type == kNewLine) {
            break;
          }
          expect(type == kBasicBlockStart, cur, "Expect a basic block start.");
          int id = token.data;

          block_ = func_->allocateBasicBlock();
          auto pair = block_index_map_.emplace(id, block_);
          expect(pair.second, cur, "Duplicated basic block id.");

          state = INSTR_OUTPUT;
          break;
        }
        case INSTR_OUTPUT: {
          if (type == kNewLine) {
            break;
          } else if (type == kBasicBlockStart) {
            state = BASIC_BLOCK;
            continue;
          }

          instr_ = block_->allocateInstr(Instruction::kNone, nullptr);
          auto output = instr_->output();
          if (type == kId) {
            state = INSTR_NAME;
            continue;
          } else if (type == kVReg) {
            output->setVirtualRegister();
            auto pair = output_index_map_.emplace(token.data, instr_);
            expect(pair.second, cur, "Duplicated output virtual register.");
          } else if (type == kPhyReg) {
            output->setPhyRegister(jit::codegen::PhyLocation::parse(
                std::string(cur, token.length)));
          } else if (type == kStack) {
            output->setStackSlot(token.data);
          } else if (type == kAddress) {
            output->setMemoryAddress(reinterpret_cast<void*>(token.data));
          } else if (type == kImmediate) {
            output->setConstant(token.data);
          } else {
            expect(false, cur);
          }
          state = INSTR_OUTPUT_TYPE;
          break;
        }
        case INSTR_OUTPUT_TYPE: {
          if (type == kEqual) {
            state = INSTR_EQUAL;
            continue;
          }
          expect(type == kDataType, cur, "Expect output data type.");
          instr_->output()->setDataType(
              getOperandDataType(std::string(cur, token.length)));
          state = INSTR_EQUAL;
          break;
        }
        case INSTR_EQUAL: {
          expect(type == kEqual, cur, "Expect \"=\".");
          state = INSTR_NAME;
          break;
        }
        case INSTR_NAME: {
          expect(type == kId, cur, "Expect an instruction name.");
          instr_->setOpcode(getInstrOpcode(std::string(cur, token.length)));
          state = INSTR_INPUT;
          break;
        }
        case INSTR_INPUT_START: {
          if (type == kNewLine) {
            state = INSTR_OUTPUT;
            break;
          }
          state = INSTR_INPUT;
          continue;
        }
        case INSTR_INPUT: {
          if (type == kParLeft) {
            state = PHI_INPUT_FIRST;
          } else {
            parseInput(token, cur);
            state = INSTR_INPUT_TYPE;
          }
          break;
        }
        case INSTR_INPUT_TYPE: {
          if (type == kComma || type == kNewLine) {
            state = INSTR_INPUT_COMMA;
            continue;
          }
          expect(type == kDataType, cur, "Expect input data type.");
          expect(
              instr_->getNumInputs() > 0,
              cur,
              "Expect data type to follow an input.");
          OperandBase* input_base =
              instr_->getInput(instr_->getNumInputs() - 1);
          if (!input_base->isLinked()) {
            Operand* input = static_cast<Operand*>(input_base);
            auto data_type = getOperandDataType(std::string(cur, token.length));
            input->setDataType(data_type);
          }
          state = INSTR_INPUT_COMMA;
          break;
        }
        case INSTR_INPUT_COMMA: {
          // expect commas between inputs
          if (type == kNewLine) {
            state = INSTR_OUTPUT;
            break;
          }

          expect(type == kComma, cur, "Expect a comma.");
          state = INSTR_INPUT;
          break;
        }
        case PHI_INPUT_FIRST: {
          // first argument of phi input pairs - basic block id
          expect(type == kBasicBlockRef, cur, "Expect a basic block id.");
          parseInput(token, cur);
          state = PHI_INPUT_COMMA;
          break;
        }
        case PHI_INPUT_COMMA: {
          expect(type == kComma, cur, "Expect a comma.");
          state = PHI_INPUT_SECOND;
          break;
        }
        case PHI_INPUT_SECOND: {
          // second argument of phi input pairs - a variable
          parseInput(token, cur);
          state = PHI_INPUT_SECOND_TYPE;
          break;
        }
        case PHI_INPUT_SECOND_TYPE: {
          if (type == kParRight) {
            state = PHI_INPUT_PAR;
            continue;
          }
          expect(type == kDataType, cur, "Expect phi input second data type.");
          expect(
              instr_->getNumInputs() > 0,
              cur,
              "Expect data type to follow an input.");
          OperandBase* input_base =
              instr_->getInput(instr_->getNumInputs() - 1);
          if (!input_base->isLinked()) {
            Operand* input = static_cast<Operand*>(input_base);
            auto data_type = getOperandDataType(std::string(cur, token.length));
            input->setDataType(data_type);
          }
          state = PHI_INPUT_PAR;
          break;
        }
        case PHI_INPUT_PAR: {
          // expect a right parenthesis
          expect(type == kParRight, cur, "Expect a right parenthesis");
          state = INSTR_INPUT_COMMA;
          break;
        }
      }

      break;
    }

    cur += token.length;
    // skip whitespaces
    while (cur != end && (*cur == ' ' || *cur == '\t')) {
      cur++;
    }
  }

  fixOperands();
  connectBasicBlocks();

  return func;
}

OperandBase::DataType Parser::getOperandDataType(
    const std::string& name) const {
  static const std::unordered_map<std::string, OperandBase::DataType>
      type_name_to_data_type = {
#define TYPE_NAME_TO_DATA_TYPE(v, ...) {":" #v, OperandBase::k##v},
          FOREACH_OPERAND_DATA_TYPE(TYPE_NAME_TO_DATA_TYPE)
#undef TYPE_NAME_TO_DATA_TYPE
      };

  return map_get_strict(type_name_to_data_type, name);
}

Instruction::Opcode Parser::getInstrOpcode(const std::string& name) const {
  static const std::unordered_map<std::string, Instruction::Opcode>
      instr_name_to_opcode = {
#define INSTR_NAME_TO_OPCODE(v, ...) {#v, Instruction::k##v},
          FOREACH_INSTR_TYPE(INSTR_NAME_TO_OPCODE)
#undef INSTR_NAME_TO_OPCODE
      };

  return map_get_strict(instr_name_to_opcode, name);
}

void Parser::parseInput(const Token& token, const char* code) {
  auto type = token.type;
  switch (type) {
    case kVReg: {
      auto linked_opnd = instr_->allocateLinkedInput(nullptr);
      auto id = token.data;
      instr_refs_.emplace(linked_opnd, id);
      break;
    }
    case kPhyReg: {
      auto reg =
          jit::codegen::PhyLocation::parse(std::string(code, token.length));
      expect(
          reg != jit::codegen::PhyLocation::REG_INVALID,
          code,
          "Unable to parse physical register.");
      instr_->allocatePhyRegisterInput(reg);

      break;
    }
    case kStack: {
      instr_->allocateStackInput(token.data);
      break;
    }
    case kAddress: {
      instr_->allocateAddressInput(reinterpret_cast<void*>(token.data));
      break;
    }
    case kImmediate: {
      instr_->allocateImmediateInput(token.data);
      break;
    }
    case kBasicBlockRef: {
      auto opnd = instr_->allocateImmediateInput(0);
      basic_block_refs_.emplace(opnd, token.data);
      break;
    }
    default:
      expect(false, code, "Unable to parse instruction input.");
  }
}

void Parser::fixOperands() {
  for (auto& pair : basic_block_refs_) {
    auto operand = pair.first;
    int block_index = pair.second;

    operand->setBasicBlock(map_get_strict(block_index_map_, block_index));
  }

  for (auto& pair : instr_refs_) {
    auto operand = pair.first;
    int instr_index = pair.second;

    auto instr = map_get_strict(output_index_map_, instr_index);
    instr->output()->addUse(operand);
  }
}

void Parser::connectBasicBlocks() {
  auto& basicblocks = func_->basicblocks();
  for (auto& block : basicblocks) {
    auto instr = block->getLastInstr();

    // the last basic block may have no instructions
    if (instr == nullptr) {
      continue;
    }

    auto opcode = instr->opcode();
    if (opcode == Instruction::kBranch) {
      auto succ = instr->getInput(0)->getBasicBlock();
      block->addSuccessor(succ);
    } else if (opcode == Instruction::kCondBranch) {
      auto succ = instr->getInput(1)->getBasicBlock();
      block->addSuccessor(succ);
      succ = instr->getInput(2)->getBasicBlock();
      block->addSuccessor(succ);
    } else if (opcode == Instruction::kReturn) {
      auto& last_block = *(func_->basicblocks().rbegin());
      block->addSuccessor(last_block);
    }
  }

  // the first basic block has no branch instructions in the end,
  // so it always falls through.
  auto& block = basicblocks[0];
  auto& second_block = basicblocks[1];
  block->addSuccessor(second_block);
}

} // namespace lir
} // namespace jit
