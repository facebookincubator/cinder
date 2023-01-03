// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/lir/operand.h"
#include "Jit/util.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jit {
namespace hir {
class Instr;
}

namespace lir {

class BasicBlock;

enum class FlagEffects {
  kNone,
  kSet,
  kInvalidate,
};

#define FOREACH_INSTR_TYPE(X)                                                \
  /* name, <inputs live across> <flag effects>, <opnd_size_type>, <out use   \
   * reg>, <in use reg>, <is essential> */                                   \
  /* Bind is not used to generate any machine code. Its sole      */         \
  /* purpose is to associate a physical register with a predefined */        \
  /* value to virtual register for register allocator. */                    \
  X(Bind)                                                                    \
  X(Nop)                                                                     \
  X(Call, false, FlagEffects::kInvalidate, kAlways64, 1, {}, 1)              \
  X(VectorCall, true, FlagEffects::kInvalidate, kAlways64, 1, {1}, 1)        \
  X(Guard, true, FlagEffects::kInvalidate, kDefault, 1, {0, 0, 1, 1}, 1)     \
  X(DeoptPatchpoint, true, FlagEffects::kInvalidate, kDefault, 0, {1, 1}, 1) \
  X(Sext)                                                                    \
  X(Zext)                                                                    \
  X(Negate, false, FlagEffects::kSet, kOut)                                  \
  X(Invert, false, FlagEffects::kNone, kOut)                                 \
  X(Add, false, FlagEffects::kSet, kOut, 1, {1})                             \
  X(Sub, false, FlagEffects::kSet, kOut, 1, {1})                             \
  X(And, false, FlagEffects::kSet, kOut, 1, {1})                             \
  X(Xor, false, FlagEffects::kSet, kOut, 1, {1})                             \
  X(Div, false, FlagEffects::kSet, kDefault, 1, {1})                         \
  X(DivUn, false, FlagEffects::kSet, kDefault, 1, {1})                       \
  X(Mul, false, FlagEffects::kSet, kOut, 1, {1})                             \
  X(Or, false, FlagEffects::kSet, kOut, 1, {1})                              \
  X(Fadd, true, FlagEffects::kNone, kAlways64, 1, {1, 1})                    \
  X(Fsub, true, FlagEffects::kNone, kAlways64, 1, {1, 1})                    \
  X(Fmul, true, FlagEffects::kNone, kAlways64, 1, {1, 1})                    \
  X(Fdiv, true, FlagEffects::kNone, kAlways64, 1, {1, 1})                    \
  X(LShift, false, FlagEffects::kSet)                                        \
  X(RShift, false, FlagEffects::kSet)                                        \
  X(RShiftUn, false, FlagEffects::kSet)                                      \
  X(Test, false, FlagEffects::kSet, kDefault, 0, {1, 1})                     \
  X(Test32, false, FlagEffects::kSet, kDefault, 0, {1, 1})                   \
  X(Equal, false, FlagEffects::kSet, kDefault, 1, {1, 1})                    \
  X(NotEqual, false, FlagEffects::kSet, kDefault, 1, {1, 1})                 \
  X(GreaterThanSigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})        \
  X(LessThanSigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})           \
  X(GreaterThanEqualSigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})   \
  X(LessThanEqualSigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})      \
  X(GreaterThanUnsigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})      \
  X(LessThanUnsigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})         \
  X(GreaterThanEqualUnsigned, false, FlagEffects::kSet, kDefault, 1, {1, 1}) \
  X(LessThanEqualUnsigned, false, FlagEffects::kSet, kDefault, 1, {1, 1})    \
  X(Cmp, false, FlagEffects::kSet, kOut, 1, {1, 1})                          \
  X(Lea, false, FlagEffects::kNone, kAlways64, 1, {1, 1})                    \
  X(LoadArg, false, FlagEffects::kNone, kAlways64)                           \
  X(Exchange, false, FlagEffects::kNone, kAlways64, 1, {1, 1})               \
  X(Move, false, FlagEffects::kNone, kOut)                                   \
  X(Push, false, FlagEffects::kNone, kDefault, 1, {}, 1)                     \
  X(Pop, false, FlagEffects::kNone, kDefault, 0, {}, 1)                      \
  X(Cdq, false, FlagEffects::kNone, kDefault, 1, {}, 1)                      \
  X(Cwd, false, FlagEffects::kNone, kDefault, 1, {}, 1)                      \
  X(Cqo, false, FlagEffects::kNone, kDefault, 1, {}, 1)                      \
  X(BatchDecref, false, FlagEffects::kInvalidate, kDefault, 1, {1})          \
  X(Branch)                                                                  \
  X(BranchNZ)                                                                \
  X(BranchZ)                                                                 \
  X(BranchA)                                                                 \
  X(BranchB)                                                                 \
  X(BranchAE)                                                                \
  X(BranchBE)                                                                \
  X(BranchG)                                                                 \
  X(BranchL)                                                                 \
  X(BranchGE)                                                                \
  X(BranchLE)                                                                \
  X(BranchC)                                                                 \
  X(BranchNC)                                                                \
  X(BranchO)                                                                 \
  X(BranchNO)                                                                \
  X(BranchS)                                                                 \
  X(BranchNS)                                                                \
  X(BranchE)                                                                 \
  X(BitTest, false, FlagEffects::kSet, kDefault, 1, {1})                     \
  X(Inc, false, FlagEffects::kSet)                                           \
  X(Dec, false, FlagEffects::kSet)                                           \
  X(CondBranch, false, FlagEffects::kInvalidate, kDefault, 0, {1})           \
  X(Phi)                                                                     \
  X(Return, false, FlagEffects::kInvalidate)                                 \
  X(MovZX)                                                                   \
  X(MovSX)                                                                   \
  X(MovSXD)                                                                  \
  X(YieldInitial, true, FlagEffects::kInvalidate, kDefault, 0, {}, 1)        \
  X(YieldFrom, true, FlagEffects::kInvalidate, kDefault, 0, {}, 1)           \
  X(YieldFromSkipInitialSend,                                                \
    true,                                                                    \
    FlagEffects::kInvalidate,                                                \
    kDefault,                                                                \
    0,                                                                       \
    {},                                                                      \
    1)                                                                       \
  X(YieldFromHandleStopAsyncIteration,                                       \
    true,                                                                    \
    FlagEffects::kInvalidate,                                                \
    kDefault,                                                                \
    0,                                                                       \
    {},                                                                      \
    1)                                                                       \
  X(YieldValue, true, FlagEffects::kInvalidate, kDefault, 0, {}, 1)

// Instruction class defines instructions in LIR.
// Every instruction can have no more than one output, but arbitrary
// number of inputs. The instruction logically has no output also
// has an output data member with the type kNone.
class Instruction {
 public:
  // instruction type
  enum Opcode : int {
    kNone = -1,
#define INSTR_DECL_TYPE(v, ...) k##v,
    FOREACH_INSTR_TYPE(INSTR_DECL_TYPE)
#undef INSTR_DECL_TYPE
  };

  static constexpr const char* kOpcodeNames[] = {
#define INSTR_DECL_TYPE(v, ...) #v,
      FOREACH_INSTR_TYPE(INSTR_DECL_TYPE)
#undef INSTR_DECL_TYPE
  };

#define COUNT_INSTR(...) +1
  static constexpr size_t kNumOpcodes = FOREACH_INSTR_TYPE(COUNT_INSTR);
#undef COUNT_INSTR

#define DECL_OPCODE_TEST(v, ...) \
  bool is##v() const {           \
    return opcode() == k##v;     \
  }
  FOREACH_INSTR_TYPE(DECL_OPCODE_TEST)
#undef DECL_OPCODE_TEST

  Instruction(BasicBlock* basic_block, Opcode opcode, const hir::Instr* origin);

  // Only copies simple fields (opcode_, basic_block_, origin_) from instr.
  // The output_ only has its simple fields copied.
  // The inputs are not copied.
  Instruction(BasicBlock* bb, Instruction* instr, const hir::Instr* origin);

  int id() const {
    return id_;
  }

  Operand* output() {
    return &output_;
  }
  const Operand* output() const {
    return &output_;
  }

  const hir::Instr* origin() const {
    return origin_;
  }

  size_t getNumInputs() const {
    return inputs_.size();
  }

  void setNumInputs(size_t n) {
    inputs_.resize(n);
  }

  size_t getNumOutputs() const {
    return output_.type() == OperandBase::kNone ? 0 : 1;
  }

  OperandBase* getInput(size_t i) {
    return inputs_.at(i).get();
  }

  const OperandBase* getInput(size_t i) const {
    return inputs_.at(i).get();
  }

  Operand* allocateImmediateInput(
      uint64_t n,
      OperandBase::DataType data_type = OperandBase::k64bit);
  Operand* allocateFPImmediateInput(double n);
  LinkedOperand* allocateLinkedInput(Instruction* def_instr);
  Operand* allocatePhyRegisterInput(int loc) {
    return allocateOperand(&Operand::setPhyRegister, loc);
  }
  Operand* allocateStackInput(int stack) {
    return allocateOperand(&Operand::setStackSlot, stack);
  }
  Operand* allocatePhyRegOrStackInput(int loc) {
    return allocateOperand(&Operand::setPhyRegOrStackSlot, loc);
  }
  Operand* allocateAddressInput(void* address) {
    return allocateOperand(&Operand::setMemoryAddress, address);
  }
  Operand* allocateLabelInput(BasicBlock* block) {
    return allocateOperand(&Operand::setBasicBlock, block);
  }

  template <typename... Args>
  Operand* allocateMemoryIndirectInput(Args&&... args) {
    auto operand = std::make_unique<Operand>(this);
    auto opnd = operand.get();
    operand->setMemoryIndirect(std::forward<Args>(args)...);
    inputs_.push_back(std::move(operand));

    return opnd;
  }

  // add operands to the instruction. The arguments can be one
  // of the following:
  // - [Out]PhyReg(phyreg, size): a physical register
  // - [Out]Imm(imm, size): an immediate
  // - [Out]Stack(slot, size): a stack slot
  // - [Out]PhyRegStack(phyreg/stack, size): a physical register or stack slot
  // - [Out]Lbl(Basicblock): a basic block target
  // - VReg(instr), OutVReg(size): a virtual register
  // the arguments with the names prefixed with `Out` are output operands.
  // the output operand must be the first argument of this function.
  template <typename FirstT, typename... T>
  Instruction* addOperands(FirstT&& first_arg, T&&... args) {
    static_assert(
        !(std::decay_t<decltype(args)>::is_output || ... || false),
        "output must be the first argument.");

    using FT = std::decay_t<FirstT>;

    if constexpr (std::is_same_v<FT, PhyRegStack>) {
      allocatePhyRegOrStackInput(first_arg.value)
          ->setDataType(first_arg.data_type);
    } else if constexpr (std::is_same_v<FT, Imm>) {
      allocateImmediateInput(first_arg.value)->setDataType(first_arg.data_type);
    } else if constexpr (std::is_same_v<FT, FPImm>) {
      allocateFPImmediateInput(first_arg.value)
          ->setDataType(OperandBase::kDouble);
    } else if constexpr (std::is_same_v<FT, Lbl>) {
      allocateLabelInput(first_arg.value);
    } else if constexpr (std::is_same_v<FT, VReg>) {
      allocateLinkedInput(first_arg.value);
    } else if constexpr (std::is_same_v<FT, Ind>) {
      allocateMemoryIndirectInput(
          first_arg.base,
          first_arg.index,
          first_arg.multiplier,
          first_arg.offset);
    } else if constexpr (std::is_same_v<FT, OutPhyRegStack>) {
      output()->setPhyRegOrStackSlot(first_arg.value);
      output()->setDataType(first_arg.data_type);
    } else if constexpr (std::is_same_v<FT, OutImm>) {
      output()->setConstant(first_arg.value);
      output()->setDataType(first_arg.data_type);
    } else if constexpr (std::is_same_v<FT, OutFPImm>) {
      output()->setFPConstant(first_arg.value);
      output()->setDataType(OperandBase::kDouble);
    } else if constexpr (std::is_same_v<FT, OutLbl>) {
      output()->setBasicBlock(first_arg.value);
    } else if constexpr (std::is_same_v<FT, OutVReg>) {
      output()->setVirtualRegister();
      output()->setDataType(first_arg.data_type);
    } else if constexpr (std::is_same_v<FT, OutInd>) {
      output()->setMemoryIndirect(
          first_arg.base,
          first_arg.index,
          first_arg.multiplier,
          first_arg.offset);
    } else {
      static_assert(!sizeof(FT*), "Bad argument type.");
    }

    return addOperands(std::forward<T>(args)...);
  }

  constexpr Instruction* addOperands() {
    return this;
  }

  void setbasicblock(BasicBlock* bb) {
    basic_block_ = bb;
  }

  BasicBlock* basicblock() {
    return basic_block_;
  }
  const BasicBlock* basicblock() const {
    return basic_block_;
  }

  Opcode opcode() const {
    return opcode_;
  }

  std::string opname() const {
    return kOpcodeNames[opcode_];
  }

  void setOpcode(Opcode opcode) {
    opcode_ = opcode;
  }

  template <typename Func>
  void foreachInputOperand(const Func& f) const {
    for (size_t i = 0; i < this->getNumInputs(); i++) {
      auto operand = getInput(i);
      f(operand);
    }
  }

  template <typename Func>
  void foreachInputOperand(const Func& f) {
    for (size_t i = 0; i < this->getNumInputs(); i++) {
      auto operand = getInput(i);
      f(operand);
    }
  }

  // replace the input operand at index with operand.
  void replaceInputOperand(size_t index, std::unique_ptr<OperandBase> operand) {
    inputs_[index] = std::move(operand);
  }

  std::unique_ptr<OperandBase> removeInputOperand(size_t index) {
    auto opnd = std::move(inputs_.at(index));
    inputs_.erase(inputs_.begin() + index);
    return opnd;
  }

  // Release the input operand at index from the instruction without
  // deallocating it. The original index of inputs_ will be left with
  // a null std::unique_ptr, which is supposed be removed from inputs_
  // by an operation to follow.
  std::unique_ptr<OperandBase> releaseInputOperand(size_t index) {
    auto& operand = inputs_.at(index);
    operand->releaseFromInstr();
    return std::move(inputs_.at(index));
  }

  OperandBase* appendInputOperand(std::unique_ptr<OperandBase> operand) {
    auto opnd = operand.get();
    opnd->assignToInstr(this);
    inputs_.push_back(std::move(operand));
    return opnd;
  }

  OperandBase* prependInputOperand(std::unique_ptr<OperandBase> operand) {
    auto opnd = operand.get();
    opnd->assignToInstr(this);
    inputs_.insert(inputs_.begin(), std::move(operand));
    return opnd;
  }

  // get the operand associated to a given predecessor in a phi instruction
  // returns nullptr if not found.
  OperandBase* getOperandByPredecessor(const BasicBlock* pred) {
    auto index = getOperandIndexByPredecessor(pred);
    return index == -1 ? nullptr : inputs_.at(index).get();
  }

  int getOperandIndexByPredecessor(const BasicBlock* pred) const {
    JIT_DCHECK(opcode_ == kPhi, "The current instruction must be Phi.");
    size_t num_inputs = getNumInputs();
    for (size_t i = 0; i < num_inputs; i += 2) {
      if (getInput(i)->getBasicBlock() == pred) {
        return i + 1;
      }
    }
    return -1;
  }

  const OperandBase* getOperandByPredecessor(const BasicBlock* pred) const {
    return const_cast<Instruction*>(this)->getOperandByPredecessor(pred);
  }

  bool getOutputPhyRegUse() const;
  bool getInputPhyRegUse(size_t i) const;
  // Should input registers live across the instruction until it
  // finish execution? Some instructions need this such as Guard
  // instruction, whose its inputs may be needed to reify the frame
  // upon deopt. Other instructions do not need it, such as Add, etc.,
  // so the input registers can be used for other purposes  e.g., allocated for
  // the output even before they finish execution.
  bool inputsLiveAcross() const;

  bool isCompare() const {
    switch (opcode_) {
      case kEqual:
      case kNotEqual:
      case kGreaterThanSigned:
      case kLessThanSigned:
      case kGreaterThanEqualSigned:
      case kLessThanEqualSigned:
      case kGreaterThanUnsigned:
      case kLessThanUnsigned:
      case kGreaterThanEqualUnsigned:
      case kLessThanEqualUnsigned:
        return true;
      default:
        return false;
    }
  }

  bool isBranchCC() const {
    switch (opcode_) {
      case kBranchC:
      case kBranchNC:
      case kBranchO:
      case kBranchNO:
      case kBranchS:
      case kBranchNS:
      case kBranchZ:
      case kBranchNZ:
      case kBranchA:
      case kBranchB:
      case kBranchBE:
      case kBranchAE:
      case kBranchL:
      case kBranchG:
      case kBranchLE:
      case kBranchGE:
      case kBranchE:
        return true;
      default:
        return false;
    }
  }

  bool isAnyBranch() const {
    return (opcode_ == kCondBranch) || isBranchCC();
  }

  bool isTerminator() const {
    switch (opcode_) {
      case kReturn:
        return true;
      default:
        return false;
    }
  }

  bool isAnyYield() const {
    switch (opcode_) {
      case kYieldFrom:
      case kYieldFromHandleStopAsyncIteration:
      case kYieldFromSkipInitialSend:
      case kYieldInitial:
      case kYieldValue:
        return true;
      default:
        return false;
    }
  }

#define CASE_FLIP(op1, op2) \
  case op1:                 \
    return op2;             \
  case op2:                 \
    return op1;

  // negate the branch condition:
  // e.g. A >= B -> !(A < B)
  static Opcode negateBranchCC(Opcode opcode) {
    switch (opcode) {
      CASE_FLIP(kBranchC, kBranchNC)
      CASE_FLIP(kBranchO, kBranchNO)
      CASE_FLIP(kBranchS, kBranchNS)
      CASE_FLIP(kBranchZ, kBranchNZ)
      CASE_FLIP(kBranchA, kBranchBE)
      CASE_FLIP(kBranchB, kBranchAE)
      CASE_FLIP(kBranchL, kBranchGE)
      CASE_FLIP(kBranchG, kBranchLE)
      default:
        JIT_CHECK(false, "Not a conditional branch opcode.");
    }
  }

  // flipping the direction of comparison:
  // e.g. A >= B -> B <= A
  static Opcode flipBranchCCDirection(Opcode opcode) {
    switch (opcode) {
      CASE_FLIP(kBranchA, kBranchB)
      CASE_FLIP(kBranchAE, kBranchBE)
      CASE_FLIP(kBranchL, kBranchG)
      CASE_FLIP(kBranchLE, kBranchGE)
      default:
        JIT_CHECK(false, "Unable to flip direction for opcode.");
    }
  }

  static Opcode flipComparisonDirection(Opcode opcode) {
    switch (opcode) {
      CASE_FLIP(kGreaterThanEqualSigned, kLessThanEqualSigned)
      CASE_FLIP(kGreaterThanEqualUnsigned, kLessThanEqualUnsigned)
      CASE_FLIP(kGreaterThanSigned, kLessThanSigned)
      CASE_FLIP(kGreaterThanUnsigned, kLessThanUnsigned)
      case kEqual:
        return kEqual;
      case kNotEqual:
        return kNotEqual;
      default:
        JIT_CHECK(false, "Unable to flip direction for comparison opcode.");
    }
  }

#undef CASE_FLIP

  static Opcode compareToBranchCC(Opcode opcode) {
    switch (opcode) {
      case kEqual:
        return kBranchZ;
      case kNotEqual:
        return kBranchNZ;
      case kGreaterThanUnsigned:
        return kBranchA;
      case kLessThanUnsigned:
        return kBranchB;
      case kGreaterThanEqualUnsigned:
        return kBranchAE;
      case kLessThanEqualUnsigned:
        return kBranchBE;
      case kGreaterThanSigned:
        return kBranchG;
      case kLessThanSigned:
        return kBranchL;
      case kGreaterThanEqualSigned:
        return kBranchGE;
      case kLessThanEqualSigned:
        return kBranchLE;
      default:
        JIT_CHECK(false, "Not a compare opcode.");
    }
  }

  void print() const;

 private:
  int id_;
  Opcode opcode_;
  Operand output_;
  std::vector<std::unique_ptr<OperandBase>> inputs_;

  BasicBlock* basic_block_;

  const hir::Instr* origin_;

  template <typename FType, typename... AType>
  Operand* allocateOperand(FType&& set_func, AType&&... arg) {
    auto operand = std::make_unique<Operand>(this);
    auto opnd = operand.get();
    (opnd->*set_func)(std::forward<AType>(arg)...);
    inputs_.push_back(std::move(operand));
    return opnd;
  }

  // used in parser, expect unique id
  void setId(int id) {
    id_ = id;
  }

  friend class Parser;
};

// Instruction Guard specific
enum InstrGuardKind { kNotZero, kNotNegative, kAlwaysFail, kIs, kHasType };

// an instruction property type specifying how its operand sizes
// are determined.
enum OperandSizeType {
  kDefault, // every operand uses its own size
  kAlways64, // every operand uses 64-bit size
  kOut // every operand uses output size or first input operand (when there is
       // no output)
};

// This class defines instruction properties for different types of
// instructions.
class InstrProperty {
 public:
  struct InstrInfo;
  static InstrInfo& getProperties(Instruction::Opcode opcode);
  static InstrInfo& getProperties(const Instruction* instr) {
    return getProperties(instr->opcode());
  }

 private:
  static std::vector<InstrInfo> prop_map_;
};

// Initialize instruction properties
#define BEGIN_INSTR_PROPERTY_FIELD struct InstrProperty::InstrInfo {
#define END_INSTR_PROPERTY_FIELD \
  }                              \
  ;
#define FIELD_DEFAULT(__t, __n, __d) __t __n{__d};
#define FIELD_NO_DEFAULT(__t, __n) __t __n;

// clang-format off
// This table contains definitions of all the instruction property field.
// `is_essential` indicates that a given instruction can have memory effects not
// captured by its operands. We maintain the invariant that all instructions
// without operands have `may_store` set.
BEGIN_INSTR_PROPERTY_FIELD
  FIELD_NO_DEFAULT(std::string, name)
  FIELD_DEFAULT(bool, inputs_live_across, false)
  FIELD_DEFAULT(FlagEffects, flag_effects, FlagEffects::kNone)
  FIELD_DEFAULT(OperandSizeType, opnd_size_type, kDefault)
  FIELD_DEFAULT(bool, output_phy_use, true)
  FIELD_DEFAULT(std::vector<int>, input_phy_uses, std::vector<int>{})
  FIELD_DEFAULT(bool, is_essential, false)
END_INSTR_PROPERTY_FIELD
// clang-format on

} // namespace lir
} // namespace jit
