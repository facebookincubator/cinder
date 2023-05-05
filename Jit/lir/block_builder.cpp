// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

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

namespace jit::lir {

static inline std::string GetId(const std::string& s) {
  size_t colon;
  if ((colon = s.find(':')) != std::string::npos) {
    return s.substr(0, colon);
  }
  return s;
}

BasicBlockBuilder::BasicBlockBuilder(jit::codegen::Environ* env, Function* func)
    : env_(env), func_(func) {
  cur_bb_ = GetBasicBlockByLabel("__main__");
  bbs_.push_back(cur_bb_);
}

std::size_t BasicBlockBuilder::makeDeoptMetadata() {
  JIT_CHECK(
      cur_hir_instr_ != nullptr,
      "Can't make DeoptMetadata with a nullptr HIR instruction");
  auto deopt_base = cur_hir_instr_->asDeoptBase();
  JIT_CHECK(deopt_base != nullptr, "Current HIR instruction can't deopt");

  if (!cur_deopt_metadata_.has_value()) {
    cur_deopt_metadata_ = env_->rt->addDeoptMetadata(
        DeoptMetadata::fromInstr(*deopt_base, env_->code_rt));
  }
  return cur_deopt_metadata_.value();
}

BasicBlock* BasicBlockBuilder::allocateBlock(std::string_view label) {
  auto [it, inserted] = label_to_bb_.emplace(std::string{label}, nullptr);
  if (inserted) {
    it->second = func_->allocateBasicBlock();
  }
  return it->second;
}

void BasicBlockBuilder::appendBlock(BasicBlock* block) {
  if (cur_bb_->successors().size() < 2) {
    cur_bb_->addSuccessor(block);
  }
  switchBlock(block);
}

void BasicBlockBuilder::switchBlock(BasicBlock* block) {
  bbs_.push_back(block);
  cur_bb_ = block;
}

void BasicBlockBuilder::AppendLabel(std::string_view s) {
  appendBlock(allocateBlock(s));
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
      std::optional<std::string> name =
          symbolize(reinterpret_cast<void*>(helper_addr));
      if (name.has_value()) {
        JIT_LOG("Call to function %s.", *name);
      } else {
        JIT_LOG("Call to function at %s.", tokens[dest_idx]);
      }
    }
  }

  auto instr = createInstr(
      is_vector_call ? Instruction::kVectorCall : Instruction::kCall);

  for (size_t i = is_invoke ? 1 : 2; i < tokens.size(); i++) {
    if (IsConstant(tokens[i])) {
      CreateInstrImmediateInputFromStr(instr, tokens[i]);
    } else {
      CreateInstrInputFromStr(instr, tokens[i]);
    }
  }

  if (!is_invoke) {
    CreateInstrOutputFromStr(instr, tokens[1]);
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
      CreateInstrImmediateInputFromStr(instr, tokens[i]);
    } else {
      CreateInstrInputFromStr(instr, tokens[i]);
    }
  }

  if (has_output) {
    CreateInstrOutputFromStr(instr, tokens[1]);
  }
}

namespace {
void createYield(
    BasicBlockBuilder& bldr,
    const std::vector<std::string>& tokens,
    Instruction::Opcode opcode) {
  JIT_CHECK(
      tokens.size() >= 4,
      "Yield variants expect at least (opcode, output, num_live_regs, "
      "deopt_idx)");
  Instruction* instr = bldr.createInstr(opcode);
  size_t tok_n = 1;
  // Output
  bldr.CreateInstrOutputFromStr(instr, tokens[tok_n++]);
  // Live registers
  while (tok_n < tokens.size() - 2) {
    bldr.CreateInstrInputFromStr(instr, tokens[tok_n++]);
  }
  // Number of live registers
  bldr.CreateInstrImmediateInputFromStr(instr, tokens[tok_n++]);
  // DeoptMetadata index
  bldr.CreateInstrImmediateInputFromStr(instr, tokens[tok_n++]);
};
} // namespace

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
           bldr.CreateInstrIndirectFromStr(
               instr, tokens[2], std::stoull(tokens[3], nullptr, 0));
         }
         bldr.CreateInstrOutputFromStr(instr, tokens[1]);
       }},
      {"LoadArg",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         JIT_CHECK(tokens.size() == 3, "expected 3 args");
         auto instr = bldr.createInstr(Instruction::kLoadArg);

         bldr.CreateInstrImmediateInputFromStr(instr, tokens[2]);
         bldr.CreateInstrOutputFromStr(instr, tokens[1]);
       }},
      {"Store",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kMove);

         JIT_CHECK(
             tokens.size() == 3 || tokens.size() == 4,
             "Syntax error for Store");

         if (bldr.IsConstant(tokens[1])) {
           bldr.CreateInstrImmediateInputFromStr(instr, tokens[1]);
         } else {
           bldr.CreateInstrInputFromStr(instr, tokens[1]);
         }
         if (tokens.size() == 3) {
           instr->output()->setMemoryAddress(
               reinterpret_cast<void*>(std::stoull(tokens[2], nullptr, 0)));
         } else {
           bldr.CreateInstrIndirectOutputFromStr(
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
           bldr.CreateInstrImmediateInputFromStr(instr, tokens[2]);
         } else {
           bldr.CreateInstrInputFromStr(instr, tokens[2]);
         }
         bldr.CreateInstrOutputFromStr(instr, tokens[1]);
       }},
      {"Lea",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         JIT_CHECK(tokens.size() == 4, "Syntax error for LoadAddress.");
         JIT_CHECK(
             !bldr.IsConstant(tokens[1]),
             "Syntax error for LoadAddress: %s",
             tokens[1].c_str());
         auto instr = bldr.createInstr(Instruction::kLea);

         bldr.CreateInstrIndirectFromStr(
             instr, tokens[2], std::stoull(tokens[3], nullptr, 0));
         bldr.CreateInstrOutputFromStr(instr, tokens[1]);
       }},
      {"Return",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kReturn);

         bldr.CreateInstrInputFromStr(instr, tokens[1]);
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
           bldr.CreateInstrImmediateInputFromStr(instr, cond);
         } else {
           bldr.CreateInstrInputFromStr(instr, cond);
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
           bldr.CreateInstrImmediateInputFromStr(instr, cond);
         } else {
           bldr.CreateInstrInputFromStr(instr, cond);
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
      {"BranchO",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchO);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchNO",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchNO);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchS",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchS);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchNS",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchNS);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BranchE",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createInstr(Instruction::kBranchE);
         auto succ_bb = bldr.GetBasicBlockByLabel(tokens[1]);
         bldr.cur_bb_->addSuccessor(succ_bb);
       }},
      {"BitTest",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kBitTest);
         bldr.CreateInstrInputFromStr(instr, tokens[1]);
         bldr.CreateInstrImmediateInputFromStr(instr, tokens[2]);
       }},
      {"Inc",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kInc);
         bldr.CreateInstrInputFromStr(instr, tokens[1]);
       }},
      {"Dec",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kDec);
         bldr.CreateInstrInputFromStr(instr, tokens[1]);
       }},
      {"Guard",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         static UnorderedMap<std::string_view, InstrGuardKind> guardKindMap = {
             {"AlwaysFail", InstrGuardKind::kAlwaysFail},
             {"HasType", InstrGuardKind::kHasType},
             {"Is", InstrGuardKind::kIs},
             {"NotNegative", InstrGuardKind::kNotNegative},
             {"NotZero", InstrGuardKind::kNotZero},
             {"Zero", InstrGuardKind::kZero},
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
         bldr.CreateInstrImmediateInputFromStr(instr, tokens[2]);

         for (size_t i = 3; i < tokens.size(); i++) {
           if (tokens[i] == "reg:edx") {
             Operand* opnd = instr->allocatePhyRegisterInput(PhyLocation::RDX);
             opnd->setDataType(OperandBase::k32bit);
           } else if (tokens[i] == "reg:xmm1") {
             Operand* opnd = instr->allocatePhyRegisterInput(PhyLocation::XMM1);
             opnd->setDataType(OperandBase::kDouble);
           } else if (bldr.IsConstant(tokens[i])) {
             bldr.CreateInstrImmediateInputFromStr(instr, tokens[i]);
           } else {
             bldr.CreateInstrInputFromStr(instr, tokens[i]);
           }
         }
       }},
      {"DeoptPatchpoint",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(
             Instruction::kDeoptPatchpoint, false, -1, tokens);
       }},
      {"Phi",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kPhi);

         JIT_CHECK((tokens.size() & 1) == 0, "Expected even number of tokens");
         for (size_t i = 2; i < tokens.size() - 1; i += 2) {
           instr->allocateLabelInput(reinterpret_cast<BasicBlock*>(
               std::stoull(tokens[i], nullptr, 0)));
           bldr.CreateInstrInputFromStr(instr, tokens[i + 1]);
         }
         bldr.CreateInstrOutputFromStr(instr, tokens[1]);
       }},
      {"Test32",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         auto instr = bldr.createInstr(Instruction::kTest32);
         bldr.CreateInstrInputFromStr(instr, tokens[1]);
         bldr.CreateInstrInputFromStr(instr, tokens[2]);
       }},
      {"YieldInitial",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         createYield(bldr, tokens, Instruction::kYieldInitial);
       }},
      {"YieldValue",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         createYield(bldr, tokens, Instruction::kYieldValue);
       }},
      {"YieldFromSkipInitialSend",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         createYield(bldr, tokens, Instruction::kYieldFromSkipInitialSend);
       }},
      {"YieldFromHandleStopAsyncIteration",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         createYield(
             bldr, tokens, Instruction::kYieldFromHandleStopAsyncIteration);
       }},
      {"YieldFrom",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         createYield(bldr, tokens, Instruction::kYieldFrom);
       }},
      {"BatchDecref",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kBatchDecref, false, -1, tokens);
       }},
      {"Select",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>& tokens) {
         bldr.createBasicInstr(Instruction::kSelect, true, 3, tokens);
       }},
      {"Unreachable",
       [](BasicBlockBuilder& bldr, const std::vector<std::string>&) {
         bldr.createInstr(Instruction::kUnreachable);
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

void BasicBlockBuilder::CreateInstrImmediateInputFromStr(
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

Instruction* BasicBlockBuilder::getDefInstr(const hir::Register* reg) {
  return getDefInstr(reg->name());
}

void BasicBlockBuilder::CreateInstrInput(
    Instruction* instr,
    const std::string& name) {
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
    const std::string& name,
    Operand::DataType data_type) {
  auto pair = env_->output_map.emplace(name, instr);
  JIT_DCHECK(
      pair.second,
      "Multiple outputs with the same name (%s)- HIR is not in SSA form.",
      name);
  auto output = instr->output();
  output->setVirtualRegister();
  output->setDataType(data_type);
}

void BasicBlockBuilder::CreateInstrInputFromStr(
    Instruction* instr,
    const std::string& name_size) {
  std::string name = GetId(name_size);
  CreateInstrInput(instr, name);
}

void BasicBlockBuilder::CreateInstrOutputFromStr(
    Instruction* instr,
    const std::string& name_size) {
  std::string name;
  Operand::DataType data_type;
  std::tie(name, data_type) = GetIdAndType(name_size);
  CreateInstrOutput(instr, name, data_type);
}

void BasicBlockBuilder::CreateInstrIndirectFromStr(
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

void BasicBlockBuilder::CreateInstrIndirect(
    Instruction* instr,
    const std::string& base,
    const std::string& index,
    int multiplier,
    int offset) {
  JIT_CHECK(multiplier >= 0 && multiplier <= 3, "bad multiplier");
  auto base_instr = getDefInstr(base);
  auto index_instr = getDefInstr(index);
  auto indirect = instr->allocateMemoryIndirectInput(
      base_instr, index_instr, multiplier, offset);
  if (base_instr == nullptr) {
    auto ind_opnd = indirect->getMemoryIndirect()->getBaseRegOperand();
    JIT_DCHECK(
        ind_opnd->isLinked(), "Should not have generated unlinked operand.");
    env_->operand_to_fix[base].push_back(static_cast<LinkedOperand*>(ind_opnd));
  }
  if (index_instr == nullptr) {
    auto ind_opnd = indirect->getMemoryIndirect()->getIndexRegOperand();
    JIT_DCHECK(
        ind_opnd->isLinked(), "Should not have generated unlinked operand.");
    env_->operand_to_fix[index].push_back(
        static_cast<LinkedOperand*>(ind_opnd));
  }
}

void BasicBlockBuilder::CreateInstrIndirectOutputFromStr(
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

} // namespace jit::lir
