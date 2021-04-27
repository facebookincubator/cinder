// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/block_builder.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/lir.h"
#include "Jit/util.h"

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// bbbuilder.cpp with some interfaces changes so that it works with the new
// LIR.

namespace jit {
namespace lir {

static inline uint64_t stoull(std::string& s) {
  return std::stoull(s, 0, 0);
}

static inline std::string GetId(const std::string& s) {
  size_t colon;
  if ((colon = s.find(':')) != std::string::npos) {
    return s.substr(0, colon);
  }
  return s;
}

static inline std::pair<std::string, Operand::DataType> GetIdAndType(
    const std::string& name) {
  size_t colon;
  Operand::DataType data_type = Operand::kObject;

  if ((colon = name.find(':')) != std::string::npos) {
    std::string type = name.substr(colon + 1);
    if (type == "CInt8" || type == "CUInt8" || type == "CBool") {
      data_type = Operand::k8bit;
    } else if (type == "CInt16" || type == "CUInt16") {
      data_type = Operand::k16bit;
    } else if (type == "CInt32" || type == "CUInt32") {
      data_type = Operand::k32bit;
    } else if (type == "CInt64" || type == "CUInt64") {
      data_type = Operand::k64bit;
    } else if (type == "CDouble") {
      data_type = Operand::kDouble;
    }

    return {name.substr(0, colon), data_type};
  } else {
    return {name, data_type};
  }
}

BasicBlockBuilder::BasicBlockBuilder(jit::codegen::Environ* env, Function* func)
    : env_(env), func_(func) {
  cur_bb_ = GetBasicBlockByLabel("__main__");
  bbs_.push_back(cur_bb_);
}

void BasicBlockBuilder::AppendCode(const std::string& s) {
  auto it = s.begin();
  do {
    auto end = std::find(it, s.end(), '\n');
    std::string line(it, end);
    if (IsLabel(line)) {
      line.pop_back();
      auto next_bb = GetBasicBlockByLabel(line);
      if (cur_bb_->successors().size() < 2) {
        cur_bb_->addSuccessor(next_bb);
      }
      cur_bb_ = next_bb;
      bbs_.push_back(cur_bb_);
    } else {
      AppendCodeLine(line);
    }
    it = (end == s.end() ? s.end() : ++end);
  } while (it != s.end());
}

std::vector<std::string> BasicBlockBuilder::Tokenize(const std::string& s) {
  std::vector<std::string> tokens;

  auto it = s.begin();
  it = std::find_if(
      it, s.end(), [](auto c) -> bool { return c != ' ' && c != ','; });

  while (it != s.end()) {
    auto end = std::find_if(
        it, s.end(), [](auto c) -> bool { return c == ' ' || c == ','; });

    tokens.emplace_back(it, end);

    it = std::find_if(
        end, s.end(), [](auto c) -> bool { return c != ' ' && c != ','; });
  }

  return tokens;
}

Instruction* BasicBlockBuilder::createInstr(Instruction::Opcode opcode) {
  return cur_bb_->allocateInstr(opcode, cur_hir_instr_);
}

void BasicBlockBuilder::AppendCodeLine(const std::string& s) {
  // this function assumes that input is syntactically correct.
  // there is very limited syntax checking in the following parsing process.
  std::vector<std::string> tokens = Tokenize(s);

  const std::string& instr_str = tokens[0];
  if (instr_str == "Load") {
    auto instr = createInstr(Instruction::kMove);

    if (tokens.size() == 3) {
      instr->allocateAddressInput(reinterpret_cast<void*>(stoull(tokens[2])));
    } else {
      CreateInstrIndirect(instr, tokens[2], stoull(tokens[3]));
    }
    CreateInstrOutput(instr, tokens[1]);
  } else if (instr_str == "LoadArg") {
    JIT_CHECK(tokens.size() == 3, "expected 3 args");
    auto instr = createInstr(Instruction::kBind);

    auto loc = env_->arg_locations[stoull(tokens[2])];

    CreateInstrOutput(instr, tokens[1]);
    instr->allocatePhyRegOrStackInput(loc);
  } else if (instr_str == "Store") {
    auto instr = createInstr(Instruction::kMove);

    JIT_CHECK(
        tokens.size() == 3 || tokens.size() == 4, "Syntax error for Store");

    if (IsConstant(tokens[1])) {
      CreateInstrImmediateInput(instr, tokens[1]);
    } else {
      CreateInstrInput(instr, tokens[1]);
    }
    if (tokens.size() == 3) {
      instr->output()->setMemoryAddress(
          reinterpret_cast<void*>(stoull(tokens[2])));
    } else {
      CreateInstrIndirectOutput(instr, tokens[2], stoull(tokens[3]));
    }
    instr->output()->setDataType(instr->getInput(0)->dataType());
  } else if (instr_str == "Move") {
    JIT_CHECK(tokens.size() == 3, "Syntax error for Move.");
    JIT_CHECK(
        !IsConstant(tokens[1]), "Syntax error for Move: %s", tokens[1].c_str());
    auto instr = createInstr(Instruction::kMove);

    if (IsConstant(tokens[2])) {
      CreateInstrImmediateInput(instr, tokens[2]);
    } else {
      CreateInstrInput(instr, tokens[2]);
    }
    CreateInstrOutput(instr, tokens[1]);
  } else if (instr_str == "Return") {
    auto instr = createInstr(Instruction::kReturn);

    CreateInstrInput(instr, tokens[1]);
  } else if (instr_str == "Convert") {
    auto instr = createInstr(Instruction::kSext);

    if (IsConstant(tokens[2])) {
      CreateInstrImmediateInput(instr, tokens[2]);
    } else {
      CreateInstrInput(instr, tokens[2]);
    }

    CreateInstrOutput(instr, tokens[1]);
  } else if (instr_str == "ConvertUnsigned") {
    auto instr = createInstr(Instruction::kZext);

    if (IsConstant(tokens[2])) {
      CreateInstrImmediateInput(instr, tokens[2]);
    } else {
      CreateInstrInput(instr, tokens[2]);
    }

    CreateInstrOutput(instr, tokens[1]);
  } else if (
      instr_str == "Add" || instr_str == "Sub" || instr_str == "And" ||
      instr_str == "Xor" || instr_str == "Or" || instr_str == "LShift" ||
      instr_str == "RShift" || instr_str == "RShiftUn" || instr_str == "Mul" ||
      instr_str == "Div" || instr_str == "DivUn" || instr_str == "Equal" ||
      instr_str == "NotEqual" || instr_str == "GreaterThanSigned" ||
      instr_str == "LessThanSigned" || instr_str == "GreaterThanEqualSigned" ||
      instr_str == "LessThanEqualSigned" ||
      instr_str == "GreaterThanUnsigned" || instr_str == "LessThanUnsigned" ||
      instr_str == "GreaterThanEqualUnsigned" ||
      instr_str == "LessThanEqualUnsigned" || instr_str == "Fadd" ||
      instr_str == "Fsub" || instr_str == "Fmul" || instr_str == "Fdiv") {
    Instruction* instr = nullptr;
    if (instr_str == "Add") {
      instr = createInstr(Instruction::kAdd);
    } else if (instr_str == "Sub") {
      instr = createInstr(Instruction::kSub);
    } else if (instr_str == "And") {
      instr = createInstr(Instruction::kAnd);
    } else if (instr_str == "Xor") {
      instr = createInstr(Instruction::kXor);
    } else if (instr_str == "Or") {
      instr = createInstr(Instruction::kOr);
    } else if (instr_str == "LShift") {
      instr = createInstr(Instruction::kLShift);
    } else if (instr_str == "RShift") {
      instr = createInstr(Instruction::kRShift);
    } else if (instr_str == "RShiftUn") {
      instr = createInstr(Instruction::kRShiftUn);
    } else if (instr_str == "Mul") {
      instr = createInstr(Instruction::kMul);
    } else if (instr_str == "Equal") {
      instr = createInstr(Instruction::kEqual);
    } else if (instr_str == "NotEqual") {
      instr = createInstr(Instruction::kNotEqual);
    } else if (instr_str == "GreaterThanSigned") {
      instr = createInstr(Instruction::kGreaterThanSigned);
    } else if (instr_str == "LessThanSigned") {
      instr = createInstr(Instruction::kLessThanSigned);
    } else if (instr_str == "GreaterThanEqualSigned") {
      instr = createInstr(Instruction::kGreaterThanEqualSigned);
    } else if (instr_str == "LessThanEqualSigned") {
      instr = createInstr(Instruction::kLessThanEqualSigned);
    } else if (instr_str == "GreaterThanUnsigned") {
      instr = createInstr(Instruction::kGreaterThanUnsigned);
    } else if (instr_str == "LessThanUnsigned") {
      instr = createInstr(Instruction::kLessThanUnsigned);
    } else if (instr_str == "GreaterThanEqualUnsigned") {
      instr = createInstr(Instruction::kGreaterThanEqualUnsigned);
    } else if (instr_str == "LessThanEqualUnsigned") {
      instr = createInstr(Instruction::kLessThanEqualUnsigned);
    } else if (instr_str == "Fadd") {
      instr = createInstr(Instruction::kFadd);
    } else if (instr_str == "Fsub") {
      instr = createInstr(Instruction::kFsub);
    } else if (instr_str == "Fmul") {
      instr = createInstr(Instruction::kFmul);
    } else if (instr_str == "Fdiv") {
      instr = createInstr(Instruction::kFdiv);
    } else if (instr_str == "Div") {
      instr = createInstr(Instruction::kDiv);
    } else if (instr_str == "DivUn") {
      instr = createInstr(Instruction::kDivUn);
    } else {
      JIT_CHECK(false, "Unknown LIR instruction: %s", instr_str);
    }

    for (size_t i = 2; i < tokens.size(); i++) {
      if (IsConstant(tokens[i])) {
        CreateInstrImmediateInput(instr, tokens[i]);
      } else {
        CreateInstrInput(instr, tokens[i]);
      }
    }
    CreateInstrOutput(instr, tokens[1]);
  } else if (instr_str == "Negate") {
    auto instr = createInstr(Instruction::kNegate);
    if (IsConstant(tokens[2])) {
      CreateInstrImmediateInput(instr, tokens[2]);
    } else {
      CreateInstrInput(instr, tokens[2]);
    }
    CreateInstrOutput(instr, tokens[1]);
  } else if (instr_str == "Invert") {
    auto instr = createInstr(Instruction::kInvert);
    if (IsConstant(tokens[2])) {
      CreateInstrImmediateInput(instr, tokens[2]);
    } else {
      CreateInstrInput(instr, tokens[2]);
    }
    CreateInstrOutput(instr, tokens[1]);
  } else if (
      instr_str == "Call" || instr_str == "Vectorcall" ||
      instr_str == "Invoke") {
    bool is_invoke = (instr_str == "Invoke");
    bool is_vector_call = (instr_str == "Vectorcall");

    auto instr = createInstr(
        is_vector_call ? Instruction::kVectorCall : Instruction::kCall);

    for (size_t i = is_invoke ? 1 : 2; i < tokens.size(); i++) {
      if (IsConstant(tokens[i])) {
        CreateInstrImmediateInput(instr, tokens[i]);
      } else {
        CreateInstrInput(instr, tokens[i]);
      }
    }

    if (!is_invoke) {
      CreateInstrOutput(instr, tokens[1]);
    }
  } else if (instr_str == "CondBranch") {
    auto instr = createInstr(Instruction::kCondBranch);

    auto cond = tokens[1];
    if (IsConstant(cond)) {
      CreateInstrImmediateInput(instr, cond);
    } else {
      CreateInstrInput(instr, cond);
    }
  } else if (instr_str == "JumpIf") {
    // the difference between CondBranch and JumpIf is that the
    // arguments of CondBranch is HIR basic block ids, while those
    // of JumpIf are label names.
    // TODO: we can merge CondBranch and JumpIf by translating
    // HIR basic block ids into label names.
    auto instr = createInstr(Instruction::kCondBranch);
    auto cond = tokens[1];
    if (IsConstant(cond)) {
      CreateInstrImmediateInput(instr, cond);
    } else {
      CreateInstrInput(instr, cond);
    }
    auto true_bb = GetBasicBlockByLabel(tokens[2]);
    auto false_bb = GetBasicBlockByLabel(tokens[3]);

    cur_bb_->addSuccessor(true_bb);
    cur_bb_->addSuccessor(false_bb);
  } else if (instr_str == "Branch") {
    createInstr(Instruction::kBranch);
  } else if (instr_str == "BranchB" || instr_str == "BranchC") {
    createInstr(Instruction::kBranchB);
    auto succ_bb = GetBasicBlockByLabel(tokens[1]);
    cur_bb_->addSuccessor(succ_bb);
  } else if (instr_str == "BranchNZ") {
    createInstr(Instruction::kBranchNZ);
    auto succ_bb = GetBasicBlockByLabel(tokens[1]);
    cur_bb_->addSuccessor(succ_bb);
  } else if (instr_str == "BitTest") {
    auto instr = createInstr(Instruction::kBitTest);
    CreateInstrInput(instr, tokens[1]);
    CreateInstrImmediateInput(instr, tokens[2]);
  } else if (instr_str == "Inc" || instr_str == "Dec") {
    auto instr =
        createInstr(instr_str == "Inc" ? Instruction::kInc : Instruction::kDec);
    CreateInstrInput(instr, tokens[1]);
  } else if (instr_str == "Guard") {
    auto instr = createInstr(Instruction::kGuard);

    enum InstrGuardKind guard_kind;
    const std::string& kind = tokens[1];
    if (kind == "NotNull") {
      guard_kind = InstrGuardKind::kNotNull;
    } else if (kind == "NotNegative") {
      guard_kind = InstrGuardKind::kNotNegative;
    } else if (kind == "NotNone") {
      guard_kind = InstrGuardKind::kNotNone;
    } else if (kind == "AlwaysFail") {
      guard_kind = InstrGuardKind::kAlwaysFail;
    } else if (kind == "Is") {
      guard_kind = InstrGuardKind::kIs;
    } else {
      JIT_CHECK(false, "unknown check kind: {}", kind);
    }
    instr->allocateImmediateInput(guard_kind);
    CreateInstrImmediateInput(instr, tokens[2]);

    for (size_t i = 3; i < tokens.size(); i++) {
      if (tokens[i] == "reg:edx") {
        Operand* opnd = instr->allocatePhyRegisterInput(PhyLocation::RDX);
        opnd->setDataType(OperandBase::k32bit);
      } else if (IsConstant(tokens[i])) {
        CreateInstrImmediateInput(instr, tokens[i]);
      } else {
        CreateInstrInput(instr, tokens[i]);
      }
    }
  } else if (instr_str == "Phi") {
    auto instr = createInstr(Instruction::kPhi);

    JIT_CHECK((tokens.size() & 1) == 0, "Expected even number of tokens");
    for (size_t i = 2; i < tokens.size() - 1; i += 2) {
      instr->allocateLabelInput(
          reinterpret_cast<BasicBlock*>(stoull(tokens[i])));
      CreateInstrInput(instr, tokens[i + 1]);
    }
    CreateInstrOutput(instr, tokens[1]);
  } else if (
      instr_str == "YieldInitial" || instr_str == "YieldValue" ||
      instr_str == "YieldFrom" || instr_str == "YieldFromSkipInitialSend") {
    Instruction* instr;
    if (instr_str == "YieldInitial") {
      instr = createInstr(Instruction::kYieldInitial);
    } else if (instr_str == "YieldValue") {
      instr = createInstr(Instruction::kYieldValue);
    } else if (instr_str == "YieldFromSkipInitialSend") {
      instr = createInstr(Instruction::kYieldFromSkipInitialSend);
    } else {
      instr = createInstr(Instruction::kYieldFrom);
    }
    CreateInstrOutput(instr, tokens[1]);
    for (size_t tok_n = 2; tok_n < tokens.size() - 1; tok_n++) {
      CreateInstrInput(instr, tokens[tok_n]);
    }
    CreateInstrImmediateInput(instr, tokens[tokens.size() - 1]);
  } else {
    JIT_CHECK(false, "Unknown LIR instruction: %s", instr_str);
  }
}

BasicBlock* BasicBlockBuilder::GetBasicBlockByLabel(const std::string& label) {
  auto iter = label_to_bb_.find(label);
  if (iter == label_to_bb_.end()) {
    auto bb = func_->allocateBasicBlock();
    label_to_bb_.emplace(label, bb);
    return bb;
  }

  return iter->second;
}

void BasicBlockBuilder::CreateInstrImmediateInput(
    Instruction* instr,
    const std::string& val_type) {
  std::string sval;
  Operand::DataType type;
  std::tie(sval, type) = GetIdAndType(val_type);
  instr->allocateImmediateInput(stoull(sval), type);
}

Instruction* BasicBlockBuilder::getDefInstr(const std::string& name) {
  auto def_instr = map_get(env_->output_map, name, nullptr);

  if (def_instr == nullptr) {
    // the output has to be copy propagated.
    auto iter = env_->copy_propagation_map.find(name);
    const char* prop_name = nullptr;
    while (iter != env_->copy_propagation_map.end()) {
      prop_name = iter->second.c_str();
      iter = env_->copy_propagation_map.find(prop_name);
    }

    if (prop_name != nullptr) {
      def_instr = map_get(env_->output_map, prop_name, nullptr);
    }
  }

  return def_instr;
}

void BasicBlockBuilder::CreateInstrInput(
    Instruction* instr,
    const std::string& name_size) {
  auto name = GetId(name_size);

  auto def_instr = getDefInstr(name);
  auto operand = instr->allocateLinkedInput(def_instr);

  // if def_instr is still nullptr, it means that the output is defined
  // later in the function. This can happen when the function has a
  // backward edge.
  if (def_instr == nullptr) {
    // we need to fix later
    env_->operand_to_fix[name].push_back(operand);
  }
}

void BasicBlockBuilder::CreateInstrOutput(
    Instruction* instr,
    const std::string& name_size) {
  std::string name;
  Operand::DataType data_type;
  std::tie(name, data_type) = GetIdAndType(name_size);

  auto pair = env_->output_map.emplace(name, instr);
  JIT_DCHECK(
      pair.second,
      "Multiple outputs with the same name (%s)- HIR is not in SSA form.",
      name);
  auto output = instr->output();
  output->setVirtualRegister();
  output->setDataType(data_type);
}

void BasicBlockBuilder::CreateInstrIndirect(
    Instruction* instr,
    const std::string& name_size,
    int offset) {
  auto name = GetId(name_size);
  auto def_instr = getDefInstr(name);

  auto indirect = instr->allocateMemoryIndirectInput(def_instr, offset);

  if (def_instr == nullptr) {
    auto ind_opnd = indirect->getMemoryIndirect()->getBaseRegOperand();
    JIT_DCHECK(
        ind_opnd->isLinked(), "Should not have generated unlinked operand.");
    env_->operand_to_fix[name].push_back(static_cast<LinkedOperand*>(ind_opnd));
  }
}

void BasicBlockBuilder::CreateInstrIndirectOutput(
    Instruction* instr,
    const std::string& name_size,
    int offset) {
  auto name = GetId(name_size);
  auto def_instr = getDefInstr(name);

  auto output = instr->output();
  output->setMemoryIndirect(def_instr, offset);

  if (def_instr == nullptr) {
    auto ind_opnd = output->getMemoryIndirect()->getBaseRegOperand();
    JIT_DCHECK(
        ind_opnd->isLinked(), "Should not have generated unlinked operand.");
    env_->operand_to_fix[name].push_back(static_cast<LinkedOperand*>(ind_opnd));
  }
}
} // namespace lir
} // namespace jit
