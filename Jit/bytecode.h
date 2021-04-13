#ifndef JIT_BYTECODE_H
#define JIT_BYTECODE_H

#include <iterator>
#include <unordered_set>
#include "Python.h"
#include "opcode.h"

namespace jit {

extern const std::unordered_set<int> kBranchOpcodes;
extern const std::unordered_set<int> kRelBranchOpcodes;

// A structured, immutable representation of a CPython bytecode
class BytecodeInstruction {
 public:
  BytecodeInstruction(_Py_CODEUNIT* instrs, Py_ssize_t idx) {
    offset_ = idx * sizeof(_Py_CODEUNIT);
    _Py_CODEUNIT word = instrs[idx];
    opcode_ = _Py_OPCODE(word);
    oparg_ = _Py_OPARG(word);
  }

  BytecodeInstruction(int opcode, int oparg, Py_ssize_t offset)
      : offset_(offset), opcode_(opcode), oparg_(oparg) {}

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
    return kBranchOpcodes.count(opcode());
  }

  bool IsCondBranch() const {
    // TODO(mpage): Fill this out
    switch (opcode_) {
      case FOR_ITER:
      case POP_JUMP_IF_FALSE:
      case JUMP_IF_FALSE_OR_POP:
      case JUMP_IF_NONZERO_OR_POP:
      case JUMP_IF_TRUE_OR_POP:
      case JUMP_IF_ZERO_OR_POP: {
        return true;
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

 private:
  Py_ssize_t offset_;
  int opcode_;
  int oparg_;
};

// A half open block of bytecode [start, end) viewed as a sequence of
// `BytecodeInstruction`s
//
// Extended args are handled automatically when iterating over the bytecode;
// they will not appear in the stream of `BytecodeInstruction`s.
class BytecodeInstructionBlock {
 public:
  explicit BytecodeInstructionBlock(PyCodeObject* code)
      : instrs_(code->co_rawcode),
        start_idx_(0),
        end_idx_(code->co_codelen / sizeof(_Py_CODEUNIT)) {}

  BytecodeInstructionBlock(
      _Py_CODEUNIT* instrs,
      Py_ssize_t start,
      Py_ssize_t end)
      : instrs_(instrs), start_idx_(start), end_idx_(end) {}

  class Iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = BytecodeInstruction;
    using pointer = const value_type*;
    using reference = const value_type&;

    Iterator(_Py_CODEUNIT* instr, Py_ssize_t idx, Py_ssize_t end_idx)
        : instr_(instr),
          idx_(idx),
          end_idx_(end_idx),
          bci_(
              _Py_OPCODE(*instr),
              _Py_OPARG(*instr),
              idx * sizeof(_Py_CODEUNIT)) {
      consumeExtendedArgs();
    }

    reference operator*() {
      return bci_;
    }

    pointer operator->() {
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

   private:
    void consumeExtendedArgs() {
      int accum = 0;
      while ((idx_ != end_idx_) && (_Py_OPCODE(*instr_) == EXTENDED_ARG)) {
        accum = (accum << 8) | _Py_OPARG(*instr_);
        instr_++;
        idx_++;
      }
      if (idx_ != end_idx_) {
        int opcode = _Py_OPCODE(*instr_);
        int oparg = (accum << 8) | _Py_OPARG(*instr_);
        bci_ = BytecodeInstruction(opcode, oparg, idx_ * sizeof(_Py_CODEUNIT));
      }
    }

    _Py_CODEUNIT* instr_;
    Py_ssize_t idx_;
    Py_ssize_t end_idx_;
    BytecodeInstruction bci_;
  };

  Iterator begin() const {
    return Iterator(instrs_ + start_idx_, start_idx_, end_idx_);
  }

  Iterator end() const {
    return Iterator(instrs_ + end_idx_, end_idx_, end_idx_);
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
    return BytecodeInstruction(instrs_, start_idx_ + idx);
  }

  BytecodeInstruction lastInstr() const {
    return BytecodeInstruction(instrs_, end_idx_ - 1);
  }

  _Py_CODEUNIT* bytecode() const {
    return instrs_;
  }

 private:
  _Py_CODEUNIT* instrs_;
  Py_ssize_t start_idx_;
  Py_ssize_t end_idx_;
};

} // namespace jit

#endif
