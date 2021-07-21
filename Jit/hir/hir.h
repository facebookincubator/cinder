// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef JIT_HIR_H
#define JIT_HIR_H

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Jit/bytecode.h"
#include "Jit/hir/type.h"
#include "Jit/intrusive_list.h"
#include "Jit/jit_rt.h"
#include "Jit/ref.h"
#include "Jit/stack.h"
#include "Jit/util.h"
#include "Python.h"
#include "code.h"
#include "opcode.h"

namespace jit {
namespace hir {

/*
 * This file defines the high-level intermediate representation (HIR) used by
 * the JIT.
 *
 * The main goals for the IR are:
 * 1. Stay close to Python. The HIR is machine independent and tries to stay
 *    close to Python in order to enable optimizations that are easier to
 *    perform at a higher level of abstraction. For example, null checks for
 *    variable accesses are represented explicitly so that they may be
 *    optimized away when it can be statically determined that a variable is
 *    defined.
 * 2. Be as explicit as possible. The CPython bytecode has a large amount of
 *    implicit logic (e.g. refcounting, null checks). Making that logic
 *    explicit in the IR makes it possible to optimize away.
 * 3. Be easy to lower into a lower-level IR for code generation. It should be
 *    possible to lower the HIR into C or LLVM IR mechanically.
 *
 * Functions are converted into HIR by performing an abstract interpretation
 * over the function's bytecode.
 *
 * Functions are represented as a control flow graph of basic blocks. Each
 * basic block contains a list of instructions that ends in a
 * terminator. Instructions operate on an arbitrary set of variables and are
 * not in SSA form.
 */

class Instr;

// The IR operates on an infinite number of virtual registers.
class Register {
 public:
  explicit Register(int i) : id_(i) {}

  // An integer identifier for this register. This is unique per `Function`.
  int id() const {
    return id_;
  }

  // The type of this value. Only meaningful for SSA-form HIR.
  Type type() const {
    return type_;
  }
  void set_type(Type type) {
    type_ = type;
  }

  // Shorthand for checking the type of this Register.
  bool isA(Type type) const {
    return type_ <= type;
  }

  // The instruction that defined this value. Always set, but only meaningful
  // for SSA-form HIR.
  Instr* instr() const {
    return instr_;
  }
  void set_instr(Instr* instr) {
    instr_ = instr;
  }

  // A unique name for this value. This name has no connection to the original
  // Python program.
  const std::string& name() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(Register);

  Type type_{TTop};
  Instr* instr_{nullptr};
  int id_{-1};
  mutable std::string name_;
};

std::ostream& operator<<(std::ostream& os, const Register& reg);

// The refcount semantics of a value held in a Register.
enum class RefKind {
  // A PyObject* that is either null or points to an immortal object, and
  // doesn't need to be reference counted, or a primitive.
  kUncounted,
  // A PyObject* with a borrowed reference.
  kBorrowed,
  // A PyObject* that owns a reference.
  kOwned,
};

// The kind of value held in a Register.
enum class ValueKind {
  // A PyObject*.
  kObject,
  // A signed 64-bit integer.
  kSigned,
  // An unsigned 64-bit integer.
  kUnsigned,
  // A C bool.
  kBool,
  // A C Double
  kDouble,
};

std::ostream& operator<<(std::ostream& os, RefKind kind);

// An entry in the CPython block stack
struct ExecutionBlock {
  // The CPython opcode for the block
  int opcode;

  // Offset in the bytecode of the handler for this block
  int handler_off;

  // Level to pop the operand stack when the block is exited
  int stack_level;

  bool operator==(const ExecutionBlock& other) const {
    return (opcode == other.opcode) && (handler_off == other.handler_off) &&
        (stack_level == other.stack_level);
  }

  bool operator!=(const ExecutionBlock& other) const {
    return !(*this == other);
  }

  bool isTryBlock() const {
    return opcode == SETUP_FINALLY;
  }

  bool isAsyncForHeaderBlock(const BytecodeInstructionBlock& instrs) const {
    Py_ssize_t idx = handler_off / sizeof(_Py_CODEUNIT);
    return opcode == SETUP_FINALLY && instrs.at(idx).opcode() == END_ASYNC_FOR;
  }
};

using BlockStack = jit::Stack<ExecutionBlock>;
using OperandStack = jit::Stack<Register*>;

// The abstract state of the python frame
struct FrameState {
  FrameState() = default;
  explicit FrameState(int bc_off) : next_instr_offset(bc_off) {}
  FrameState(int bc_off, const OperandStack& os, const BlockStack& bs)
      : next_instr_offset(bc_off), stack(os), block_stack(bs) {}
  FrameState(const OperandStack& os, const BlockStack& bs)
      : next_instr_offset(0), stack(os), block_stack(bs) {}

  // The bytecode offset of the next instruction to be executed once control has
  // transferred to the interpreter.
  int next_instr_offset{0};

  // Local variables
  std::vector<Register*> locals;

  // Cells for cellvars (used by closures of inner functions) and freevars (our
  // closure)
  std::vector<Register*> cells;

  OperandStack stack;
  BlockStack block_stack;

  int instr_offset() const {
    return next_instr_offset - sizeof(_Py_CODEUNIT);
  }

  bool visitUses(const std::function<bool(Register*&)>& func) {
    for (auto& reg : stack) {
      if (!func(reg)) {
        return false;
      }
    }
    for (auto& reg : locals) {
      if (reg != nullptr && !func(reg)) {
        return false;
      }
    }
    for (auto& reg : cells) {
      if (reg != nullptr && !func(reg)) {
        return false;
      }
    }
    return true;
  }

  bool operator==(const FrameState& other) const {
    return (next_instr_offset == other.next_instr_offset) &&
        (stack == other.stack) && (block_stack == other.block_stack) &&
        (locals == other.locals) && (cells == other.cells);
  }

  bool operator!=(const FrameState& other) const {
    return !(*this == other);
  }

  bool hasTryBlock() const {
    for (auto& bse : block_stack) {
      if (bse.isTryBlock()) {
        return true;
      }
    }
    return false;
  }
};

#define FOREACH_OPCODE(V)       \
  V(Assign)                     \
  V(BinaryOp)                   \
  V(Branch)                     \
  V(BuildSlice)                 \
  V(BuildString)                \
  V(CallCFunc)                  \
  V(CallEx)                     \
  V(CallExKw)                   \
  V(CallMethod)                 \
  V(CallStatic)                 \
  V(CallStaticRetVoid)          \
  V(Cast)                       \
  V(CheckSequenceBounds)        \
  V(CheckExc)                   \
  V(CheckNeg)                   \
  V(CheckNone)                  \
  V(CheckVar)                   \
  V(CheckField)                 \
  V(CheckTuple)                 \
  V(ClearError)                 \
  V(Compare)                    \
  V(CompareBool)                \
  V(CondBranch)                 \
  V(CondBranchIterNotDone)      \
  V(CondBranchCheckType)        \
  V(Decref)                     \
  V(DeleteSubscr)               \
  V(Deopt)                      \
  V(DoubleBinaryOp)             \
  V(FillTypeAttrCache)          \
  V(FormatValue)                \
  V(GetIter)                    \
  V(GetTuple)                   \
  V(Guard)                      \
  V(GuardIs)                    \
  V(ImportFrom)                 \
  V(ImportName)                 \
  V(InPlaceOp)                  \
  V(Incref)                     \
  V(InitFunction)               \
  V(InitListTuple)              \
  V(InitialYield)               \
  V(IntBinaryOp)                \
  V(PrimitiveBox)               \
  V(PrimitiveCompare)           \
  V(IntConvert)                 \
  V(PrimitiveUnaryOp)           \
  V(PrimitiveUnbox)             \
  V(InvokeIterNext)             \
  V(InvokeMethod)               \
  V(IsInstance)                 \
  V(InvokeStaticFunction)       \
  V(IsErrStopAsyncIteration)    \
  V(IsNegativeAndErrOccurred)   \
  V(IsTruthy)                   \
  V(ListAppend)                 \
  V(ListExtend)                 \
  V(LoadArrayItem)              \
  V(LoadArg)                    \
  V(LoadAttr)                   \
  V(LoadAttrSpecial)            \
  V(LoadAttrSuper)              \
  V(LoadCellItem)               \
  V(LoadConst)                  \
  V(LoadCurrentFunc)            \
  V(LoadEvalBreaker)            \
  V(LoadField)                  \
  V(LoadFunctionIndirect)       \
  V(LoadGlobalCached)           \
  V(LoadGlobal)                 \
  V(LoadMethod)                 \
  V(LoadMethodSuper)            \
  V(LoadTupleItem)              \
  V(LoadTypeAttrCacheItem)      \
  V(LoadVarObjectSize)          \
  V(MakeCheckedDict)            \
  V(MakeCell)                   \
  V(MakeDict)                   \
  V(MakeFunction)               \
  V(MakeListTuple)              \
  V(MakeSet)                    \
  V(MakeTupleFromList)          \
  V(MergeDictUnpack)            \
  V(MergeSetUnpack)             \
  V(Phi)                        \
  V(Raise)                      \
  V(RaiseStatic)                \
  V(RaiseAwaitableError)        \
  V(RefineType)                 \
  V(RepeatList)                 \
  V(RepeatTuple)                \
  V(Return)                     \
  V(RunPeriodicTasks)           \
  V(SetCellItem)                \
  V(SetDictItem)                \
  V(SetFunctionAttr)            \
  V(SetSetItem)                 \
  V(Snapshot)                   \
  V(StealCellItem)              \
  V(StoreArrayItem)             \
  V(StoreAttr)                  \
  V(StoreField)                 \
  V(StoreSubscr)                \
  V(UnaryOp)                    \
  V(UnpackExToTuple)            \
  V(VectorCall)                 \
  V(VectorCallStatic)           \
  V(VectorCallKW)               \
  V(WaitHandleLoadCoroOrResult) \
  V(WaitHandleLoadWaiter)       \
  V(WaitHandleRelease)          \
  V(XDecref)                    \
  V(XIncref)                    \
  V(YieldFrom)                  \
  V(YieldValue)

enum class Opcode {
#define DECLARE_OP(opname) k##opname,
  FOREACH_OPCODE(DECLARE_OP)
#undef DECLARE_OP
};

#define COUNT_OP(opname) +1
const size_t kNumOpcodes = FOREACH_OPCODE(COUNT_OP);
#undef COUNT_OP

extern const char* const kOpcodeNames[kNumOpcodes];

class BasicBlock;

// Every control flow instruction has one or more Edges. BasicBlocks that
// contain or are targets of these instructions hold pointers to their Edges in
// sets of in- and out-edges.
class Edge {
 public:
  Edge() = default;
  ~Edge();

  BasicBlock* from() const {
    return from_;
  }
  BasicBlock* to() const {
    return to_;
  }

  void set_from(BasicBlock* from);
  void set_to(BasicBlock* to);

 private:
  DISALLOW_COPY_AND_ASSIGN(Edge);

  BasicBlock* from_{nullptr};
  BasicBlock* to_{nullptr};
};

// Base class that all concrete HIR instructions must derive from.
//
// Instructions have variable sized instances; the operands are stored
// **before** the instruction. The memory layout for an instruction looks
// like:
//
//   +--------------+
//   | Operand 0    |
//   | Operand 1    |
//   | ...          |
//   | Num operands |
//   | Vtable ptr   | <--- Where `this` points
//   | ....         |
//   +--------------+
//
// Given that instructions have variable sized instances, they must be
// allocated using the `create` methods that are defined on concrete
// subclasses. Attempting to heap allocate instructions should result
// in a compiler error, however, automatic allocation will still compile.
// Don't do that.
class Instr {
 public:
  virtual ~Instr();

  static void operator delete(void* ptr) {
    auto instr = static_cast<Instr*>(ptr);
    free(instr->base());
  }

  // This defines a predicate per opcode that can be used to determine
  // if an instance of an instruction is a particular subclass
  // (e.g. `instr->IsBranch()`)
#define DEFINE_OP_PREDICATE(opname)       \
  bool Is##opname() const {               \
    return opcode() == Opcode::k##opname; \
  }
  FOREACH_OPCODE(DEFINE_OP_PREDICATE)
#undef DEFINE_OP_PREDICATE

  Opcode opcode() const {
    return opcode_;
  }

  const char* opname() const {
    auto opnum = static_cast<size_t>(opcode());
    if (opnum < kNumOpcodes) {
      return kOpcodeNames[opnum];
    }
    return "<invalid>";
  }

  // Return the number of operands that the instruction takes
  std::size_t NumOperands() const {
    return *(reinterpret_cast<const std::size_t*>(this) - 1);
  }

  // Return the i-th operand
  Register* GetOperand(std::size_t i) const {
    return const_cast<Instr*>(this)->operandAt(i);
  }

  // Update the i-th operand
  void SetOperand(std::size_t i, Register* reg) {
    operandAt(i) = reg;
  }

  // Visit all Registers used by the instruction, whether they're normal
  // operands or other data. Iteration can be stopped early by returning false
  // from the callback.
  virtual bool visitUses(const std::function<bool(Register*&)>& func) {
    auto num_uses = NumOperands();
    for (std::size_t i = 0; i < num_uses; i++) {
      if (!func(operandAt(i))) {
        return false;
      }
    }
    return true;
  }

  // Visit all Registers used by the instruction, without allowing mutation of
  // the uses.
  bool visitUses(const std::function<bool(Register*)>& func) const {
    return const_cast<Instr*>(this)->visitUses(
        [&func](Register*& reg) { return func(reg); });
  }

  // Return whether or not the instruction uses the supplied register as an
  // input
  bool Uses(Register* needle) const {
    bool found = false;
    visitUses([&](const Register* reg) {
      if (reg == needle) {
        found = true;
        return false;
      }
      return true;
    });
    return found;
  }

  // Replace uses of orig with replacement.
  void ReplaceUsesOf(Register* orig, Register* replacement) {
    visitUses([&](Register*& reg) {
      if (reg == orig) {
        reg = replacement;
      }
      return true;
    });
  }

  // If this instruction produces a value, return where it will be stored
  Register* GetOutput() const {
    return output_;
  }

  // Set where the output from this instruction will be stored
  void SetOutput(Register* dst) {
    if (output_ != nullptr) {
      output_->set_instr(nullptr);
    }
    if (dst != nullptr) {
      dst->set_instr(this);
    }
    output_ = dst;
  }

  // Basic blocks must be terminated with control flow ops
  bool IsTerminator() const;

  // If this is a control instruction, return the number of outgoing edges
  virtual std::size_t numEdges() const {
    return 0;
  }

  // If this is a control instruction, return the i-th edge
  virtual Edge* edge(std::size_t /* i */) {
    JIT_DCHECK(false, "not a control instruction");
    return nullptr;
  }

  const Edge* edge(std::size_t i) const {
    return const_cast<Instr*>(this)->edge(i);
  }

  // Get or set the i-th successor.
  BasicBlock* successor(std::size_t i) const {
    return edge(i)->to();
  }
  void set_successor(std::size_t i, BasicBlock* to) {
    edge(i)->set_to(to);
  }

  void InsertBefore(Instr& instr) {
    block_node_.InsertBefore(&instr.block_node_);
    link(instr.block());
  }

  void InsertAfter(Instr& instr) {
    block_node_.InsertAfter(&instr.block_node_);
    link(instr.block());
  }

  // Unlink this Instr from its block.
  void unlink();

  BasicBlock* block() const {
    return block_;
  }

  void ReplaceWith(Instr& instr) {
    instr.InsertBefore(*this);
    unlink();
  }

  void ExpandInto(const std::vector<Instr*>& expansion) {
    Instr* last = this;
    for (Instr* instr : expansion) {
      instr->InsertAfter(*last);
      last = instr;
    }
    unlink();
  }

  // Returns the `FrameState` that dominates this instruction, if one exists
  // and there are no non-replayable instructions between it and the
  // instruction.
  FrameState* getDominatingFrameState();

  // Returns whether or not this instruction can be safely re-executed.
  bool isReplayable() const;

  // Set/get the bytecode offset that this instruction is associated with
  void setBytecodeOffset(int off) {
    bytecode_offset_ = off;
  }
  int bytecodeOffset() const {
    return bytecode_offset_;
  }

  void copyBytecodeOffset(const Instr& instr) {
    setBytecodeOffset(instr.bytecodeOffset());
  }

 protected:
  DISALLOW_COPY_AND_ASSIGN(Instr);

  explicit Instr(Opcode opcode) : opcode_(opcode) {}

  void* operator new(std::size_t count, void* ptr) {
    return ::operator new(count, ptr);
  }

  // Allocate a block of memory suitable to house an `Instr`. This function is
  // intended to be used by the various `create` functions that are defined on
  // concrete `Instr` subclasses.
  static void* allocate(std::size_t fixed_size, std::size_t num_operands) {
    auto variable_size = num_operands * kPointerSize;
    char* ptr = static_cast<char*>(
        malloc(variable_size + fixed_size + sizeof(std::size_t)));
    ptr += variable_size;
    *reinterpret_cast<size_t*>(ptr) = num_operands;
    ptr += sizeof(std::size_t);
    return ptr;
  }

  void* base() {
    return reinterpret_cast<char*>(this) - (NumOperands() * kPointerSize) -
        sizeof(size_t);
  }

  Register** operands() {
    return static_cast<Register**>(base());
  }

  Register*& operandAt(std::size_t i) {
    JIT_DCHECK(
        i < NumOperands(),
        "operand %d out of range (max is %d)",
        i,
        NumOperands() - 1);
    return operands()[i];
  }

  friend class BasicBlock;

  // Link this Instr into its block. Meant to be called after inserting it into
  // the appropriate position in the block.
  void link(BasicBlock* block);

  // Set this Instr's block, updating any edges as appropriate.
  void set_block(BasicBlock* block);

  Opcode opcode_;
  Register* output_{nullptr};

  // Instructions are part of a doubly linked list in the basic block they
  // belong to
  IntrusiveListNode block_node_;

  BasicBlock* block_{nullptr};

  int bytecode_offset_{-1};
};

using InstrPredicate = std::function<bool(const Instr&)>;

struct RegState {
  RegState() = default;
  RegState(Register* reg, RefKind ref_kind, ValueKind value_kind)
      : reg{reg}, ref_kind{ref_kind}, value_kind{value_kind} {}

  Register* reg{nullptr};
  RefKind ref_kind{RefKind::kUncounted};
  ValueKind value_kind{ValueKind::kObject};
};

class DeoptBase : public Instr {
 public:
  explicit DeoptBase(Opcode op) : Instr(op) {}
  DeoptBase(Opcode op, const FrameState& frame) : Instr(op) {
    setFrameState(frame);
  }

  template <typename... Args>
  void emplaceLiveReg(Args&&... args) {
    live_regs_.emplace_back(std::forward<Args>(args)...);
  }

  const std::vector<RegState>& live_regs() const {
    return live_regs_;
  }

  std::vector<RegState>& live_regs() {
    return live_regs_;
  }

  // Set/get the metadata needed to reconstruct the state of the interpreter
  // after this instruction executes.
  void setFrameState(std::unique_ptr<FrameState> state) {
    frame_state_ = std::move(state);
  }

  void setFrameState(const FrameState& state) {
    frame_state_ = std::make_unique<FrameState>(state);
  }

  FrameState* frameState() const {
    return frame_state_.get();
  }

  std::unique_ptr<FrameState> takeFrameState() {
    return std::move(frame_state_);
  }

  bool visitUses(const std::function<bool(Register*&)>& func) override {
    if (!Instr::visitUses(func)) {
      return false;
    }
    if (auto fs = frameState()) {
      if (!fs->visitUses(func)) {
        return false;
      }
    }
    for (auto& reg_state : live_regs_) {
      if (!func(reg_state.reg)) {
        return false;
      }
    }
    if (guilty_reg_ != nullptr) {
      if (!func(guilty_reg_)) {
        return false;
      }
    }
    return true;
  }

  int nonce() const {
    return nonce_;
  }

  void set_nonce(int nonce) {
    nonce_ = nonce;
  }

  // Get or set the human-readable description of why this instruction might
  // deopt.
  const std::string& descr() const {
    return descr_;
  }

  void setDescr(std::string r) {
    descr_ = std::move(r);
  }

  // Get or set the optional value that is the likely cause for this
  // instruction deopting.
  Register* guiltyReg() const {
    return guilty_reg_;
  }

  void setGuiltyReg(Register* reg) {
    guilty_reg_ = reg;
  }

 private:
  std::vector<RegState> live_regs_;
  std::unique_ptr<FrameState> frame_state_{nullptr};
  // If set and this instruction deopts at runtime, the value that most likely
  // caused the deopt.
  Register* guilty_reg_{nullptr};
  int nonce_{-1};
  // A human-readable description of why this instruction might deopt.
  std::string descr_;
};

// This pile of template metaprogramming provides a convenient way to define
// concrete subclasses of `Instr`. It allows users to
//
// - Specify whether or not the instruction has an output via the `HasOutput`
//   tag type.
// - Specify the number of operands via the `Operands` tag type. Variadic
//   instructions are defined using `Operands<>`.
// - Specify an optional different base class. If given, it must derive from
//   `Instr` and appear as the last template argument. It's constructor must
//   accept an `Opcode` as the first argument.
template <class T, Opcode opcode, typename... Tys>
class InstrT;

// Base classes.
template <class T, Opcode opc>
class InstrT<T, opc> : public Instr {
 public:
  static constexpr bool has_output = false;

  template <typename... Args>
  InstrT(Args&&... args) : Instr(opc, std::forward<Args>(args)...) {}
};

template <class T, Opcode opc, class Base, typename... Tys>
class InstrT<T, opc, Base, Tys...> : public Base {
 public:
  static_assert(
      std::is_base_of<Instr, Base>::value,
      "base type must derive from Instr");
  static_assert(
      sizeof...(Tys) == 0,
      "base type must appear as last template parameter");

  template <typename... Args>
  InstrT(Args&&... args) : Base(opc, std::forward<Args>(args)...) {}
};

// Support for specifying the number of operands expected by the instruction.
//
// Caveats:
//
// - Custom constructors must be public in order to be accessible by the
//   `create` methods defined below.
// - Constructors are provided for common arities that expect operands to be
//   provided and handle setting them on the instruction.
template <int n = -1>
struct Operands;

template <class T, Opcode opcode, int arity, typename... Tys>
class InstrT<T, opcode, Operands<arity>, Tys...>
    : public InstrT<T, opcode, Tys...> {
 public:
  // Define a `create` method for non-variadic `T`.
  //
  // Usage:
  //   auto instr = T::create(<args for T's constructor>);
  template <typename... Args, class T1 = T>
  static std::enable_if_t<arity >= 0, T1>* create(Args&&... args) {
    auto ptr = Instr::allocate(sizeof(T1), arity);
    return new (ptr) T1(std::forward<Args>(args)...);
  }

  // Define a `create` method for variadic `T`.
  //
  // Usage:
  //   auto instr = T::create(<num_operands>, <args for T's constructor>);
  template <typename... Args, class T1 = T>
      static std::enable_if_t <
      arity<0, T1>* create(std::size_t num_ops, Args&&... args) {
    auto ptr = Instr::allocate(sizeof(T1), num_ops);
    return new (ptr) T1(std::forward<Args>(args)...);
  }

  // Forwarding constructor for variadic `T`.
  template <
      typename... Args,
      int a = arity,
      typename = std::enable_if_t<a <= 0>>
  InstrT(Args&&... args)
      : InstrT<T, opcode, Tys...>(std::forward<Args>(args)...) {}

  // Constructor for unary `T`.
  template <
      typename... Args,
      int a = arity,
      typename = std::enable_if_t<a == 1>>
  InstrT(Register* reg, Args&&... args)
      : InstrT<T, opcode, Tys...>(std::forward<Args>(args)...) {
    this->operandAt(0) = reg;
  }

  // TODO(mpage) - Get rid of this?
  template <int a = arity, typename T1 = std::enable_if_t<a == 1, Register>>
  T1* reg() const {
    return this->GetOperand(0);
  }

  // Constructor for binary `T`.
  template <
      typename... Args,
      int a = arity,
      typename = std::enable_if_t<a == 2>>
  InstrT(Register* lhs, Register* rhs, Args&&... args)
      : InstrT<T, opcode, Tys...>(std::forward<Args>(args)...) {
    this->operandAt(0) = lhs;
    this->operandAt(1) = rhs;
  }

  // Constructor for trinary `T`.
  template <
      typename... Args,
      int x = arity,
      typename = std::enable_if_t<x == 3>>
  InstrT(Register* a, Register* b, Register* c, Args&&... args)
      : InstrT<T, opcode, Tys...>(std::forward<Args>(args)...) {
    this->operandAt(0) = a;
    this->operandAt(1) = b;
    this->operandAt(2) = c;
  }

  // Constructor for 4 operand `T`.
  template <
      typename... Args,
      int x = arity,
      typename = std::enable_if_t<x == 4>>
  InstrT(Register* a, Register* b, Register* c, Register* d, Args&&... args)
      : InstrT<T, opcode, Tys...>(std::forward<Args>(args)...) {
    this->operandAt(0) = a;
    this->operandAt(1) = b;
    this->operandAt(2) = c;
    this->operandAt(3) = d;
  }
};

// Support for setting the output
struct HasOutput;

template <class T, Opcode opcode, typename... Tys>
class InstrT<T, opcode, HasOutput, Tys...> : public InstrT<T, opcode, Tys...> {
 public:
  static constexpr bool has_output = true;

  template <typename... Args>
  InstrT(Register* dst, Args&&... args)
      : InstrT<T, opcode, Tys...>(std::forward<Args>(args)...) {
    this->SetOutput(dst);
  }

  Register* dst() const {
    return this->GetOutput();
  }
};

#define INSTR_CLASS(name, ...) \
  name final : public InstrT<name, Opcode::k##name, __VA_ARGS__>

#define DEFINE_SIMPLE_INSTR(name, ...)   \
  class INSTR_CLASS(name, __VA_ARGS__) { \
   private:                              \
    friend InstrT;                       \
    using InstrT::InstrT;                \
  }

enum class BinaryOpKind {
  kAdd = 0,
  kAnd,
  kFloorDivide,
  kLShift,
  kMatrixMultiply,
  kModulo,
  kMultiply,
  kOr,
  kPower,
  kRShift,
  kSubscript,
  kSubtract,
  kTrueDivide,
  kXor,
  kFloorDivideUnsigned,
  kModuloUnsigned,
  kRShiftUnsigned,
  kNumBinaryOps,
};

const char* GetBinaryOpName(BinaryOpKind op);
BinaryOpKind ParseBinaryOpName(const char* name);

// Perform a binary operation (e.g. '+', '-')
class INSTR_CLASS(BinaryOp, HasOutput, Operands<2>, DeoptBase) {
 public:
  BinaryOp(
      Register* dst,
      BinaryOpKind op,
      Register* left,
      Register* right,
      const FrameState& frame)
      : InstrT(dst, left, right, frame), op_(op) {}

  BinaryOpKind op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  BinaryOpKind op_;
};

enum class UnaryOpKind {
  kNot = 0,
  kPositive = 1,
  kNegate = 2,
  kInvert = 3,
};

const char* GetUnaryOpName(UnaryOpKind op);
UnaryOpKind ParseUnaryOpName(const char* name);

// Perform a unary operator (-x, ~x, etc...)
class INSTR_CLASS(UnaryOp, HasOutput, Operands<1>, DeoptBase) {
 public:
  UnaryOp(
      Register* dst,
      UnaryOpKind op,
      Register* operand,
      const FrameState& frame)
      : InstrT(dst, operand, frame), op_(op) {}

  UnaryOpKind op() const {
    return op_;
  }

  Register* operand() const {
    return GetOperand(0);
  }

 private:
  UnaryOpKind op_;
};

enum class InPlaceOpKind {
  kAdd = 0,
  kAnd = 1,
  kFloorDivide = 2,
  kLShift = 3,
  kMatrixMultiply = 4,
  kModulo = 5,
  kMultiply = 6,
  kOr = 7,
  kPower = 8,
  kRShift = 9,
  kSubtract = 10,
  kTrueDivide = 11,
  kXor = 12,
};

const char* GetInPlaceOpName(InPlaceOpKind op);
InPlaceOpKind ParseInPlaceOpName(const char* name);

// Perform a in place operator x += 2
class INSTR_CLASS(InPlaceOp, HasOutput, Operands<2>, DeoptBase) {
 public:
  InPlaceOp(
      Register* dst,
      InPlaceOpKind op,
      Register* left,
      Register* right,
      const FrameState& frame)
      : InstrT(dst, left, right, frame), op_(op) {}

  InPlaceOpKind op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  InPlaceOpKind op_;
};

// Builds a slice object, with 2 or 3 operands from the stack
class INSTR_CLASS(BuildSlice, HasOutput, Operands<>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* start() const {
    return GetOperand(0);
  }

  Register* stop() const {
    return GetOperand(1);
  }

  Register* step() const {
    return NumOperands() == 2 ? nullptr : GetOperand(2);
  }
};

// Builds a new Function object, with the given qualified name and codeobj
class INSTR_CLASS(MakeFunction, HasOutput, Operands<2>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* qualname() const {
    return GetOperand(0);
  }

  Register* codeobj() const {
    return GetOperand(1);
  }
};

// Calls PyEntry_Init(func)
class INSTR_CLASS(InitFunction, Operands<1>) {
 public:
  using InstrT::InstrT;

  Register* func() const {
    return reg();
  }
};

class INSTR_CLASS(ListAppend, HasOutput, Operands<2>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* list() const {
    return GetOperand(0);
  }

  Register* item() const {
    return GetOperand(1);
  }
};

// extend the list with the elements in iterable
class INSTR_CLASS(ListExtend, HasOutput, Operands<3>, DeoptBase) {
 public:
  ListExtend(
      Register* dst,
      Register* list,
      Register* iterable,
      Register* func,
      const FrameState& frame)
      : InstrT(dst, list, iterable, func, frame) {}
  Register* list() const {
    return GetOperand(0);
  }

  Register* iterable() const {
    return GetOperand(1);
  }

  Register* func() const {
    return GetOperand(2);
  }
};

// Check whether the given pyobject is a tuple.
class INSTR_CLASS(CheckTuple, HasOutput, Operands<1>) {
 public:
  using InstrT::InstrT;

  Register* iterable() const {
    return GetOperand(0);
  }
};

// Gets a tuple representation from a sequence.
class INSTR_CLASS(GetTuple, HasOutput, Operands<1>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* iterable() const {
    return GetOperand(0);
  }
};

// An unconditional branch
class INSTR_CLASS(Branch, Operands<0>) {
 public:
  Branch(BasicBlock* target) : InstrT() {
    set_target(target);
  }

  BasicBlock* target() const {
    return edge_.to();
  }

  void set_target(BasicBlock* target) {
    edge_.set_to(target);
  }

  std::size_t numEdges() const override {
    return 1;
  }

  Edge* edge(std::size_t i) override {
    JIT_CHECK(i == 0, "only have 1 edge");
    return &edge_;
  }

 private:
  Edge edge_;
};

enum class FunctionAttr {
  kClosure,
  kAnnotations,
  kKwDefaults,
  kDefaults,
};

const char* functionFieldName(FunctionAttr field);

class INSTR_CLASS(SetFunctionAttr, Operands<2>) {
 public:
  SetFunctionAttr(Register* value, Register* base, FunctionAttr field)
      : InstrT(value, base), field_(field) {}

  Register* value() const {
    return GetOperand(0);
  }

  Register* base() const {
    return GetOperand(1);
  }

  FunctionAttr field() const {
    return field_;
  };

  uint64_t offset() const {
    switch (field_) {
      case FunctionAttr::kClosure:
        return offsetof(PyFunctionObject, func_closure);
      case FunctionAttr::kAnnotations:
        return offsetof(PyFunctionObject, func_annotations);
      case FunctionAttr::kKwDefaults:
        return offsetof(PyFunctionObject, func_kwdefaults);
      case FunctionAttr::kDefaults:
        return offsetof(PyFunctionObject, func_defaults);
    }
    JIT_CHECK(false, "invalid field %d", static_cast<int>(field_));
  }

 private:
  FunctionAttr field_;
};

class VectorCallBase : public DeoptBase {
 public:
  VectorCallBase(Opcode op, bool is_awaited)
      : DeoptBase(op), is_awaited_(is_awaited) {}
  VectorCallBase(Opcode op, bool is_awaited, const FrameState& frame)
      : DeoptBase(op, frame), is_awaited_(is_awaited) {
    setFrameState(frame);
  }

  // The function to call
  Register* func() const {
    return GetOperand(0);
  }

  std::size_t numArgs() const {
    return NumOperands() - 1;
  }

  Register* arg(std::size_t i) const {
    return GetOperand(i + 1);
  }

  Register* dst() const {
    return this->GetOutput();
  }

  bool isAwaited() const {
    return is_awaited_;
  }

 private:
  const bool is_awaited_;
};

DEFINE_SIMPLE_INSTR(VectorCall, HasOutput, Operands<>, VectorCallBase);
DEFINE_SIMPLE_INSTR(VectorCallStatic, HasOutput, Operands<>, VectorCallBase);
DEFINE_SIMPLE_INSTR(VectorCallKW, HasOutput, Operands<>, VectorCallBase);

class INSTR_CLASS(CallEx, HasOutput, Operands<2>, DeoptBase) {
 public:
  CallEx(Register* dst, Register* func, Register* pargs, bool is_awaited)
      : InstrT(dst, func, pargs), is_awaited_(is_awaited) {}
  CallEx(
      Register* dst,
      Register* func,
      Register* pargs,
      bool is_awaited,
      const FrameState& frame)
      : InstrT(dst, func, pargs, frame), is_awaited_(is_awaited) {}

  // The function to call
  Register* func() const {
    return GetOperand(0);
  }

  Register* pargs() const {
    return GetOperand(1);
  }

  bool isAwaited() const {
    return is_awaited_;
  }

 private:
  const bool is_awaited_;
};

class INSTR_CLASS(CallExKw, HasOutput, Operands<3>, DeoptBase) {
 public:
  CallExKw(
      Register* dst,
      Register* func,
      Register* pargs,
      Register* kwargs,
      bool is_awaited)
      : InstrT(dst, func, pargs, kwargs), is_awaited_(is_awaited) {}
  CallExKw(
      Register* dst,
      Register* func,
      Register* pargs,
      Register* kwargs,
      bool is_awaited,
      const FrameState& frame)
      : InstrT(dst, func, pargs, kwargs, frame), is_awaited_(is_awaited) {}

  // The function to call
  Register* func() const {
    return GetOperand(0);
  }

  Register* pargs() const {
    return GetOperand(1);
  }

  Register* kwargs() const {
    return GetOperand(2);
  }

  bool isAwaited() const {
    return is_awaited_;
  }

 private:
  const bool is_awaited_;
};

// Call to one of the C functions defined by CallCFunc_FUNCS. We have a static
// set of functions so we can (one day) safely (de)serialize HIR fully.
class INSTR_CLASS(CallCFunc, HasOutput, Operands<>) {
 public:
// List of allowed functions
#define CallCFunc_FUNCS(X)      \
  X(_PyAsyncGenValueWrapperNew) \
  X(_PyCoro_GetAwaitableIter)   \
  X(_PyGen_yf)                  \
  X(_PyEval_GetAIter)           \
  X(_PyEval_GetANext)

  enum class Func {
#define ENUM_FUNC(name, ...) k##name,
    CallCFunc_FUNCS(ENUM_FUNC)
#undef ENUM_FUNC
  };

  CallCFunc(Register* dst, Func func, const std::vector<Register*>& args)
      : InstrT(dst), func_(func) {
    size_t i = 0;
    for (Register* arg : args) {
      SetOperand(i++, arg);
    }
  }

  uint64_t funcAddr() const {
    return reinterpret_cast<uint64_t>(kFuncPtrMap[static_cast<size_t>(func_)]);
  }

  const char* funcName() const {
    return kFuncNames[static_cast<size_t>(func_)];
  }

 private:
  const Func func_;

  static const std::vector<void*> kFuncPtrMap;
  static const std::vector<const char*> kFuncNames;
};

// Phi instruction
class INSTR_CLASS(Phi, HasOutput, Operands<>) {
 public:
  Phi(Register* dst) : InstrT(dst) {}

  static Phi* create(
      Register* dst,
      const std::unordered_map<BasicBlock*, Register*>& args) {
    void* ptr = Instr::allocate(sizeof(Phi), args.size());
    auto phi = new (ptr) Phi(dst);
    phi->setArgs(args);
    return phi;
  }

  // A trivial phi merges its output with only one other value.
  Register* isTrivial() const {
    Register* out = GetOutput();
    Register* val = nullptr;
    for (std::size_t i = 0; i < NumOperands(); i++) {
      Register* reg = GetOperand(i);
      if (reg != out && reg != val) {
        if (val != nullptr) {
          return nullptr;
        }
        val = reg;
      }
    }
    return val;
  }

  // Return the index of the given predecessor in basic_blocks.
  std::size_t blockIndex(const BasicBlock* block) const;

  const std::vector<BasicBlock*> basic_blocks() const {
    return basic_blocks_;
  }

  void setArgs(const std::unordered_map<BasicBlock*, Register*>& args);

 private:
  // List of incoming blocks, sorted by ascending block ID.
  std::vector<BasicBlock*> basic_blocks_;
};

// The first operand is the receiver that was used for the corresponding
// LoadMethod. The second operand is the callable to call. The remaining
// operands are arguments to the call.
class INSTR_CLASS(CallMethod, HasOutput, Operands<>, DeoptBase) {
 public:
  CallMethod(Register* dst, bool is_awaited)
      : InstrT(dst), is_awaited_(is_awaited) {}
  CallMethod(Register* dst, bool is_awaited, const FrameState& frame)
      : InstrT(dst, frame), is_awaited_(is_awaited) {}

  // The function to call
  Register* func() const {
    return GetOperand(1);
  }

  // The register containing the receiver used to perform the method lookup
  Register* self() const {
    return GetOperand(0);
  }

  std::size_t NumArgs() const {
    return NumOperands() - 2;
  }

  Register* arg(std::size_t i) const {
    return GetOperand(i + 2);
  }

  bool isAwaited() const {
    return is_awaited_;
  }

 private:
  const bool is_awaited_;
};

class INSTR_CLASS(InvokeMethod, HasOutput, Operands<>, DeoptBase) {
 public:
  InvokeMethod(Register* dst, std::size_t slot, bool is_awaited)
      : InstrT(dst), slot_(slot), is_awaited_(is_awaited) {}

  // The function to call
  Register* func() const {
    return GetOperand(1);
  }

  // The register containing the receiver used to perform the method lookup
  Register* self() const {
    return GetOperand(0);
  }

  std::size_t NumArgs() const {
    return NumOperands() - 2;
  }

  Register* arg(std::size_t i) const {
    return GetOperand(i + 2);
  }

  int slot() const {
    return slot_;
  }

  bool isAwaited() const {
    return is_awaited_;
  }

 private:
  const std::size_t slot_;
  const bool is_awaited_;
};

// A call to a function at a known address
class INSTR_CLASS(CallStatic, HasOutput, Operands<>) {
 public:
  CallStatic(Register* out, void* addr, Type ret_type)
      : InstrT(out), addr_(addr), ret_type_(ret_type) {}

  std::size_t NumArgs() const {
    return NumOperands();
  }

  Register* arg(std::size_t i) const {
    return GetOperand(i);
  }

  void* addr() const {
    return addr_;
  }

  Type ret_type() const {
    return ret_type_;
  }

 private:
  void* addr_;
  Type ret_type_;
};

// A call to a function at a known address
class INSTR_CLASS(CallStaticRetVoid, Operands<>) {
 public:
  CallStaticRetVoid(void* addr) : InstrT(), addr_(addr) {}

  std::size_t NumArgs() const {
    return NumOperands();
  }

  Register* arg(std::size_t i) const {
    return GetOperand(i);
  }

  void* addr() const {
    return addr_;
  }

 private:
  void* addr_;
};

// Invokes a function with a static entry point, where we can
// directly provide the arguments using the x64 calling convention.
class INSTR_CLASS(InvokeStaticFunction, HasOutput, Operands<>, DeoptBase) {
 public:
  // Would be better not to have this constructor, we shouldn't use it, but
  // currently newInstr in the parser requires it, T85605140
  InvokeStaticFunction(
      Register* dst,
      PyFunctionObject* func,
      Type ret_type,
      const FrameState& frame)
      : InstrT(dst, frame), func_(func), ret_type_(ret_type) {}

  InvokeStaticFunction(Register* dst, PyFunctionObject* func, Type ret_type)
      : InstrT(dst), func_(func), ret_type_(ret_type) {}

  std::size_t NumArgs() const {
    return NumOperands();
  }

  Register* arg(std::size_t i) const {
    return GetOperand(i);
  }

  PyFunctionObject* func() const {
    return func_;
  }

  Type ret_type() const {
    return ret_type_;
  }

 private:
  PyFunctionObject* func_;
  Type ret_type_;
};

class CheckBase : public DeoptBase {
 protected:
  CheckBase(Opcode op) : DeoptBase(op) {
    auto new_frame = std::make_unique<FrameState>();
    setFrameState(std::move(new_frame));
  }

  CheckBase(Opcode op, const FrameState& frame) : DeoptBase(op, frame) {}

 public:
  Register* reg() const {
    return GetOperand(0);
  }
};

// Check if an exception has occurred (implied by var being NULL).
// If so, transfer control to the exception handler for the block.
DEFINE_SIMPLE_INSTR(CheckExc, HasOutput, Operands<1>, CheckBase);

// Check if an exception has occurred as indicated by a negative
// return code.
DEFINE_SIMPLE_INSTR(CheckNeg, HasOutput, Operands<1>, CheckBase);

// Check that a variable has been set. If not, raise an UnboundLocalError and
// transfer control to the block's exception handler.
class INSTR_CLASS(CheckVar, HasOutput, Operands<1>, CheckBase) {
 public:
  // `name_idx` is the index into co_varnames of the name that should be used
  // when raising an UnboundLocalError. A value of -1 indicates that no name
  // should be reported in the exception.
  CheckVar(Register* dst, Register* reg, int name_idx)
      : InstrT(dst, reg), name_idx_(name_idx) {}

  CheckVar(Register* dst, Register* reg, int name_idx, const FrameState& frame)
      : InstrT(dst, reg, frame), name_idx_(name_idx) {}

  void set_name_idx(int name_idx) {
    name_idx_ = name_idx;
  }

  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

// Check that a variable has been set. If not, raise an AttributeError and
// transfer control to the block's exception handler.
class INSTR_CLASS(CheckField, HasOutput, Operands<1>, CheckBase) {
 public:
  // `field_idx` is the index into co_consts of the field that should be used
  // when raising an AttributeError.
  CheckField(Register* dst, Register* reg, int field_idx)
      : InstrT(dst, reg), field_idx_(field_idx) {}

  CheckField(
      Register* dst,
      Register* reg,
      const FrameState& frame,
      int field_idx)
      : InstrT(dst, reg, frame), field_idx_(field_idx) {}

  void set_field_idx(int field_idx) {
    field_idx_ = field_idx;
  }

  int field_idx() const {
    return field_idx_;
  }

 private:
  int field_idx_;
};

// Check that a variable is not None.  If it is None it raises an AttributeError
// saying None doesn't have that attribute.
class INSTR_CLASS(CheckNone, HasOutput, Operands<1>, CheckBase) {
 public:
  // `name_idx` is the index into co_names of the name that should be used
  // when raising an AttributeError.
  CheckNone(Register* dst, Register* reg, int name_idx)
      : InstrT(dst, reg), name_idx_(name_idx) {}

  CheckNone(Register* dst, Register* reg, const FrameState& frame, int name_idx)
      : InstrT(dst, reg, frame), name_idx_(name_idx) {}

  void set_name_idx(int name_idx) {
    name_idx_ = name_idx;
  }

  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

DEFINE_SIMPLE_INSTR(
    IsNegativeAndErrOccurred,
    HasOutput,
    Operands<1>,
    DeoptBase);

class INSTR_CLASS(LoadField, HasOutput, Operands<1>) {
 public:
  LoadField(
      Register* dst,
      Register* receiver,
      std::size_t offset,
      Type type,
      bool borrowed = true)
      : InstrT(dst, receiver),
        offset_(offset),
        type_(type),
        borrowed_(borrowed) {}

  // The object we're loading the attribute from
  Register* receiver() const {
    return reg();
  }

  // Offset where the field is stored
  std::size_t offset() const {
    return offset_;
  }

  Type type() const {
    return type_;
  }

  bool borrowed() const {
    return borrowed_;
  }

 private:
  std::size_t offset_;
  Type type_;
  bool borrowed_;
};

class INSTR_CLASS(StoreField, Operands<3>) {
 public:
  StoreField(
      Register* receiver,
      std::size_t offset,
      Register* value,
      Type type,
      Register* previous)
      : InstrT(receiver, value, previous), offset_(offset), type_(type) {}

  // The object we're loading the attribute from
  Register* receiver() const {
    return GetOperand(0);
  }

  void set_receiver(Register* receiver) {
    SetOperand(0, receiver);
  }

  // The value being stored
  Register* value() const {
    return GetOperand(1);
  }

  void set_value(Register* value) {
    SetOperand(1, value);
  }

  Register* previous() const {
    return GetOperand(2);
  }

  // Offset where the field is stored
  std::size_t offset() const {
    return offset_;
  }

  Type type() const {
    return type_;
  }

 private:
  std::size_t offset_;
  Type type_;
};

class INSTR_CLASS(Cast, HasOutput, Operands<1>, DeoptBase) {
 public:
  Cast(
      Register* dst,
      Register* receiver,
      PyTypeObject* pytype,
      bool optional,
      const FrameState& frame)
      : InstrT(dst, receiver, frame), pytype_(pytype), optional_(optional) {}

  Register* value() const {
    return reg();
  }

  PyTypeObject* pytype() const {
    return pytype_;
  }

  bool optional() const {
    return optional_;
  }

 private:
  PyTypeObject* pytype_;
  bool optional_;
};

// Perform a binary operation (e.g. '+', '-') on primitive int operands
class INSTR_CLASS(IntBinaryOp, HasOutput, Operands<2>) {
 public:
  IntBinaryOp(Register* dst, BinaryOpKind op, Register* left, Register* right)
      : InstrT(dst, left, right), op_(op) {}

  BinaryOpKind op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  BinaryOpKind op_;
};

// Perform a binary operation (e.g. '+', '-') on primitive double operands
class INSTR_CLASS(DoubleBinaryOp, HasOutput, Operands<2>) {
 public:
  DoubleBinaryOp(
      Register* dst,
      BinaryOpKind op,
      Register* left,
      Register* right)
      : InstrT(dst, left, right), op_(op) {}

  BinaryOpKind op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  BinaryOpKind op_;
};

enum class PrimitiveUnaryOpKind {
  kNegateInt = 0,
  kInvertInt = 1,
};

const char* GetPrimitiveUnaryOpName(PrimitiveUnaryOpKind op);
PrimitiveUnaryOpKind ParsePrimitiveUnaryOpName(const char* name);

// Perform a unary operation (e.g. '~', '-') on primitive operands
class INSTR_CLASS(PrimitiveUnaryOp, HasOutput, Operands<1>) {
 public:
  PrimitiveUnaryOp(Register* dst, PrimitiveUnaryOpKind op, Register* value)
      : InstrT(dst, value), op_(op) {}

  PrimitiveUnaryOpKind op() const {
    return op_;
  }

  Register* value() const {
    return GetOperand(0);
  }

 private:
  PrimitiveUnaryOpKind op_;
};

enum class CompareOp {
  kLessThan = 0,
  kLessThanEqual,
  kEqual,
  kNotEqual,
  kGreaterThan,
  kGreaterThanEqual,
  kIn,
  kNotIn,
  kIs,
  kIsNot,
  kExcMatch,
  kGreaterThanUnsigned,
  kGreaterThanEqualUnsigned,
  kLessThanUnsigned,
  kLessThanEqualUnsigned,
  kNumCompareOps
};

const char* GetCompareOpName(CompareOp op);

// Perform the comparison indicated by op
class INSTR_CLASS(Compare, HasOutput, Operands<2>, DeoptBase) {
 public:
  Compare(
      Register* dst,
      CompareOp op,
      Register* left,
      Register* right,
      const FrameState& frame)
      : InstrT(dst, left, right, frame), op_(op) {}

  CompareOp op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  CompareOp op_;
};

// Like Compare but has an Int32 output so it can be used to replace
// a Compare + IsTruthy.
class INSTR_CLASS(CompareBool, HasOutput, Operands<2>, DeoptBase) {
 public:
  CompareBool(
      CompareOp op,
      Register* dst,
      Register* left,
      Register* right,
      const FrameState& frame)
      : InstrT(dst, left, right, frame), op_(op) {}

  CompareOp op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  CompareOp op_;
};

class INSTR_CLASS(IntConvert, HasOutput, Operands<1>) {
 public:
  IntConvert(Register* dst, Register* src, Type type)
      : InstrT(dst, src), type_(type) {}

  Register* src() const {
    return GetOperand(0);
  }

  Type type() const {
    return type_;
  }

 private:
  Type type_;
};

// Perform the comparison indicated by op
enum class PrimitiveCompareOp {
  kLessThan = 0,
  kLessThanEqual,
  kEqual,
  kNotEqual,
  kGreaterThan,
  kGreaterThanEqual,
  kGreaterThanUnsigned,
  kGreaterThanEqualUnsigned,
  kLessThanUnsigned,
  kLessThanEqualUnsigned,
  kNumPrimitiveCompareOps
};

const char* GetPrimitiveCompareOpName(PrimitiveCompareOp op);
PrimitiveCompareOp ParsePrimitiveCompareOpName(const char* name);

class INSTR_CLASS(PrimitiveCompare, HasOutput, Operands<2>) {
 public:
  PrimitiveCompare(
      Register* dst,
      PrimitiveCompareOp op,
      Register* left,
      Register* right)
      : InstrT(dst, left, right), op_(op) {}

  PrimitiveCompareOp op() const {
    return op_;
  }

  Register* left() const {
    return GetOperand(0);
  }

  Register* right() const {
    return GetOperand(1);
  }

 private:
  PrimitiveCompareOp op_;
};

class INSTR_CLASS(PrimitiveBox, HasOutput, Operands<1>) {
 public:
  PrimitiveBox(Register* dst, Register* value, int type)
      : InstrT(dst, value), type_(type) {}

  Register* value() const {
    return GetOperand(0);
  }

  bool is_signed() const {
    return type_ != TYPED_DOUBLE && (type_ & (TYPED_INT_SIGNED));
  }

 private:
  int type_;
};

class INSTR_CLASS(PrimitiveUnbox, HasOutput, Operands<1>) {
 public:
  PrimitiveUnbox(Register* dst, Register* value, Type type)
      : InstrT(dst, value), type_(type) {}

  Register* value() const {
    return GetOperand(0);
  }

  Type type() const {
    return type_;
  }

 private:
  Type type_;
};

class CondBranchBase : public Instr {
 public:
  CondBranchBase(Opcode opcode, BasicBlock* true_bb, BasicBlock* false_bb)
      : Instr(opcode) {
    set_true_bb(true_bb);
    set_false_bb(false_bb);
  }

  BasicBlock* true_bb() const {
    return true_edge_.to();
  }

  void set_true_bb(BasicBlock* block) {
    true_edge_.set_to(block);
  }

  BasicBlock* false_bb() const {
    return false_edge_.to();
  }

  void set_false_bb(BasicBlock* block) {
    false_edge_.set_to(block);
  }

  std::size_t numEdges() const override {
    return 2;
  }

  Edge* edge(std::size_t i) override {
    JIT_DCHECK(i < 2, "only have 2 edges");
    return i == 0 ? &true_edge_ : &false_edge_;
  }

 private:
  Edge true_edge_;
  Edge false_edge_;
};

// Transfer control to `true_bb` if `reg` is nonzero, otherwise `false_bb`.
DEFINE_SIMPLE_INSTR(CondBranch, Operands<1>, CondBranchBase);

// Branch to `true_bb` if the operand is not the sentinel value that indicates
// an iterator is exhausted, or `false_bb` otherwise.
DEFINE_SIMPLE_INSTR(CondBranchIterNotDone, Operands<1>, CondBranchBase);

// Branch to `true_bb` if the operand matches the supplied type specification,
// or `false_bb` otherwise.
class INSTR_CLASS(CondBranchCheckType, Operands<1>, CondBranchBase) {
 public:
  CondBranchCheckType(
      Register* target,
      const Type& type,
      BasicBlock* true_bb,
      BasicBlock* false_bb)
      : InstrT(target, true_bb, false_bb), type_(type) {}

  const Type& type() const {
    return type_;
  }

 private:
  const Type type_;
};

// Decrement the reference count of `reg`
DEFINE_SIMPLE_INSTR(Decref, Operands<1>);

// Decrement the reference count of `reg`, if `reg` is not NULL
DEFINE_SIMPLE_INSTR(XDecref, Operands<1>);

// Increment the reference count of `reg`
DEFINE_SIMPLE_INSTR(Incref, Operands<1>);

// Increment the refrence count of `reg`, if `reg` is not NULL
DEFINE_SIMPLE_INSTR(XIncref, Operands<1>);

// Load an attribute from an object
class INSTR_CLASS(LoadAttr, HasOutput, Operands<1>, DeoptBase) {
 public:
  LoadAttr(
      Register* dst,
      Register* receiver,
      int name_idx,
      const FrameState& frame)
      : InstrT(dst, receiver, frame), name_idx_(name_idx) {}

  // The object we're loading the attribute from
  Register* receiver() const {
    return GetOperand(0);
  }

  // Index of the attribute name in the code object's co_names tuple
  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

// Load an attribute from an object, skipping the instance dictionary but still
// calling descriptors as appropriate (to create bound methods, for example).
class INSTR_CLASS(LoadAttrSpecial, HasOutput, Operands<1>, DeoptBase) {
 public:
  LoadAttrSpecial(
      Register* dst,
      Register* receiver,
      _Py_Identifier* id,
      const FrameState& frame)
      : InstrT(dst, receiver, frame), id_(id) {}

  _Py_Identifier* id() const {
    return id_;
  }

 private:
  _Py_Identifier* id_;
};

// Format and raise an error after failing to get an iterator for 'async with'.
class INSTR_CLASS(RaiseAwaitableError, Operands<1>, DeoptBase) {
 public:
  RaiseAwaitableError(
      Register* type,
      _Py_CODEUNIT with_opcode,
      const FrameState& frame)
      : InstrT(type, frame), with_opcode_(with_opcode) {}

  _Py_CODEUNIT with_opcode() const {
    return with_opcode_;
  }

 private:
  _Py_CODEUNIT with_opcode_;
};

// Load a guard (index 0) or value (index 1) from a cache specialized for
// loading attributes from type receivers
class INSTR_CLASS(LoadTypeAttrCacheItem, HasOutput, Operands<0>) {
 public:
  LoadTypeAttrCacheItem(Register* dst, int cache_id, int item_idx)
      : InstrT(dst), cache_id_(cache_id), item_idx_(item_idx) {
    JIT_CHECK(item_idx < 2, "only two elements in the cache");
  }

  int cache_id() const {
    return cache_id_;
  }

  int item_idx() const {
    return item_idx_;
  }

 private:
  int cache_id_;
  int item_idx_;
};

// Perform a full attribute lookup. Fill the cache if the receiver is a type
// object.
class INSTR_CLASS(FillTypeAttrCache, HasOutput, Operands<1>, DeoptBase) {
 public:
  FillTypeAttrCache(
      Register* dst,
      Register* receiver,
      int name_idx,
      int cache_id,
      const FrameState& frame)
      : InstrT(dst, receiver, frame),
        name_idx_(name_idx),
        cache_id_(cache_id) {}
  FillTypeAttrCache(
      Register* dst,
      Register* receiver,
      int name_idx,
      int cache_id,
      std::unique_ptr<FrameState> frame)
      : InstrT(dst, receiver), name_idx_(name_idx), cache_id_(cache_id) {
    setFrameState(std::move(frame));
  }

  // The object we're loading the attribute from
  Register* receiver() const {
    return reg();
  }

  // Index of the attribute name in the code object's co_names tuple
  int name_idx() const {
    return name_idx_;
  }

  int cache_id() const {
    return cache_id_;
  }

 private:
  int name_idx_;
  int cache_id_;
};

// Like LoadAttr, but when we know that we're loading an attribute that will be
// used for a method call.
class INSTR_CLASS(LoadMethod, HasOutput, Operands<1>, DeoptBase) {
 public:
  LoadMethod(
      Register* dst,
      Register* receiver,
      int name_idx,
      const FrameState& frame)
      : InstrT(dst, receiver, frame), name_idx_(name_idx) {}

  // The object we're loading the attribute from
  Register* receiver() const {
    return GetOperand(0);
  }

  // Index of the attribute name in the code object's co_names tuple
  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

class LoadSuperBase : public DeoptBase {
 protected:
  LoadSuperBase(Opcode op, int name_idx, bool no_args_in_super_call)
      : DeoptBase(op),
        name_idx_(name_idx),
        no_args_in_super_call_(no_args_in_super_call) {}

  LoadSuperBase(
      Opcode op,
      int name_idx,
      bool no_args_in_super_call,
      const FrameState& frame)
      : DeoptBase(op, frame),
        name_idx_(name_idx),
        no_args_in_super_call_(no_args_in_super_call) {}

 public:
  // Global 'super' value
  Register* global_super() const {
    return GetOperand(0);
  }

  // See comment for 'receiver'
  Register* type() const {
    return GetOperand(1);
  }

  // The object that determines mro to be searched.
  // Search will be started from the class right after the 'type'
  Register* receiver() const {
    return GetOperand(2);
  }
  // Index of the attribute name in the code object's co_names tuple
  int name_idx() const {
    return name_idx_;
  }

  bool no_args_in_super_call() const {
    return no_args_in_super_call_;
  }

 private:
  int name_idx_;
  bool no_args_in_super_call_;
};

DEFINE_SIMPLE_INSTR(LoadMethodSuper, HasOutput, Operands<3>, LoadSuperBase);
DEFINE_SIMPLE_INSTR(LoadAttrSuper, HasOutput, Operands<3>, LoadSuperBase);

// Load the current PyFunctionObject* into a Register. Must not appear after
// any non-LoadArg instructions.
DEFINE_SIMPLE_INSTR(LoadCurrentFunc, HasOutput, Operands<0>);

// Load the value from the cell in operand
DEFINE_SIMPLE_INSTR(LoadCellItem, HasOutput, Operands<1>);

// Load the value from the cell in src, stealing the reference to it. This is
// used only as the precursor to SetCellItem, so that we can decref the old item
// in the cell that the cell is about to lose its reference to.
DEFINE_SIMPLE_INSTR(StealCellItem, HasOutput, Operands<1>);

// Store a value to the cell in dst. The `old` arg is unused but exists in order
// to ensure that the previous cell contents are not decref-ed until after the
// new cell contents are in place.
class INSTR_CLASS(SetCellItem, Operands<3>) {
 public:
  using InstrT::InstrT;

  Register* cell() const {
    return GetOperand(0);
  }

  Register* src() const {
    return GetOperand(1);
  }
};

// Load a constant value (given as a Type) into a register.
class INSTR_CLASS(LoadConst, HasOutput, Operands<0>) {
 public:
  LoadConst(Register* dst, Type type) : InstrT(dst), type_(type) {
    JIT_DCHECK(
        type.isSingleValue(), "Given Type must represent a single value");
  }

  Type type() const {
    return type_;
  }

 private:
  Type type_;
};

class INSTR_CLASS(LoadFunctionIndirect, HasOutput, Operands<0>, DeoptBase) {
 public:
  LoadFunctionIndirect(
      PyObject** funcptr,
      PyObject* descr,
      Register* dst,
      const FrameState& frame)
      : InstrT(dst, frame), funcptr_(funcptr), descr_(descr) {}

  PyObject** funcptr() const {
    return funcptr_;
  }
  PyObject* descr() const {
    return descr_;
  }

 private:
  PyObject** funcptr_;
  PyObject* descr_;
};

// Load a global.
//
// The name is specified by the name_idx in the co_names tuple of the code
// object.
class INSTR_CLASS(LoadGlobalCached, HasOutput, Operands<0>) {
 public:
  LoadGlobalCached(Register* dst, int name_idx)
      : InstrT(dst), name_idx_(name_idx) {}

  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

class INSTR_CLASS(LoadGlobal, HasOutput, Operands<0>, DeoptBase) {
 public:
  LoadGlobal(Register* dst, int name_idx, const FrameState& frame)
      : InstrT(dst, frame), name_idx_(name_idx) {}

  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

// Return a copy of the input with a refined Type. The output Type is the
// intersection of the given Type and the input's Type.
class INSTR_CLASS(RefineType, HasOutput, Operands<1>) {
 public:
  RefineType(Register* dst, Type type, Register* src)
      : InstrT(dst, src), type_(type) {}

  Type type() const {
    return type_;
  }

 private:
  Type type_;
};

class RepeatBase : public DeoptBase {
 protected:
  RepeatBase(Opcode op) : DeoptBase(op) {
    auto new_frame = std::make_unique<FrameState>();
    setFrameState(std::move(new_frame));
  }

  RepeatBase(Opcode op, const FrameState& frame) : DeoptBase(op, frame) {}

 public:
  Register* seq() const {
    return GetOperand(0);
  }

  Register* num() const {
    return GetOperand(1);
  }
};

// Repeat a list; e.g. [1, 2] * 2 == [1, 2, 1, 2]
// Expects `num` to be a primitive integer
DEFINE_SIMPLE_INSTR(RepeatList, HasOutput, Operands<2>, RepeatBase);

// Repeat a tuple; e.g. (1, 2) * 2 == (1, 2, 1, 2)
// Expects `num` to be a primitive integer
DEFINE_SIMPLE_INSTR(RepeatTuple, HasOutput, Operands<2>, RepeatBase);

//  Return from the function
class INSTR_CLASS(Return, Operands<1>) {
 public:
  Return(Register* val) : InstrT(val), type_(TObject) {}
  Return(Register* val, Type type) : InstrT(val), type_(type) {}

  Type type() const {
    return type_;
  }

 private:
  Type type_;
};

// Assign one register to another
DEFINE_SIMPLE_INSTR(Assign, HasOutput, Operands<1>);

// Load the value of an argument to the current function. Reads from implicit
// state set up by the function prologue and must not appear after any
// non-LoadArg instruction.
class INSTR_CLASS(LoadArg, HasOutput, Operands<0>) {
 public:
  LoadArg(Register* dst, uint arg_idx)
      : InstrT(dst), arg_idx_(arg_idx), type_(TObject) {}

  LoadArg(Register* dst, uint arg_idx, Type type)
      : InstrT(dst), arg_idx_(arg_idx), type_(type) {}

  uint arg_idx() const {
    return arg_idx_;
  }

  Type type() const {
    return type_;
  }

 private:
  uint arg_idx_;
  Type type_;
};

// Allocate a tuple or list object with number of values
class INSTR_CLASS(MakeListTuple, HasOutput, Operands<0>, DeoptBase) {
 public:
  MakeListTuple(
      bool is_tuple,
      Register* dst,
      size_t nvalues,
      const FrameState& frame)
      : InstrT(dst, frame), tuple_(is_tuple), nvalues_(nvalues) {}

  size_t nvalues() const {
    return nvalues_;
  }

  bool is_tuple() const {
    return tuple_;
  }

 private:
  bool tuple_;
  size_t nvalues_;
};

// Initialize a tuple or a list with the arguments
class INSTR_CLASS(InitListTuple, Operands<>) {
 public:
  InitListTuple(bool is_tuple) : InstrT(), tuple_(is_tuple) {}

  bool is_tuple() const {
    return tuple_;
  }

  size_t num_args() const {
    return NumOperands() - 1;
  }

 private:
  bool tuple_;
};

// Initialize a tuple from a list
class INSTR_CLASS(MakeTupleFromList, HasOutput, Operands<1>, DeoptBase) {
 public:
  MakeTupleFromList(Register* dst, Register* list, const FrameState& frame)
      : InstrT(dst, list, frame) {}

  Register* list() const {
    return GetOperand(0);
  }
};

// Load an element from a tuple at a known index, with no bounds checking.
class INSTR_CLASS(LoadTupleItem, HasOutput, Operands<1>) {
 public:
  LoadTupleItem(Register* dst, Register* tuple, size_t idx)
      : InstrT(dst, tuple), idx_(idx) {}

  Register* tuple() const {
    return GetOperand(0);
  }

  size_t idx() const {
    return idx_;
  }

 private:
  size_t idx_;
};

// Load an element from an array at a known index and offset, with no bounds
// checking. Equivalent to ((type*)(((char*)ob_item)+offset))[idx]
class INSTR_CLASS(LoadArrayItem, HasOutput, Operands<3>) {
 public:
  LoadArrayItem(
      Register* dst,
      Register* ob_item,
      Register* idx,
      // This operand is never actually used, but it's an input for this because
      // we need to keep a reference to the container alive. The refcount
      // insertion pass handles this for us if the container is an input for
      // this instruction.
      Register* array_unused,
      ssize_t offset,
      Type type)
      : InstrT(dst, ob_item, idx, array_unused), offset_(offset), type_(type) {}

  Register* ob_item() const {
    return GetOperand(0);
  }

  Register* idx() const {
    return GetOperand(1);
  }

  ssize_t offset() const {
    return offset_;
  }

  Type type() const {
    return type_;
  }

 private:
  ssize_t offset_;
  Type type_;
};

// Store an element to an array at a known index, with no bounds checking.
class INSTR_CLASS(StoreArrayItem, Operands<4>) {
 public:
  StoreArrayItem(
      Register* ob_item,
      Register* idx,
      Register* value,
      // This operand is never actually used, but it's an input for this because
      // we need to keep a reference to the container alive. The refcount
      // insertion pass handles this for us if the container is an input for
      // this instruction.
      Register* container_unused,
      Type type)
      : InstrT(ob_item, idx, value, container_unused), type_(type) {}

  Register* ob_item() const {
    return GetOperand(0);
  }

  Register* idx() const {
    return GetOperand(1);
  }

  Register* value() const {
    return GetOperand(2);
  }

  Type type() const {
    return type_;
  }

 private:
  Type type_;
};

// Check whether the given index lies within the array boundary.
// Returns the actual index between [0, len(array)) into the array (in case it's
// negative). Returns -1 if the given index is not within bounds.
class INSTR_CLASS(CheckSequenceBounds, HasOutput, Operands<2>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* array() const {
    return GetOperand(0);
  }

  Register* idx() const {
    return GetOperand(1);
  }
};

// Create a cell holding given value and place the cell in dst.
// Calls PyCell_New, so it implicitly increfs the value placed in the cell.
class INSTR_CLASS(MakeCell, HasOutput, Operands<1>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* val() const {
    return GetOperand(0);
  }
};

// Allocate an empty dict with the given capacity, or the default capacity if 0
// is given.
class INSTR_CLASS(MakeDict, HasOutput, Operands<0>, DeoptBase) {
 public:
  MakeDict(Register* dst, size_t capacity, const FrameState& frame)
      : InstrT(dst, frame), capacity_(capacity) {}

  size_t GetCapacity() const {
    return capacity_;
  }

 private:
  size_t capacity_;
};

// Allocate an empty checked dict with the given capacity, or the default
// capacity if 0 is given.
class INSTR_CLASS(MakeCheckedDict, HasOutput, Operands<0>, DeoptBase) {
 public:
  MakeCheckedDict(
      Register* dst,
      size_t capacity,
      Type dict_type,
      const FrameState& frame)
      : InstrT(dst, frame), capacity_(capacity), type_(dict_type) {}

  size_t GetCapacity() const {
    return capacity_;
  }

  Type type() const {
    return type_;
  }

 private:
  size_t capacity_;
  Type type_;
};

// merge two maps by (ultimately) calling _PyDict_MergeEx
DEFINE_SIMPLE_INSTR(MergeDictUnpack, HasOutput, Operands<3>, DeoptBase);

// Allocate an empty set
DEFINE_SIMPLE_INSTR(MakeSet, HasOutput, Operands<0>, DeoptBase);

// merge two sets by calling _PySet_Update
DEFINE_SIMPLE_INSTR(MergeSetUnpack, HasOutput, Operands<2>, DeoptBase);

class INSTR_CLASS(SetDictItem, HasOutput, Operands<3>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* GetDict() const {
    return GetOperand(0);
  }

  Register* GetKey() const {
    return GetOperand(1);
  }

  Register* GetValue() const {
    return GetOperand(2);
  }
};

class INSTR_CLASS(SetSetItem, HasOutput, Operands<2>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* GetSet() const {
    return GetOperand(0);
  }

  Register* GetKey() const {
    return GetOperand(1);
  }
};

// Load the size of a PyVarObject as a CInt64.
DEFINE_SIMPLE_INSTR(LoadVarObjectSize, HasOutput, Operands<1>);

// Set the attribute of an object
//
// Places NULL in dst if an error occurred or a non-NULL value otherwise
class INSTR_CLASS(StoreAttr, HasOutput, Operands<2>, DeoptBase) {
 public:
  StoreAttr(
      Register* dst,
      Register* receiver,
      int name_idx,
      Register* value,
      const FrameState& frame)
      : InstrT(dst, receiver, value, frame), name_idx_(name_idx) {}

  // The object we're loading the attribute from
  Register* receiver() const {
    return GetOperand(0);
  }

  void set_receiver(Register* receiver) {
    SetOperand(0, receiver);
  }

  // The value being stored
  Register* value() const {
    return GetOperand(1);
  }

  void set_value(Register* value) {
    SetOperand(1, value);
  }

  // Index of the attribute name in the code object's co_names tuple
  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

// Stores into an index
//
// Places NULL in dst if an error occurred or a non-NULL value otherwise
class INSTR_CLASS(StoreSubscr, HasOutput, Operands<3>, DeoptBase) {
 public:
  using InstrT::InstrT;

  // The index we're doing the subscript with
  Register* index() const {
    return GetOperand(1);
  }

  void set_index(Register* receiver) {
    SetOperand(1, receiver);
  }

  // The container we're doing the subscript with
  Register* container() const {
    return GetOperand(0);
  }

  void set_container(Register* receiver) {
    SetOperand(0, receiver);
  }

  // The value being stored
  Register* value() const {
    return GetOperand(2);
  }

  void set_value(Register* value) {
    SetOperand(2, value);
  }
};

// Return a new iterator for the object, or return it if it's an iterator
DEFINE_SIMPLE_INSTR(GetIter, HasOutput, Operands<1>, DeoptBase);

// Invoke next() on the iterator.
//
// The output is one of three values:
//
//   1. A sentinel value that indicates the iterator is exhausted.
//   2. NULL to indicate an error has occurred.
//   3. Any other value is the output of the iterator.
DEFINE_SIMPLE_INSTR(InvokeIterNext, HasOutput, Operands<1>, DeoptBase);

// Returns a non-zero value if we need to release the GIL or run pending calls
// (e.g. signal handlers).  Returns 0 otherwise. This is intended to be
// followed immediately by a CondBranch.
DEFINE_SIMPLE_INSTR(LoadEvalBreaker, HasOutput, Operands<0>);

// Let other threads run, run signal handlers, etc.
DEFINE_SIMPLE_INSTR(RunPeriodicTasks, HasOutput, Operands<0>, DeoptBase);

class INSTR_CLASS(Snapshot, Operands<0>) {
 public:
  Snapshot(const FrameState& frame_state) : InstrT() {
    setFrameState(frame_state);
  }
  Snapshot() : InstrT() {}

  // Set/get the metadata needed to reconstruct the state of the interpreter
  // after this instruction executes.
  void setFrameState(std::unique_ptr<FrameState> state) {
    frame_state_ = std::move(state);
  }

  void setFrameState(const FrameState& state) {
    frame_state_ = std::make_unique<FrameState>(state);
  }

  FrameState* frameState() const {
    return frame_state_.get();
  }

  bool visitUses(const std::function<bool(Register*&)>& func) override {
    if (auto fs = frameState()) {
      return fs->visitUses(func);
    }
    return true;
  }

 private:
  std::unique_ptr<FrameState> frame_state_{nullptr};
};

// Always deopt.
DEFINE_SIMPLE_INSTR(Deopt, Operands<0>, DeoptBase);

// A guard verifies that the value of pred is true. When it's not, control is
// transferred to the interpreter at the point specified by the attached
// FrameState.
DEFINE_SIMPLE_INSTR(Guard, Operands<1>, DeoptBase);

// A guard that verifies that its src is the same object as the target, or
// deopts if not.
class INSTR_CLASS(GuardIs, HasOutput, Operands<1>, DeoptBase) {
 public:
  GuardIs(Register* dst, PyObject* target, Register* src)
      : InstrT(dst, src), target_(target) {}

  PyObject* target() const {
    return target_;
  }

 private:
  PyObject* target_;
};

// Output 1, 0, if `value` is truthy or not truthy.
DEFINE_SIMPLE_INSTR(IsTruthy, HasOutput, Operands<1>, DeoptBase);

DEFINE_SIMPLE_INSTR(IsInstance, HasOutput, Operands<2>, DeoptBase);

class INSTR_CLASS(ImportFrom, HasOutput, Operands<1>, DeoptBase) {
 public:
  ImportFrom(
      Register* dst,
      Register* module,
      int name_idx,
      const FrameState& frame)
      : InstrT(dst, module, frame), name_idx_(name_idx) {}

  Register* module() const {
    return GetOperand(0);
  }

  int nameIdx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

class INSTR_CLASS(ImportName, HasOutput, Operands<2>, DeoptBase) {
 public:
  ImportName(
      Register* dst,
      int name_idx,
      Register* fromlist,
      Register* level,
      const FrameState& frame)
      : InstrT(dst, fromlist, level, frame), name_idx_(name_idx) {}

  Register* GetFromList() const {
    return GetOperand(0);
  }

  Register* GetLevel() const {
    return GetOperand(1);
  }

  int name_idx() const {
    return name_idx_;
  }

 private:
  int name_idx_;
};

// (Re)raises an exception with optional cause.
class INSTR_CLASS(Raise, Operands<>, DeoptBase) {
 public:
  enum class Kind {
    kReraise,
    kRaiseWithExc,
    kRaiseWithExcAndCause,
  };

 private:
  Raise(Kind kind, const FrameState& frame) : InstrT(frame), kind_(kind) {}

 public:
  explicit Raise(const FrameState& frame) : Raise(Kind::kReraise, frame) {}

  Raise(const FrameState& frame, Register* exc)
      : Raise(Raise::Kind::kRaiseWithExc, frame) {
    SetOperand(0, exc);
  }

  Raise(const FrameState& frame, Register* exc, Register* cause)
      : Raise(Raise::Kind::kRaiseWithExcAndCause, frame) {
    SetOperand(0, exc);
    SetOperand(1, cause);
  }

  Kind kind() const {
    return kind_;
  }

 private:
  const Kind kind_;
};

// Set an error by calling PyErr_Format() and then raising. This is typically
// used when a runtime assertion implemented as part of a Python opcode is hit.
class INSTR_CLASS(RaiseStatic, Operands<>, DeoptBase) {
 public:
  RaiseStatic(PyObject* exc_type, const char* fmt, const FrameState& frame)
      : InstrT(), fmt_(fmt), exc_type_(exc_type) {
    JIT_CHECK(PyExceptionClass_Check(exc_type), "Expecting exception type");
    auto new_frame = std::make_unique<FrameState>();
    new_frame->next_instr_offset = frame.next_instr_offset;
    setFrameState(std::move(new_frame));
  }

  const char* fmt() const {
    return fmt_;
  }

  PyObject* excType() const {
    return exc_type_;
  }

 private:
  const char* fmt_;
  PyObject* exc_type_;
};

class YieldBase : public Instr {
 public:
  explicit YieldBase(Opcode op) : Instr(op) {}

  void emplaceLiveOwnedReg(Register* reg) {
    live_owned_regs_.emplace_back(reg);
  }

  void emplaceLiveUnownedReg(Register* reg) {
    live_unowned_regs_.emplace_back(reg);
  }

  const std::vector<Register*>& liveOwnedRegs() const {
    return live_owned_regs_;
  }

  const std::vector<Register*>& liveUnownedRegs() const {
    return live_unowned_regs_;
  }

 private:
  std::vector<Register*> live_owned_regs_;
  std::vector<Register*> live_unowned_regs_;
};

DEFINE_SIMPLE_INSTR(YieldValue, HasOutput, Operands<1>, YieldBase);

// InitialYield causes a generator function to suspend and return a new
// 'PyGenObject' object holding its state. This should only appear in generator
// functions and there should be exactly one instance before execution begins.
DEFINE_SIMPLE_INSTR(InitialYield, HasOutput, Operands<0>, YieldBase);

class INSTR_CLASS(YieldFrom, HasOutput, Operands<2>, YieldBase) {
 public:
  YieldFrom(
      Register* out,
      Register* send_value,
      Register* iter,
      bool skip_initial_yield)
      : InstrT(out, send_value, iter),
        skip_initial_yield_(skip_initial_yield) {}

  Register* sendValue() const {
    return GetOperand(0);
  }

  Register* iter() const {
    return GetOperand(1);
  }

  bool skipInitialYield() const {
    return skip_initial_yield_;
  }

 private:
  const bool skip_initial_yield_;
};

// Implements BUILD_STRING opcode.
DEFINE_SIMPLE_INSTR(BuildString, HasOutput, Operands<>, DeoptBase);

// Implements FORMAT_VALUE opcode, which handles f-string value formatting.
class INSTR_CLASS(FormatValue, HasOutput, Operands<2>, DeoptBase) {
 public:
  FormatValue(
      Register* dst,
      Register* fmt_spec,
      Register* value,
      int conversion,
      const FrameState& frame)
      : InstrT(dst, fmt_spec, value, frame), conversion_(conversion) {}
  int conversion() const {
    return conversion_;
  }

 private:
  int conversion_;
};

// Implements `del container[sub]`
class INSTR_CLASS(DeleteSubscr, Operands<2>, DeoptBase) {
 public:
  using InstrT::InstrT;

  Register* container() const {
    return GetOperand(0);
  }

  Register* sub() const {
    return GetOperand(1);
  }
};

// Unpack a sequence as UNPACK_EX opcode and save the results
// to a tuple
class INSTR_CLASS(UnpackExToTuple, HasOutput, Operands<1>, DeoptBase) {
 public:
  UnpackExToTuple(
      Register* dst,
      Register* seq,
      int before,
      int after,
      const FrameState& frame)
      : InstrT(dst, seq, frame), before_(before), after_(after) {}

  Register* seq() const {
    return GetOperand(0);
  }

  int before() const {
    return before_;
  }
  int after() const {
    return after_;
  }

 private:
  int before_;
  int after_;
};

DEFINE_SIMPLE_INSTR(WaitHandleLoadCoroOrResult, HasOutput, Operands<1>);
DEFINE_SIMPLE_INSTR(WaitHandleLoadWaiter, HasOutput, Operands<1>);
DEFINE_SIMPLE_INSTR(WaitHandleRelease, Operands<1>);

DEFINE_SIMPLE_INSTR(IsErrStopAsyncIteration, HasOutput, Operands<0>);

DEFINE_SIMPLE_INSTR(ClearError, Operands<0>);

class CFG;

class BasicBlock {
 public:
  BasicBlock() : BasicBlock(0) {}
  explicit BasicBlock(int id_) : id(id_), cfg(nullptr) {}
  ~BasicBlock();

  // Split this block after instr. Once split, this block will contain all
  // instructions up to and including instr. A newly allocated block is returned
  // that contains all instructions following instr.
  BasicBlock* splitAfter(Instr& instr);

  // Replace any references to old_pred in this block's Phis with new_pred.
  void fixupPhis(BasicBlock* old_pred, BasicBlock* new_pred);
  // Adds a new predecessor to the phi that follows from the old predecessor
  void addPhiPredecessor(BasicBlock* old_pred, BasicBlock* new_pred);

  // Read-only access to the incoming and outgoing edges.
  const std::unordered_set<const Edge*>& in_edges() const {
    return in_edges_;
  }
  const std::unordered_set<const Edge*>& out_edges() const {
    return out_edges_;
  }

  // Append or prepend an instruction to the instructions in the basic block.
  //
  // NB: The block takes ownership of the insruction and frees it when the block
  //     is deleted.
  Instr* Append(Instr* instr);
  void push_front(Instr* instr);

  template <typename T, typename... Args>
  T* append(Args&&... args) {
    T* instr = T::create(std::forward<Args>(args)...);
    Append(instr);
    return instr;
  }

  template <typename T, typename... Args>
  T* appendWithOff(int bc_off, Args&&... args) {
    auto instr = append<T>(std::forward<Args>(args)...);
    instr->setBytecodeOffset(bc_off);
    return instr;
  }

  template <typename T, typename... Args>
  T* push_front(Args&&... args) {
    T* instr = T::create(std::forward<Args>(args)...);
    push_front(instr);
    return instr;
  }

  void retargetPreds(BasicBlock* target) {
    JIT_CHECK(target != this, "Can't retarget to self");
    for (auto it = in_edges_.begin(); it != in_edges_.end();) {
      auto edge = *it;
      ++it;
      const_cast<Edge*>(edge)->set_to(target);
    }
  }

  BasicBlock* successor(std::size_t i) const {
    return GetTerminator()->successor(i);
  }

  void set_successor(std::size_t i, BasicBlock* succ) {
    GetTerminator()->set_successor(i, succ);
  }

  // BasicBlock holds a list of instructions, delegating most operations
  // directly to its IntrusiveList.
  auto empty() const {
    return instrs_.IsEmpty();
  }
  auto& front() {
    return instrs_.Front();
  }
  auto& back() {
    return instrs_.Back();
  }
  auto& back() const {
    return instrs_.Back();
  }
  auto iterator_to(Instr& instr) {
    return instrs_.iterator_to(instr);
  }
  auto begin() {
    return instrs_.begin();
  }
  auto begin() const {
    return instrs_.begin();
  }
  auto end() {
    return instrs_.end();
  }
  auto end() const {
    return instrs_.end();
  }
  auto reverse_iterator_to(Instr& instr) {
    return instrs_.reverse_iterator_to(instr);
  }
  auto rbegin() {
    return instrs_.rbegin();
  }
  auto rbegin() const {
    return instrs_.rbegin();
  }
  auto rend() {
    return instrs_.rend();
  }
  auto rend() const {
    return instrs_.rend();
  }

  // Return the snapshot on entry to this block
  Snapshot* entrySnapshot();

  // Return the last instruction in the block
  Instr* GetTerminator();
  const Instr* GetTerminator() const {
    return const_cast<BasicBlock*>(this)->GetTerminator();
  }

  // A trampoline block consists of a single direct jump to another block
  bool IsTrampoline();

  void Print() const;

  // Call f with each Phi instruction at the beginning of this block.
  template <typename F>
  void forEachPhi(F f) {
    for (auto& instr : *this) {
      if (!instr.IsPhi()) {
        break;
      }
      f(static_cast<Phi&>(instr));
    }
  }

  int id;

  // CFG that this block belongs to; may be NULL
  CFG* cfg;

  // Basic blocks belong to a list of all blocks in their CFG
  IntrusiveListNode cfg_node;

 private:
  DISALLOW_COPY_AND_ASSIGN(BasicBlock);

  friend class Edge;

  // Instructions for this basic block.
  //
  // The last instruction is guaranteed to be a terminator, which must be one
  // of:
  //
  // - Branch
  // - CondBranch
  // - Return
  //
  IntrusiveList<Instr, &Instr::block_node_> instrs_;

  // Outgoing edges.
  std::unordered_set<const Edge*> out_edges_;

  // Incoming edges.
  std::unordered_set<const Edge*> in_edges_;
};

class Function;

class CFG {
 public:
  CFG() = default;
  ~CFG();

  // Allocate a new basic block and insert it into this CFG
  BasicBlock* AllocateBlock();

  // Allocate a block without linking it into the CFG
  BasicBlock* AllocateUnlinkedBlock();

  // Insert a block into the CFG. The CFG takes ownership and will free it
  // upon destruction of the CFG.
  void InsertBlock(BasicBlock* block);

  // Remove block from the CFG
  void RemoveBlock(BasicBlock* block);

  // Remove blocks that aren't reachable from the entry, whether or not they're
  // empty.
  void removeUnreachableBlocks();

  // Split any critical edges by inserting trampoline blocks.
  void splitCriticalEdges();

  // Remove any blocks that consist of a single jump to another block
  void RemoveTrampolineBlocks();

  // Replace cond branches where both sides of branch go to the same block with
  // a direct branch
  void simplifyRedundantCondBranches();

  // Return the RPO traversal of the basic blocks in the CFG starting from
  // entry_block.
  std::vector<BasicBlock*> GetRPOTraversal() const;

  // Entry point into the CFG; may be null
  BasicBlock* entry_block{nullptr};

  // List of all blocks in the CFG
  IntrusiveList<BasicBlock, &BasicBlock::cfg_node> blocks;

  // The Function this CFG belongs to. May be null in tests.
  Function* func{nullptr};

 private:
  DISALLOW_COPY_AND_ASSIGN(CFG);

  int next_block_id_{0};
};

class Environment {
 public:
  using RegisterMap = std::unordered_map<int, std::unique_ptr<Register>>;

  Environment() = default;

  Register* AllocateRegister();

  const RegisterMap& GetRegisters() const;

  // Only intended to be used in tests and parsing code
  Register* addRegister(std::unique_ptr<Register> reg);

  // Returns nullptr if a register with the given `id` isn't found
  Register* getRegister(int id);

  int nextRegisterId() const {
    return next_register_id_;
  }

  void setNextRegisterId(int id) {
    next_register_id_ = id;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Environment);

  RegisterMap registers_;
  int next_register_id_{0};
};

enum class FrameMode {
  kNone,
  kNormal,
  kShadow,
};

// Does the given code object need access to its containing PyFunctionObject at
// runtime?
bool usesRuntimeFunc(BorrowedRef<PyCodeObject> code);

class Function {
 public:
  Function();
  ~Function();

  Ref<PyCodeObject> code;
  Ref<PyDictObject> globals;
  Ref<PyDictObject> builtins;

  // Fully-qualified name of the function
  std::string fullname;

  // Does this function need its PyFunctionObject* at runtime?
  bool uses_runtime_func{false};

  // Return type
  Type return_type{TObject};

  FrameMode frameMode{FrameMode::kNormal};

  CFG cfg;

  Environment env;

  // Return the total number of arguments (positional + kwonly + varargs +
  // varkeywords)
  int numArgs() const;

  // Return the number of locals + cellvars + freevars
  Py_ssize_t numVars() const;

  // Set code and a number of other members that are derived from it.
  void setCode(BorrowedRef<PyCodeObject> code);

  void Print() const;

  // Count the number of instructions that match the predicate
  std::size_t CountInstrs(InstrPredicate pred) const;

  // Does this function return a primitive type?
  bool returnsPrimitive() const {
    return return_type <= TPrimitive;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Function);
};

FrameState* get_frame_state(Instr& instr);
const FrameState* get_frame_state(const Instr& instr);

}; // namespace hir
}; // namespace jit

#endif // JIT_HIR_H
