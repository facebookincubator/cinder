// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"
#include "cinder/port-assert.h"
#include "opcode.h"

#include "Jit/log.h"

#include <iterator>
#include <unordered_set>

namespace jit {

extern const std::unordered_set<int> kBranchOpcodes;
extern const std::unordered_set<int> kRelBranchOpcodes;

// A structured, immutable representation of a CPython bytecode
class BytecodeInstruction {
 public:
  BytecodeInstruction(
      _Py_CODEUNIT* instrs,
      Py_ssize_t idx,
      PyCodeObject* code) {
    offset_ = idx * sizeof(_Py_CODEUNIT);
    _Py_CODEUNIT word = instrs[idx];
    opcode_ = _Py_OPCODE(word);
    oparg_ = _Py_OPARG(word);
    const_ = OpArgTuple(code);
  }

  BytecodeInstruction(
      int opcode,
      int oparg,
      Py_ssize_t offset,
      PyCodeObject* code)
      : offset_(offset), opcode_(opcode), oparg_(oparg) {
    const_ = OpArgTuple(code);
  }

  Py_ssize_t offset() const {
    return offset_;
  }

  Py_ssize_t index() const {
    return offset() / sizeof(_Py_CODEUNIT);
  }

  int opcode() const {
    return opcode_;
  }

  int oparg() const {
    return oparg_;
  }

  int opargAsIndex() const {
    return oparg() / sizeof(_Py_CODEUNIT);
  }

  bool IsBranch() const {
    return kBranchOpcodes.count(opcode()) || IsReadonlyBranch();
  }

  bool IsCondBranch() const {
    switch (opcode_) {
      case FOR_ITER:
      case POP_JUMP_IF_FALSE:
      case JUMP_IF_FALSE_OR_POP:
      case JUMP_IF_NONZERO_OR_POP:
      case JUMP_IF_TRUE_OR_POP:
      case JUMP_IF_ZERO_OR_POP: {
        return true;
      }
      case READONLY_OPERATION: {
#ifdef CINDER_PORTING_DONE
        switch (ReadonlyOpcode()) {
          case READONLY_FOR_ITER: {
            return true;
          }
          default: {
            return false;
          }
        }
#else
        PORT_ASSERT("Needs Static Python + Readonly feature");
#endif
      }
      default: {
        return false;
      }
    }
  }

  bool IsRaiseVarargs() const {
    return opcode() == RAISE_VARARGS;
  }

  bool IsReturn() const {
    return opcode() == RETURN_VALUE;
  }

  bool IsTerminator() const {
    return IsBranch() || IsReturn() || IsRaiseVarargs();
  }

  Py_ssize_t GetJumpTarget() const {
    if (kRelBranchOpcodes.count(opcode())) {
      return NextInstrOffset() + oparg();
    }
    if (IsReadonlyOp()) {
      return ReadonlyJumpTarget();
    }
    return oparg();
  }

  Py_ssize_t GetJumpTargetAsIndex() const {
    return GetJumpTarget() / sizeof(_Py_CODEUNIT);
  }

  Py_ssize_t NextInstrOffset() const {
    return offset_ + sizeof(_Py_CODEUNIT);
  }

  Py_ssize_t NextInstrIndex() const {
    return NextInstrOffset() / sizeof(_Py_CODEUNIT);
  }

  void ExtendOpArgWith(int changes) {
    oparg_ = (changes << 8) | oparg_;
  }

  BorrowedRef<PyTupleObject> OpArgTuple(PyCodeObject* code) {
    if (code == nullptr) {
      return nullptr;
    }
    switch (opcode()) {
      case READONLY_OPERATION: {
#ifdef CINDER_PORTING_DONE
        PyObject* consts = code->co_consts;
        PyObject* op_tuple = PyTuple_GET_ITEM(consts, oparg());
        return BorrowedRef<PyTupleObject>(op_tuple);
#else
        PORT_ASSERT("Privacy features not yet ported");
#endif
      }
      default:
        return nullptr;
    }
  }

  bool IsReadonlyOp() const {
    return opcode() == READONLY_OPERATION;
  }

  int ReadonlyOpcode() const {
    if (!IsReadonlyOp()) {
      return -1;
    }
    JIT_CHECK(const_ != nullptr, "const tuple is nullptr");
    PyObject* opobj = PyTuple_GET_ITEM(const_, 0);
    JIT_CHECK(opobj != nullptr, "readonly opcode is nullptr");
    JIT_CHECK(PyLong_Check(opobj), "readonly opcode is not long");
    int ret = PyLong_AsLong(opobj);
    return ret;
  }

  bool IsReadonlyBranch() const {
#ifdef CINDER_PORTING_DONE
    int readonly_opcode = ReadonlyOpcode();

    switch (readonly_opcode) {
      case READONLY_FOR_ITER: {
        return true;
      }
      default: {
        return false;
      }
    }
#else
    if (IsReadonlyOp()) {
      PORT_ASSERT("Privacy features not yet ported");
    }
    return false;
#endif
  }

  Py_ssize_t ReadonlyJumpTarget() const {
#ifdef CINDER_PORTING_DONE
    JIT_CHECK(IsReadonlyOp(), "opcode %d is not readonly opcode", opcode_);
    switch (ReadonlyOpcode()) {
      case READONLY_FOR_ITER: {
        PyObject* jump_dist_obj = PyTuple_GET_ITEM(const_, 2);
        JIT_CHECK(jump_dist_obj != nullptr, "jmp dist is nullptr");
        int jump_dist = PyLong_AsLong(jump_dist_obj);
        return NextInstrOffset() + jump_dist;
      }
      default: {
        JIT_CHECK(false, "opcode %d is not readonly opcode", opcode_);
        return 0;
      }
    }
#else
    PORT_ASSERT("Privacy features not yet ported");
#endif
  }

 private:
  Py_ssize_t offset_;
  int opcode_;
  int oparg_;
  BorrowedRef<PyTupleObject> const_;
};

// A half open block of bytecode [start, end) viewed as a sequence of
// `BytecodeInstruction`s
//
// Extended args are handled automatically when iterating over the bytecode;
// they will not appear in the stream of `BytecodeInstruction`s.
class BytecodeInstructionBlock {
 public:
  // TODO(T126419906): co_{rawcode,codelen} should be removed as part of the
  // CinderVM work.
  explicit BytecodeInstructionBlock(PyCodeObject* code)
      : instrs_(code->co_rawcode),
        start_idx_(0),
        end_idx_(code->co_codelen / sizeof(_Py_CODEUNIT)),
        code_(code) {}

  BytecodeInstructionBlock(
      _Py_CODEUNIT* instrs,
      Py_ssize_t start,
      Py_ssize_t end,
      PyCodeObject* code)
      : instrs_(instrs), start_idx_(start), end_idx_(end), code_(code) {}

  class Iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = BytecodeInstruction;
    using pointer = const value_type*;
    using reference = const value_type&;

    Iterator(
        _Py_CODEUNIT* instr,
        Py_ssize_t idx,
        Py_ssize_t end_idx,
        BorrowedRef<PyCodeObject> code)
        : instr_(instr),
          idx_(idx),
          end_idx_(end_idx),
          bci_(0, 0, 0, 0),
          code_(code) {
      if (!atEnd()) {
        // Iterator end() methods are supposed to be past the logical end
        // of the underlying data structure and should not be accessed
        // directly. Dereferencing instr would be a heap buffer overflow.
        bci_ = BytecodeInstruction(
            _Py_OPCODE(*instr),
            _Py_OPARG(*instr),
            idx * sizeof(_Py_CODEUNIT),
            code_);
        consumeExtendedArgs();
      }
    }

    bool atEnd() const {
      return idx_ == end_idx_;
    }

    reference operator*() {
      JIT_DCHECK(
          !atEnd(), "cannot read past the end of BytecodeInstructionBlock");
      return bci_;
    }

    pointer operator->() {
      JIT_DCHECK(
          !atEnd(), "cannot read past the end of BytecodeInstructionBlock");
      return &bci_;
    }

    Iterator& operator++() {
      instr_++;
      idx_++;
      consumeExtendedArgs();
      return *this;
    }

    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    bool operator==(const Iterator& other) const {
      return instr_ == other.instr_;
    }

    bool operator!=(const Iterator& other) const {
      return !(*this == other);
    }

    Py_ssize_t remainingInstrs() const {
      return end_idx_ - idx_ - 1;
    }

   private:
    void consumeExtendedArgs() {
      int accum = 0;
      while (!atEnd() && (_Py_OPCODE(*instr_) == EXTENDED_ARG)) {
        accum = (accum << 8) | _Py_OPARG(*instr_);
        instr_++;
        idx_++;
      }
      if (!atEnd()) {
        int opcode = _Py_OPCODE(*instr_);
        int oparg = (accum << 8) | _Py_OPARG(*instr_);
        bci_ = BytecodeInstruction(
            opcode, oparg, idx_ * sizeof(_Py_CODEUNIT), code_);
      }
    }

    _Py_CODEUNIT* instr_;
    Py_ssize_t idx_;
    Py_ssize_t end_idx_;
    BytecodeInstruction bci_;
    BorrowedRef<PyCodeObject> code_;
  };

  Iterator begin() const {
    return Iterator(instrs_ + start_idx_, start_idx_, end_idx_, code_);
  }

  Iterator end() const {
    return Iterator(instrs_ + end_idx_, end_idx_, end_idx_, code_);
  }

  Py_ssize_t startOffset() const {
    return start_idx_ * sizeof(_Py_CODEUNIT);
  }

  Py_ssize_t endOffset() const {
    return end_idx_ * sizeof(_Py_CODEUNIT);
  }

  Py_ssize_t size() const {
    return end_idx_ - start_idx_;
  }

  BytecodeInstruction at(Py_ssize_t idx) const {
    return BytecodeInstruction(instrs_, start_idx_ + idx, code_);
  }

  BytecodeInstruction lastInstr() const {
    return BytecodeInstruction(instrs_, end_idx_ - 1, code_);
  }

  _Py_CODEUNIT* bytecode() const {
    return instrs_;
  }

  BorrowedRef<PyCodeObject> code() const {
    return code_;
  }

 private:
  _Py_CODEUNIT* instrs_;
  Py_ssize_t start_idx_;
  Py_ssize_t end_idx_;
  BorrowedRef<PyCodeObject> code_;
};

} // namespace jit
