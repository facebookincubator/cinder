// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/containers.h"
#include "Jit/lir/x86_64.h"
#include "Jit/log.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <tuple>
#include <variant>

// This file defines operand classes in LIR.
// OperandBase class is the base of the two types of operands:
//   - Operand: a normal operand that has type, size, and value, which
//     is used for instruction outputs and immediate input operand.
//   - LinkedOperand: this type of operand can only be input of an
//     instruction, which links to an output operand in a different
//     instruction, representing a def-use relationship.
namespace jit::lir {

class BasicBlock;
class Instruction;
class OperandBase;
class Operand;
class LinkedOperand;
class MemoryIndirect;

// base class of operand
// defines the interface that all the operands must have.
class OperandBase {
 public:
  explicit OperandBase(Instruction* parent) : parent_instr_(parent) {}
  OperandBase(const OperandBase& ob)
      : parent_instr_(ob.parent_instr_), last_use_(ob.last_use_) {}
  virtual ~OperandBase() {}

  /* operand types:
   *   - None:   the operand is not used.
   *   - Vreg:   the operand is in a virtual register (not yet
   *             allocated to a physical location);
   *   - Reg:    the operand is allocated to a physical register;
   *   - Stack:  the operand is allocated to a memory stack slot;
   *   - Mem:    the operand is allocated to a memory address;
   *   - Ind:    the operand is a memory indirect reference
   *   - Imm:    the operand is an immediate value;
   *   - Lbl:    the operand refers to a basic block.
   */
#define FOREACH_OPERAND_TYPE(X) \
  X(None)                       \
  X(Vreg)                       \
  X(Reg)                        \
  X(Stack)                      \
  X(Mem)                        \
  X(Ind)                        \
  X(Imm)                        \
  X(Label)

  enum Type {
#define OPERAND_DECL_TYPE(v, ...) k##v,
    FOREACH_OPERAND_TYPE(OPERAND_DECL_TYPE)
#undef OPERAND_DECL_TYPE
  };

#define OPERAND_DECL_TYPE_TEST(v, ...) \
  bool is##v() const {                 \
    return type() == Type::k##v;       \
  }
  FOREACH_OPERAND_TYPE(OPERAND_DECL_TYPE_TEST)
#undef OPERAND_DECL_TYPE_TEST

  virtual uint64_t getConstant() const = 0;
  virtual double getFPConstant() const = 0;
  virtual int getPhyRegister() const = 0;
  virtual int getStackSlot() const = 0;
  virtual int getPhyRegOrStackSlot() const = 0;
  virtual void* getMemoryAddress() const = 0;
  virtual MemoryIndirect* getMemoryIndirect() const = 0;
  virtual BasicBlock* getBasicBlock() const = 0;

  // get the def operand of the current operand
  // if the current operand is a def (of type Operand), return itself.
  virtual Operand* getDefine() = 0;
  virtual const Operand* getDefine() const = 0;

#define FOREACH_OPERAND_DATA_TYPE(X) \
  X(8bit)                            \
  X(16bit)                           \
  X(32bit)                           \
  X(64bit)                           \
  X(Double)                          \
  X(Object)

  enum DataType : unsigned char {
#define DECL_DATA_TYPE_ENUM(s, ...) k##s,
    FOREACH_OPERAND_DATA_TYPE(DECL_DATA_TYPE_ENUM)
#undef DECL_DATA_TYPE_ENUM
  };

  virtual DataType dataType() const = 0;
  int sizeInBits() const {
    auto s = dataType();
    switch (s) {
      case k8bit:
        return 8;
      case k16bit:
        return 16;
      case k32bit:
        return 32;
      case k64bit:
      case kDouble:
      case kObject:
        return 64;
      default:
        Py_UNREACHABLE();
    }
  }

  const char* getSizeName() const {
    switch (dataType()) {
#define DATA_TYPE_NAME_CASE(s, ...) \
  case k##s:                        \
    return #s;
      FOREACH_OPERAND_DATA_TYPE(DATA_TYPE_NAME_CASE)
#undef DATA_TYPE_NAME_CASE
    }
    Py_UNREACHABLE();
  }

  virtual Type type() const = 0;
  virtual bool isLinked() const = 0;

  Instruction* instr() {
    return parent_instr_;
  }
  const Instruction* instr() const {
    return parent_instr_;
  }
  void releaseFromInstr() {
    parent_instr_ = nullptr;
  }
  void assignToInstr(Instruction* instr) {
    parent_instr_ = instr;
  }

  bool isFp() const {
    return dataType() == kDouble;
  }

  bool isXmm() const {
    return PhyLocation(getPhyRegister()).is_fp_register();
  }

  bool isLastUse() const {
    return last_use_;
  }

  void setLastUse() {
    last_use_ = true;
  }

 private:
  Instruction* parent_instr_;
  bool last_use_{false};
};

// memory reference: [base_reg + index_reg * (2^index_multiplier) + offset]
class MemoryIndirect {
 public:
  explicit MemoryIndirect(Instruction* parent) : parent_(parent) {}

  void setMemoryIndirect(PhyLocation base, int32_t offset = 0) {
    setMemoryIndirect(base, PhyLocation::REG_INVALID, 0, offset);
  }

  void setMemoryIndirect(
      PhyLocation base,
      PhyLocation index_reg,
      uint8_t multipler) {
    setMemoryIndirect(base, index_reg, multipler, 0);
  }

  void setMemoryIndirect(Instruction* base, int32_t offset) {
    setMemoryIndirect(base, nullptr, 0, offset);
  }

  void setMemoryIndirect(
      std::variant<Instruction*, PhyLocation> base,
      std::variant<Instruction*, PhyLocation> index,
      uint8_t multipler,
      int32_t offset) {
    setBaseIndex(base_reg_, base);
    setBaseIndex(index_reg_, index);
    multipler_ = multipler;
    offset_ = offset;
  }

  OperandBase* getBaseRegOperand() {
    return base_reg_.get();
  }

  OperandBase* getIndexRegOperand() {
    return index_reg_.get();
  }

  OperandBase* getBaseRegOperand() const {
    return base_reg_.get();
  }

  OperandBase* getIndexRegOperand() const {
    return index_reg_.get();
  }

  uint8_t getMultipiler() const {
    return multipler_;
  }
  int32_t getOffset() const {
    return offset_;
  }

 private:
  Instruction* parent_{nullptr};
  std::unique_ptr<OperandBase> base_reg_;
  std::unique_ptr<OperandBase> index_reg_;
  uint8_t multipler_{0};
  int32_t offset_{0};

  void setBaseIndex(
      std::unique_ptr<OperandBase>& base_index_opnd,
      Instruction* base_index);
  void setBaseIndex(
      std::unique_ptr<OperandBase>& base_index_opnd,
      PhyLocation base_index);

  void setBaseIndex(
      std::unique_ptr<OperandBase>& base_index_opnd,
      std::variant<Instruction*, PhyLocation> base_index);
};

// operand class
class Operand : public OperandBase {
 public:
  explicit Operand(Instruction* parent) : OperandBase(parent) {}

  // Only copies simple fields (type and data type) from operand.
  // The value_ field is not copied.
  Operand(Instruction* parent, Operand* operand)
      : OperandBase(parent),
        type_(operand->type_),
        data_type_(operand->data_type_) {}

  virtual ~Operand() {}

  Operand(Instruction* parent, DataType data_type, Type type, uint64_t data)
      : OperandBase(parent), type_(type), data_type_(data_type) {
    value_ = data;
  }

  Operand(Instruction* parent, Type type, double data)
      : OperandBase(parent), type_(type), data_type_(kDouble) {
    value_ = bit_cast<uint64_t>(data);
  }

  uint64_t getConstant() const override {
    return std::get<uint64_t>(value_);
  }

  double getFPConstant() const override {
    auto value = std::get<uint64_t>(value_);
    return bit_cast<double>(value);
  }

  void setConstant(uint64_t n, DataType data_type = k64bit) {
    type_ = kImm;
    value_ = n;
    data_type_ = data_type;
  }

  void setFPConstant(double n) {
    type_ = kImm;
    data_type_ = kDouble;
    value_ = bit_cast<uint64_t>(n);
  }

  int getPhyRegister() const override {
    JIT_DCHECK(
        type_ == kReg, "Unable to get physical register from the operand.");
    return std::get<int>(value_);
  }

  void setPhyRegister(int reg) {
    type_ = kReg;
    value_ = reg;
  }

  int getStackSlot() const override {
    JIT_DCHECK(type_ == kStack, "Unable to get a memory stack slot.");
    return std::get<int>(value_);
  }

  void setStackSlot(int slot) {
    type_ = kStack;
    value_ = slot;
  }

  void setPhyRegOrStackSlot(int loc) {
    if (loc < 0) {
      setStackSlot(loc);
    } else {
      setPhyRegister(loc);
    }
  }

  int getPhyRegOrStackSlot() const override {
    switch (type_) {
      case kReg:
        return getPhyRegister();
      case kStack:
        return getStackSlot();
      default:
        JIT_DCHECK(
            false,
            "Unable to get a physical register or a memory stack slot from the "
            "operand");
        break;
    }
    return -1;
  }

  void* getMemoryAddress() const override {
    JIT_DCHECK(type_ == kMem, "Unable to get a memory address.");
    return std::get<void*>(value_);
  }

  void setMemoryAddress(void* addr) {
    type_ = kMem;
    value_ = addr;
  }

  MemoryIndirect* getMemoryIndirect() const override {
    JIT_DCHECK(type_ == kInd, "Unable to get a memory indirect.");
    return std::get<std::unique_ptr<MemoryIndirect>>(value_).get();
  }

  template <typename... Args>
  void setMemoryIndirect(Args&&... args) {
    type_ = kInd;
    auto ind = std::make_unique<MemoryIndirect>(instr());
    ind->setMemoryIndirect(std::forward<Args>(args)...);
    value_ = std::move(ind);
  }

  void setVirtualRegister() {
    type_ = kVreg;
  }

  BasicBlock* getBasicBlock() const override {
    JIT_DCHECK(type_ == kLabel, "Unable to get a basic block address.");
    return std::get<BasicBlock*>(value_);
  }

  const Operand* getDefine() const override {
    return this;
  }
  Operand* getDefine() override {
    return this;
  }

  void setBasicBlock(BasicBlock* block) {
    type_ = kLabel;
    data_type_ = kObject;
    value_ = block;
  }

  DataType dataType() const override {
    return data_type_;
  }
  void setDataType(DataType data_type) {
    data_type_ = data_type;
  }

  Type type() const override {
    return type_;
  }

  void setNone() {
    type_ = kNone;
  }

  bool isLinked() const override {
    return false;
  }

  int numUses() const {
    return uses_.size();
  }
  void addUse(LinkedOperand* use);
  void removeUse(LinkedOperand* use);

 private:
  Type type_{kNone};
  DataType data_type_{kObject};

  std::variant<
      uint64_t,
      int,
      void*,
      BasicBlock*,
      std::unique_ptr<MemoryIndirect>>
      value_;

  UnorderedSet<LinkedOperand*> uses_;
};

// Linked operand
// This type of operand is essentially a pointer to the instruction.
// The operand takes the value of the output of the instruction.
class LinkedOperand : public OperandBase {
 public:
  LinkedOperand(Instruction* parent, Instruction* def);
  virtual ~LinkedOperand() {}

  bool isLinked() const override {
    return true;
  }

  Operand* getLinkedOperand() {
    return def_opnd_;
  }
  const Operand* getLinkedOperand() const {
    return def_opnd_;
  }

  Instruction* getLinkedInstr() {
    return def_opnd_->instr();
  }
  const Instruction* getLinkedInstr() const {
    return def_opnd_->instr();
  }

  void setLinkedInstr(Instruction* def);

  uint64_t getConstant() const override {
    return def_opnd_->getConstant();
  }
  double getFPConstant() const override {
    return def_opnd_->getFPConstant();
  }
  int getPhyRegister() const override {
    return def_opnd_->getPhyRegister();
  }
  int getStackSlot() const override {
    return def_opnd_->getStackSlot();
  }
  int getPhyRegOrStackSlot() const override {
    return def_opnd_->getPhyRegOrStackSlot();
  }
  void* getMemoryAddress() const override {
    return def_opnd_->getMemoryAddress();
  }
  MemoryIndirect* getMemoryIndirect() const override {
    return def_opnd_->getMemoryIndirect();
  }
  BasicBlock* getBasicBlock() const override {
    return def_opnd_->getBasicBlock();
  }
  Operand* getDefine() override {
    return def_opnd_;
  }
  const Operand* getDefine() const override {
    return def_opnd_;
  }
  DataType dataType() const override {
    return def_opnd_->dataType();
  }
  Type type() const override {
    return def_opnd_->type();
  }

 private:
  friend class Operand;
  Operand* def_opnd_;
};

// OperandArg reqresents different operand data types, and is used as
// arguments to BasicBlock::allocateInstr* instructions. The latter
// will create the operands accordingly for the instructions after
// allocating them.
template <typename Type, bool Output>
struct OperandArg {
  explicit OperandArg(Type v, OperandBase::DataType dt = OperandBase::kObject)
      : value(v), data_type(dt) {}

  Type value;
  OperandBase::DataType data_type{OperandBase::kObject};
  static constexpr bool is_output = Output;
};

template <bool Output>
struct OperandArg<MemoryIndirect, Output> {
  using Reg = std::variant<Instruction*, PhyLocation>;

  explicit OperandArg(Reg b, OperandBase::DataType dt = OperandBase::kObject)
      : base(b), data_type(dt) {}
  explicit OperandArg(
      Reg b,
      int32_t off,
      OperandBase::DataType dt = OperandBase::kObject)
      : base(b), offset(off), data_type(dt) {}
  OperandArg(Reg b, Reg i, OperandBase::DataType dt = OperandBase::kObject)
      : base(b), index(i), data_type(dt) {}
  OperandArg(
      Reg b,
      Reg i,
      int32_t off,
      OperandBase::DataType dt = OperandBase::kObject)
      : base(b), index(i), offset(off), data_type(dt) {}
  OperandArg(
      Reg b,
      Reg i,
      uint8_t m,
      int32_t off,
      OperandBase::DataType dt = OperandBase::kObject)
      : base(b), index(i), multiplier(m), offset(off), data_type(dt) {}

  Reg base{PhyLocation::REG_INVALID};
  Reg index{PhyLocation::REG_INVALID};
  uint8_t multiplier{0};
  int32_t offset{0};
  OperandBase::DataType data_type{OperandBase::kObject};
  static constexpr bool is_output = Output;
};

template <>
struct OperandArg<void*, true> {
  OperandArg(const OperandBase::DataType& dt = OperandBase::kObject)
      : data_type(dt) {}

  OperandBase::DataType data_type{OperandBase::kObject};
  static constexpr bool is_output = true;
};

#define DECLARE_TYPE_ARG(__T, __V, __O) using __T = OperandArg<__V, __O>;

DECLARE_TYPE_ARG(PhyReg, PhyLocation, false)
DECLARE_TYPE_ARG(Imm, uint64_t, false)
DECLARE_TYPE_ARG(FPImm, double, false)
DECLARE_TYPE_ARG(Stk, PhyLocation, false)
DECLARE_TYPE_ARG(PhyRegStack, PhyLocation, false)
DECLARE_TYPE_ARG(Lbl, BasicBlock*, false)
DECLARE_TYPE_ARG(VReg, Instruction*, false)
DECLARE_TYPE_ARG(Ind, MemoryIndirect, false)

DECLARE_TYPE_ARG(OutPhyReg, PhyLocation, true)
DECLARE_TYPE_ARG(OutImm, uint64_t, true)
DECLARE_TYPE_ARG(OutFPImm, double, true)
DECLARE_TYPE_ARG(OutStk, PhyLocation, true)
DECLARE_TYPE_ARG(OutPhyRegStack, PhyLocation, true)
DECLARE_TYPE_ARG(OutLbl, BasicBlock*, true)
DECLARE_TYPE_ARG(OutDbl, double, true);
DECLARE_TYPE_ARG(OutInd, MemoryIndirect, true);
DECLARE_TYPE_ARG(OutVReg, void*, true);

} // namespace jit::lir
