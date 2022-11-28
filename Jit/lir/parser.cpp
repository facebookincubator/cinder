// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/parser.h"

#include "Jit/codegen/code_section.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/lir/operand.h"
#include "Jit/lir/symbol_mapping.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <string>
#include <utility>

namespace jit::lir {

std::unordered_set<std::string>& GetStringLiterals() {
  static std::unordered_set<std::string> string_literals_;
  return string_literals_;
}

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
      {"[A-Za-z_][A-Za-z0-9_]+", kId},
      {"=", kEqual},
      {",", kComma},
      {"\\(", kParLeft},
      {"\\)", kParRight},
      {"#.*\n", kComment},
      {":[A-Za-z0-9]+", kDataType},
      {"\\[[^\\]]*\\]", kIndirect},
      {"\"[^\"]+\"", kStringLiteral}};

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

// Throw exception if condition is false.
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

  throw ParserException(fmt::format("Unable to parse - %s", msg));
}

// Look up an item in the given map. Throw exception if doesn't exist.
template <typename Exc, typename M, typename K>
static auto& map_get_throw(M& map, const K& key) {
  auto it = map.find(key);
  if (it == map.end()) {
    throw Exc("Unable to parse - key not in map");
  }
  return it->second;
}

std::unique_ptr<Function> Parser::parse(const std::string& code) {
  enum {
    FUNCTION,
    BASIC_BLOCK,
    INSTR_OUTPUT,
    INSTR_OUTPUT_TYPE,
    INSTR_EQUAL,
    INSTR_NAME,
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
      if (token.type == kComment) {
        // skip comments for now
        break;
      }
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
          block_->setId(id);
          auto pair = block_index_map_.emplace(id, block_);
          expect(pair.second, cur, "Duplicated basic block id.");

          setSection(std::string(cur, token.length), block_);
          setSuccessorBlocks(std::string(cur, token.length), block_);

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
          instr_->setId(-1);
          auto output = instr_->output();
          if (type == kId) {
            state = INSTR_NAME;
            continue;
          } else if (type == kVReg) {
            output->setVirtualRegister();
            auto pair = output_index_map_.emplace(token.data, instr_);
            instr_->setId(token.data);
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
          } else if (type == kIndirect) {
            parseIndirect(output, std::string_view(cur, token.length), cur);
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
        case INSTR_INPUT: {
          if (type == kNewLine) {
            state = INSTR_OUTPUT;
            break;
          }
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
  fixUnknownIds();

  return func;
}

void Parser::setSection(const std::string& bbdef, BasicBlock* bb) {
  std::regex section_re = std::regex("- section: (.text|.coldtext)");
  std::cmatch section_m;
  if (std::regex_search(bbdef.c_str(), section_m, section_re) &&
      section_m.size() > 1) {
    std::string section = section_m.str(1);
    if (section == ".text") {
      bb->setSection(codegen::CodeSection::kHot);
    } else {
      JIT_CHECK(
          section == ".coldtext", "Code section must be .text or .coldtext.");
      bb->setSection(codegen::CodeSection::kCold);
    }
  }
}

void Parser::setSuccessorBlocks(const std::string& bbdef, BasicBlock* bb) {
  std::regex succ_re = std::regex("- succs: %(\\d+)(?: %(\\d+))?");
  std::cmatch succ_m;
  if (std::regex_search(bbdef.c_str(), succ_m, succ_re) && succ_m.size() > 1) {
    int64_t succ1 = atoll(succ_m.str(1).c_str());
    basic_block_succs_.emplace_back(bb, succ1);
    if (succ_m.size() > 2 && succ_m.str(2).size() > 0) {
      int64_t succ2 = atoll(succ_m.str(2).c_str());
      basic_block_succs_.emplace_back(bb, succ2);
    }
  }
}

OperandBase::DataType Parser::getOperandDataType(
    const std::string& name) const {
  static const std::unordered_map<std::string, OperandBase::DataType>
      type_name_to_data_type = {
#define TYPE_NAME_TO_DATA_TYPE(v, ...) {":" #v, OperandBase::k##v},
          FOREACH_OPERAND_DATA_TYPE(TYPE_NAME_TO_DATA_TYPE)
#undef TYPE_NAME_TO_DATA_TYPE
      };

  return map_get_throw<ParserException>(type_name_to_data_type, name);
}

Instruction::Opcode Parser::getInstrOpcode(const std::string& name) const {
  static const std::unordered_map<std::string, Instruction::Opcode>
      instr_name_to_opcode = {
#define INSTR_NAME_TO_OPCODE(v, ...) {#v, Instruction::k##v},
          FOREACH_INSTR_TYPE(INSTR_NAME_TO_OPCODE)
#undef INSTR_NAME_TO_OPCODE
      };

  return map_get_throw<ParserException>(instr_name_to_opcode, name);
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
    case kIndirect: {
      auto opnd = instr_->allocateMemoryIndirectInput(PhyLocation::REG_INVALID);
      parseIndirect(opnd, std::string_view(code, token.length), code);
      break;
    }
    case kId: {
      uint64_t imm_addr = map_get_throw<ParserException>(
          kSymbolMapping, std::string(code, token.length));
      instr_->allocateImmediateInput(
          reinterpret_cast<uint64_t>(imm_addr), OperandBase::kObject);
      break;
    }
    case kStringLiteral: {
      ThreadedCompileSerialize guard;
      std::unordered_set<std::string>& v = GetStringLiterals();
      auto ret = v.emplace(code, 1, token.length - 2);
      instr_->allocateImmediateInput(
          reinterpret_cast<uint64_t>((*ret.first).c_str()),
          OperandBase::kObject);
      break;
    }
    default:
      expect(false, code, "Unable to parse instruction input.");
  }
}

void Parser::parseIndirect(
    Operand* opnd,
    std::string_view token,
    const char* code) {
  std::variant<Instruction*, PhyLocation> base =
      jit::codegen::PhyLocation::REG_INVALID;
  std::variant<Instruction*, PhyLocation> index = nullptr;
  uint8_t multiplier = 0;
  int32_t offset = 0;

  std::cmatch m;

  // keep track of length of parsed operand
  // start at 1 to account for the right bracket
  size_t expected_length = 1;

  // parse base register
  std::regex base_reg = std::regex("\\[%(\\d+):[0-9a-zA-Z]+");
  std::regex base_phys = std::regex("\\[(R[0-9A-Z]+):Object");
  if (std::regex_search(token.begin(), token.end(), m, base_reg)) {
    auto base_id = std::stoll(m.str(1).c_str(), nullptr, 0);
    base = map_get_throw<ParserException>(output_index_map_, base_id);
    expected_length += m.length();
  } else if (std::regex_search(token.begin(), token.end(), m, base_phys)) {
    base = jit::codegen::PhyLocation::parse(m.str(1));
    expected_length += m.length();
  } else {
    expect(false, code, "Expected a base register.");
  }

  // parse index and multiplier
  std::regex index_reg = std::regex("\\+ %(\\d+):[0-9a-zA-Z]+( \\* (\\d+))?");
  std::regex index_phys = std::regex("\\+ (R[0-9A-Z]+):Object( \\* (\\d+))?");
  bool index_re_success = false;
  if (std::regex_search(token.begin(), token.end(), m, index_reg)) {
    auto index_id = std::stoll(m.str(1).c_str(), nullptr, 0);
    index = map_get_throw<ParserException>(output_index_map_, index_id);
    index_re_success = true;
    // add 1 for space between base and index operands
    expected_length += m.length() + 1;
  } else if (std::regex_search(token.begin(), token.end(), m, index_phys)) {
    index = jit::codegen::PhyLocation::parse(m.str(1));
    index_re_success = true;
    expected_length += m.length() + 1;
  }
  if (index_re_success && m.size() > 3 && m.str(3).size() > 0) {
    int64_t exp_multiplier = std::stoll(m.str(3).c_str(), nullptr, 0);
    expect(
        exp_multiplier != 0 && (exp_multiplier & (exp_multiplier - 1)) == 0,
        code,
        "The multiplier should not be zero and must be integral power of 2.");
    multiplier = __builtin_ctzll(exp_multiplier);
  }

  // parse offset
  std::regex offset_re = std::regex("([\\+-]) (0x[0-9a-fA-F]+)");
  if (std::regex_search(token.begin(), token.end(), m, offset_re)) {
    // need to remove space between sign and hex for stoll conversion
    offset = std::stoll((m.str(1) + m.str(2)).c_str(), nullptr, 0);
    expected_length += m.length() + 1;
  }
  expect(
      expected_length == token.length(),
      code,
      "Unable to parse memory indirect operand.");

  opnd->setMemoryIndirect(base, index, multiplier, offset);
}

void Parser::fixOperands() {
  for (auto& pair : basic_block_refs_) {
    auto operand = pair.first;
    int block_index = pair.second;

    operand->setBasicBlock(
        map_get_throw<ParserException>(block_index_map_, block_index));
  }

  for (auto& pair : instr_refs_) {
    auto operand = pair.first;
    int instr_index = pair.second;

    auto instr = map_get_throw<ParserException>(output_index_map_, instr_index);
    instr->output()->addUse(operand);
  }
}

void Parser::connectBasicBlocks() {
  // Note - Order of successors matters.
  // It depends on the order in which we add pairs to basic_block_succs_
  for (auto& succ_pair : basic_block_succs_) {
    BasicBlock* source_block = succ_pair.first;
    int dest_block_id = succ_pair.second;
    source_block->addSuccessor(
        map_get_throw<ParserException>(block_index_map_, dest_block_id));
  }
}

void Parser::fixUnknownIds() {
  // find largest ID
  int largest_id = -1;
  for (auto& bb : func_->basicblocks()) {
    if (bb->id() > largest_id) {
      largest_id = bb->id();
    }
    for (auto& instr : bb->instructions()) {
      if (instr->id() > largest_id) {
        largest_id = instr->id();
      }
    }
  }
  func_->setNextId(largest_id + 1);
  // all basic blocks should have been assigned an ID
  // assign ID's to instructions without ID's
  for (auto& bb : func_->basicblocks()) {
    for (auto& instr : bb->instructions()) {
      if (instr->id() == -1) {
        instr->setId(func_->allocateId());
      }
    }
  }
}

} // namespace jit::lir
