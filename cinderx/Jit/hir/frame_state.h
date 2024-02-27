// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"
#include "cinderx/Common/log.h"

#include "cinderx/Jit/bytecode.h"
#include "cinderx/Jit/hir/register.h"
#include "cinderx/Jit/stack.h"

namespace jit::hir {

// An entry in the CPython block stack
struct ExecutionBlock {
  // The CPython opcode for the block
  int opcode;

  // Offset in the bytecode of the handler for this block
  BCOffset handler_off;

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
    return opcode == SETUP_FINALLY &&
        instrs.at(handler_off).opcode() == END_ASYNC_FOR;
  }
};

using BlockStack = jit::Stack<ExecutionBlock>;
using OperandStack = jit::Stack<Register*>;

// The abstract state of the python frame
struct FrameState {
  FrameState() = default;
  FrameState(const FrameState& other) {
    *this = other;
  }
  FrameState& operator=(const FrameState& other) {
    next_instr_offset = other.next_instr_offset;
    locals = other.locals;
    cells = other.cells;
    stack = other.stack;
    JIT_DCHECK(
        this != other.parent, "FrameStates should not be self-referential");
    parent = other.parent;
    block_stack = other.block_stack;
    code = other.code;
    globals = other.globals;
    builtins = other.builtins;
    return *this;
  }
  FrameState(
      BorrowedRef<PyCodeObject> code,
      BorrowedRef<PyDictObject> globals,
      BorrowedRef<PyDictObject> builtins,
      FrameState* parent)
      : code(code), globals(globals), builtins(builtins), parent(parent) {
    JIT_DCHECK(this != parent, "FrameStates should not be self-referential");
  }
  // Used for testing only.
  explicit FrameState(int bc_off) : next_instr_offset(bc_off) {}

  // If the function is inlined into another function, the depth at which it
  // is inlined (nested function calls may be inlined). Starts at 1. If the
  // function is not inlined, 0.
  int inlineDepth() const {
    int inline_depth = -1;
    const FrameState* frame = this;
    while (frame != nullptr) {
      frame = frame->parent;
      inline_depth++;
    }
    JIT_DCHECK(
        inline_depth >= 0,
        "expected positive inline depth but got {}",
        inline_depth);
    return inline_depth;
  }

  // The bytecode offset of the next instruction to be executed once control has
  // transferred to the interpreter.
  BCOffset next_instr_offset{0};

  // Local variables
  std::vector<Register*> locals;

  // Cells for cellvars (used by closures of inner functions) and freevars (our
  // closure)
  std::vector<Register*> cells;

  OperandStack stack;
  BlockStack block_stack;
  BorrowedRef<PyCodeObject> code;
  BorrowedRef<PyDictObject> globals;
  BorrowedRef<PyDictObject> builtins;
  // Points to the FrameState, if any, into which this was inlined. Used to
  // construct the metadata needed to reify PyFrameObjects for inlined
  // functions during e.g. deopt.
  FrameState* parent{nullptr};

  // The bytecode offset of the current instruction, or -sizeof(_Py_CODEUNIT) if
  // no instruction has executed. This corresponds to the `f_lasti` field of
  // PyFrameObject.
  BCOffset instr_offset() const {
    return std::max(
        next_instr_offset - int{sizeof(_Py_CODEUNIT)},
        BCOffset{-int{sizeof(_Py_CODEUNIT)}});
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
    if (parent != nullptr) {
      return parent->visitUses(func);
    }
    return true;
  }

  bool operator==(const FrameState& other) const {
    return (next_instr_offset == other.next_instr_offset) &&
        (stack == other.stack) && (block_stack == other.block_stack) &&
        (locals == other.locals) && (cells == other.cells) &&
        (code == other.code);
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

} // namespace jit::hir
