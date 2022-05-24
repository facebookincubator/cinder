// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/block_builder.h"

#include "Jit/lir/generator.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/lir.h"
#include "Jit/util.h"

#include <dlfcn.h>

#include <sstream>

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// bbbuilder.cpp with some interfaces changes so that it works with the new
// LIR.

namespace jit {
namespace lir {

static inline std::string GetId(const std::string& s) {
  size_t colon;
  if ((colon = s.find(':')) != std::string::npos) {
    return s.substr(0, colon);
  }
  return s;
}

static inline std::pair<std::string, Operand::DataType> GetIdAndType(
    const std::string& name) {
  static UnorderedMap<std::string_view, Operand::DataType> typeMap = {
      {"CInt8", Operand::k8bit},
      {"CUInt8", Operand::k8bit},
      {"CBool", Operand::k8bit},
      {"CInt16", Operand::k16bit},
      {"CUInt16", Operand::k16bit},
      {"CInt32", Operand::k32bit},
      {"CUInt32", Operand::k32bit},
      {"CInt64", Operand::k64bit},
      {"CUInt64", Operand::k64bit},
      {"CDouble", Operand::kDouble},
  };
  size_t colon;
  Operand::DataType data_type = Operand::kObject;

  if ((colon = name.find(':')) != std::string::npos) {
    auto type = std::string_view(name).substr(colon + 1);
    auto t = typeMap.find(type);
    if (t != typeMap.end()) {
      data_type = t->second;
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

void BasicBlockBuilder::AppendLabel(const std::string& s) {
  auto next_bb = GetBasicBlockByLabel(s);
  if (cur_bb_->successors().size() < 2) {
    cur_bb_->addSuccessor(next_bb);
  }
  cur_bb_ = next_bb;
  bbs_.push_back(cur_bb_);
}

std::vector<std::string> BasicBlockBuilder::Tokenize(std::string_view s) {
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

void BasicBlockBuilder::createBasicCallInstr(
    const std::vector<std::string>& tokens,
    bool is_invoke,
    bool is_vector_call) {
  if (g_dump_c_helper) {
    size_t dest_idx = is_invoke ? 1 : 2;
    if (dest_idx < tokens.size() && IsConstant(tokens[dest_idx])) {
      std::string helper_id = GetId(tokens[dest_idx]);
      uint64_t helper_addr = std::stoull(helper_id, nullptr, 0);
      Dl_info helper_info;
      if (dladdr(reinterpret_cast<void*>(helper_addr), &helper_info) != 0 &&
          helper_info.dli_sname != NULL) {
        JIT_LOG("Call to function %s.", helper_info.dli_sname);
      } else {
        JIT_LOG("Call to function at %s.", tokens[dest_idx]);
      }
    }
  }

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
}

void BasicBlockBuilder::createBasicInstr(
    Instruction::Opcode opc,
    bool has_output,
    int arg_count,
    const std::vector<std::string>& tokens) {
  Instruction* instr = createInstr(opc);

  size_t input_base = has_output ? 2 : 1;
  if (arg_count != -1) {
    JIT_DCHECK(
        input_base + arg_count == tokens.size(),
        "Expected %i args to LIR instruction %i, got %i.",
        arg_count,
        (int)opc,
        int(tokens.size() - input_base));
  }

  for (size_t i = input_base; i < tokens.size(); i++) {
    if (IsConstant(tokens[i])) {
      CreateInstrImmediateInput(instr, tokens[i]);
    } else {
      CreateInstrInput(instr, tokens[i]);
    }
  }

  if (has_output) {
    CreateInstrOutput(instr, tokens[1]);
  }
}

void BasicBlockBuilder::AppendTokenizedCodeLine(
    const std::vector<std::string>& tokens) {
  // this function assumes that input is syntactically correct.
  // there is very limited syntax checking in the following parsing process.
  using InstrHandlerFunc =
      void (*)(BasicBlockBuilder&, const std::vector<std::string>&);
  static UnorderedMap<std::string_view, InstrHandlerFunc> instrHandlers = {
      {"Load",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kMove);

         if (tokens.size() == 3) {
           instr->allocateAddressInput(
               reinterpret_cast<void*>(std::stoull(tokens[2], nullptr, 0)));
         } else {
           bldr.CreateInstrIndirect(
               instr, tokens[2], std::stoull(tokens[3], nullptr, 0));
         }
         bldr.CreateInstrOutput(instr, tokens[1]);
       }},
      {"LoadArg",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         JIT_CHECK(tokens.size() == 3, "expected 3 args");
         auto instr = bldr.createInstr(Instruction::kLoadArg);

         bldr.CreateInstrImmediateInput(instr, tokens[2]);
         bldr.CreateInstrOutput(instr, tokens[1]);
       }},
      {"Store",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kMove);

         JIT_CHECK(
             tokens.size() == 3 || tokens.size() == 4,
             "Syntax error for Store");

         if (bldr.IsConstant(tokens[1])) {
           bldr.CreateInstrImmediateInput(instr, tokens[1]);
         } else {
           bldr.CreateInstrInput(instr, tokens[1]);
         }
         if (tokens.size() == 3) {
           instr->output()->setMemoryAddress(
               reinterpret_cast<void*>(std::stoull(tokens[2], nullptr, 0)));
         } else {
           bldr.CreateInstrIndirectOutput(
               instr, tokens[2], std::stoull(tokens[3], nullptr, 0));
         }
         instr->output()->setDataType(instr->getInput(0)->dataType());
       }},
      {"Move",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         JIT_CHECK(tokens.size() == 3, "Syntax error for Move.");
         JIT_CHECK(
             !bldr.IsConstant(tokens[1]),
             "Syntax error for Move: %s",
             tokens[1].c_str());
         auto instr = bldr.createInstr(Instruction::kMove);

         if (bldr.IsConstant(tokens[2])) {
           bldr.CreateInstrImmediateInput(instr, tokens[2]);
         } else {
           bldr.CreateInstrInput(instr, tokens[2]);
         }
         bldr.CreateInstrOutput(instr, tokens[1]);
       }},
      {"Lea",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         JIT_CHECK(tokens.size() == 4, "Syntax error for LoadAddress.");
         JIT_CHECK(
             !bldr.IsConstant(tokens[1]),
             "Syntax error for LoadAddress: %s",
             tokens[1].c_str());
         auto instr = bldr.createInstr(Instruction::kLea);

         bldr.CreateInstrIndirect(
             instr, tokens[2], std::stoull(tokens[3], nullptr, 0));
         bldr.CreateInstrOutput(instr, tokens[1]);
       }},
      {"Return",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kReturn);

         bldr.CreateInstrInput(instr, tokens[1]);
       }},
      {"Convert",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kSext, true, 1, tokens);
       }},
      {"ConvertUnsigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kZext, true, 1, tokens);
       }},
      {"Add",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kAdd, true, 2, tokens);
       }},
      {"Sub",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kSub, true, 2, tokens);
       }},
      {"And",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kAnd, true, 2, tokens);
       }},
      {"Xor",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kXor, true, 2, tokens);
       }},
      {"Or",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kOr, true, 2, tokens);
       }},
      {"LShift",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kLShift, true, 2, tokens);
       }},
      {"RShift",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kRShift, true, 2, tokens);
       }},
      {"RShiftUn",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kRShiftUn, true, 2, tokens);
       }},
      {"Mul",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kMul, true, 2, tokens);
       }},
      {"Equal",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kEqual, true, 2, tokens);
       }},
      {"NotEqual",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kNotEqual, true, 2, tokens);
       }},
      {"GreaterThanSigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kGreaterThanSigned, true, 2, tokens);
       }},
      {"LessThanSigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kLessThanSigned, true, 2, tokens);
       }},
      {"GreaterThanEqualSigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kGreaterThanEqualSigned, true, 2, tokens);
       }},
      {"LessThanEqualSigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kLessThanEqualSigned, true, 2, tokens);
       }},
      {"GreaterThanUnsigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kGreaterThanUnsigned, true, 2, tokens);
       }},
      {"LessThanUnsigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kLessThanUnsigned, true, 2, tokens);
       }},
      {"GreaterThanEqualUnsigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kGreaterThanEqualUnsigned, true, 2, tokens);
       }},
      {"LessThanEqualUnsigned",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kLessThanEqualUnsigned, true, 2, tokens);
       }},
      {"Fadd",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kFadd, true, 2, tokens);
       }},
      {"Fsub",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kFsub, true, 2, tokens);
       }},
      {"Fmul",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kFmul, true, 2, tokens);
       }},
      {"Fdiv",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kFdiv, true, 2, tokens);
       }},
      {"Div",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kDiv, true, 3, tokens);
       }},
      {"DivUn",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kDivUn, true, 3, tokens);
       }},
      {"Negate",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kNegate, true, 1, tokens);
       }},
      {"Invert",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kInvert, true, 1, tokens);
       }},
      {"Call",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicCallInstr(tokens, false, false);
       }},
      {"Vectorcall",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicCallInstr(tokens, false, true);
       }},
      {"Invoke",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicCallInstr(tokens, true, false);
       }},
      {"CondBranch",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kCondBranch);

         auto cond = tokens[1];
         if (bldr.IsConstant(cond)) {
           bldr.CreateInstrImmediateInput(instr, cond);
         } else {
           bldr.CreateInstrInput(instr, cond);
         }
       }},
      {"JumpIf",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         // the difference between CondBranch and JumpIf is that the
         // arguments of CondBranch is HIR basic block ids, while those
         // of JumpIf are label names.
         // TODO: we can merge CondBranch and JumpIf by translating
         // HIR basic block ids into label names.
         auto instr = bldr.createInstr(Instruction::kCondBranch);
         auto cond = tokens[1];
         if (bldr.IsConstant(cond)) {
           bldr.CreateInstrImmediateInput(instr, cond);
         } else {
           bldr.CreateInstrInput(instr, cond);
         }
         auto true_bb = bldr.GetBasicBlockByLabel(tokens[2]);
         auto false_bb = bldr.GetBasicBlockByLabel(tokens[3]);

         bldr.cur_bb_->addSuccessor(true_bb);
         bldr.cur_bb_->addSuccessor(false_bb);
       }},
      {"Branch",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>&) {
         bldr.createInstr(Instruction::kBranch);
       }},
      {"BranchB",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchB);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchC",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchB);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchNZ",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchNZ);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchC",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchC);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchNC",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchNC);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BitTest",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kBitTest);
         bldr.CreateInstrInput(instr, tokens[1]);
         bldr.CreateInstrImmediateInput(instr, tokens[2]);
       }},
      {"Inc",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kInc);
         bldr.CreateInstrInput(instr, tokens[1]);
       }},
      {"Dec",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kDec);
         bldr.CreateInstrInput(instr, tokens[1]);
       }},
      {"Guard",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         static UnorderedMap<std::string_view, InstrGuardKind> guardKindMap = {
             {"NotZero", InstrGuardKind::kNotZero},
             {"NotNegative", InstrGuardKind::kNotNegative},
             {"AlwaysFail", InstrGuardKind::kAlwaysFail},
             {"Is", InstrGuardKind::kIs},
             {"HasType", InstrGuardKind::kHasType},
         };
         auto instr = bldr.createInstr(Instruction::kGuard);

         enum InstrGuardKind guard_kind;
         const std::string& kind = tokens[1];
         auto k = guardKindMap.find(kind);
         if (k != guardKindMap.end()) {
           guard_kind = k->second;
         } else {
           JIT_CHECK(false, "unknown check kind: {}", kind);
         }
         instr->allocateImmediateInput(guard_kind);
         bldr.CreateInstrImmediateInput(instr, tokens[2]);

         for (size_t i = 3; i < tokens.size(); i++) {
           if (tokens[i] == "reg:edx") {
             Operand* opnd = instr->allocatePhyRegisterInput(PhyLocation::RDX);
             opnd->setDataType(OperandBase::k32bit);
           } else if (tokens[i] == "reg:xmm1") {
             Operand* opnd = instr->allocatePhyRegisterInput(PhyLocation::XMM1);
             opnd->setDataType(OperandBase::kDouble);
           } else if (bldr.IsConstant(tokens[i])) {
             bldr.CreateInstrImmediateInput(instr, tokens[i]);
           } else {
             bldr.CreateInstrInput(instr, tokens[i]);
           }
         }
       }},
      {"DeoptPatchpoint",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kDeoptPatchpoint, false, -1, tokens);
       }},
      {"Load2ndCallResult",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kMove);

         instr->allocatePhyRegisterInput(PhyLocation::RDX);
         bldr.CreateInstrOutput(instr, tokens[1]);
       }},
      {"Phi",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kPhi);

         JIT_CHECK((tokens.size() & 1) == 0, "Expected even number of tokens");
         for (size_t i = 2; i < tokens.size() - 1; i += 2) {
           instr->allocateLabelInput(reinterpret_cast<BasicBlock*>(
               std::stoull(tokens[i], nullptr, 0)));
           bldr.CreateInstrInput(instr, tokens[i + 1]);
         }
         bldr.CreateInstrOutput(instr, tokens[1]);
       }},
      {"YieldInitial",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         Instruction* instr = bldr.createInstr(Instruction::kYieldInitial);
         bldr.CreateInstrOutput(instr, tokens[1]);
         for (size_t tok_n = 2; tok_n < tokens.size() - 1; tok_n++) {
           bldr.CreateInstrInput(instr, tokens[tok_n]);
         }
         bldr.CreateInstrImmediateInput(instr, tokens[tokens.size() - 1]);
       }},
      {"YieldValue",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         Instruction* instr = bldr.createInstr(Instruction::kYieldValue);
         bldr.CreateInstrOutput(instr, tokens[1]);
         for (size_t tok_n = 2; tok_n < tokens.size() - 1; tok_n++) {
           bldr.CreateInstrInput(instr, tokens[tok_n]);
         }
         bldr.CreateInstrImmediateInput(instr, tokens[tokens.size() - 1]);
       }},
      {"YieldFromSkipInitialSend",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         Instruction* instr =
             bldr.createInstr(Instruction::kYieldFromSkipInitialSend);
         bldr.CreateInstrOutput(instr, tokens[1]);
         for (size_t tok_n = 2; tok_n < tokens.size() - 1; tok_n++) {
           bldr.CreateInstrInput(instr, tokens[tok_n]);
         }
         bldr.CreateInstrImmediateInput(instr, tokens[tokens.size() - 1]);
       }},
      {"YieldFromHandleStopAsyncIteration",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         Instruction* instr =
             bldr.createInstr(Instruction::kYieldFromHandleStopAsyncIteration);
         bldr.CreateInstrOutput(instr, tokens[1]);
         for (size_t tok_n = 2; tok_n < tokens.size() - 1; tok_n++) {
           bldr.CreateInstrInput(instr, tokens[tok_n]);
         }
         bldr.CreateInstrImmediateInput(instr, tokens[tokens.size() - 1]);
       }},
      {"YieldFrom",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         Instruction* instr = bldr.createInstr(Instruction::kYieldFrom);
         bldr.CreateInstrOutput(instr, tokens[1]);
         for (size_t tok_n = 2; tok_n < tokens.size() - 1; tok_n++) {
           bldr.CreateInstrInput(instr, tokens[tok_n]);
         }
         bldr.CreateInstrImmediateInput(instr, tokens[tokens.size() - 1]);
       }},
      {"BatchDecref",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kBatchDecref, false, -1, tokens);
       }},
  };

  const std::string& instr_str = tokens[0];
  auto hnd = instrHandlers.find(instr_str);
  if (hnd != instrHandlers.end()) {
    hnd->second(*this, tokens);
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
  if (type == Operand::kDouble) {
    instr->allocateImmediateInput(bit_cast<uint64_t>(stod(sval)), type);
  } else {
    instr->allocateImmediateInput(std::stoull(sval, nullptr, 0), type);
  }
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
  std::string name = GetId(name_size);
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
  if (name == "__native_frame_base") {
    instr->allocateMemoryIndirectInput(PhyLocation::RBP, offset);
    return;
  }
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
  if (name == "__native_frame_base") {
    instr->output()->setMemoryIndirect(PhyLocation::RBP, offset);
    return;
  }
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

void BasicBlockBuilder::SetBlockSection(
    const std::string& label,
    codegen::CodeSection section) {
  BasicBlock* block = GetBasicBlockByLabel(label);
  if (block == nullptr) {
    return;
  }
  block->setSection(section);
}
} // namespace lir
} // namespace jit
