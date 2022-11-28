// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/codegen/code_section.h"
#include "Jit/lir/instruction.h"

#include <algorithm>
#include <list>
#include <memory>
#include <vector>

namespace jit::hir {
class Instr;
} // namespace jit::hir

namespace jit::lir {

class Function;

// Basic block class for LIR
class BasicBlock {
 public:
  explicit BasicBlock(Function* func);

  int id() const {
    return id_;
  }

  Function* function() {
    return func_;
  }
  const Function* function() const {
    return func_;
  }

  void addSuccessor(BasicBlock* bb) {
    successors_.push_back(bb);
    bb->predecessors_.push_back(this);
  }

  // Set successor at index to bb.
  // Expects index to be within the current size of successors.
  void setSuccessor(size_t index, BasicBlock* bb);

  // insert a basic block on the edge between the current basic
  // block and another basic block specified by block.
  BasicBlock* insertBasicBlockBetween(BasicBlock* block);

  std::vector<BasicBlock*>& successors() {
    return successors_;
  }

  const std::vector<BasicBlock*>& successors() const {
    return successors_;
  }

  void swapSuccessors() {
    if (successors_.size() < 2) {
      return;
    }

    JIT_DCHECK(successors_.size() == 2, "Should at most have two successors.");
    std::swap(successors_[0], successors_[1]);
  }

  BasicBlock* getTrueSuccessor() const {
    return successors_[0];
  }

  BasicBlock* getFalseSuccessor() const {
    return successors_[1];
  }

  std::vector<BasicBlock*>& predecessors() {
    return predecessors_;
  }

  const std::vector<BasicBlock*>& predecessors() const {
    return predecessors_;
  }

  using InstrList = std::list<std::unique_ptr<Instruction>>;

  // Allocate an instruction and its operands and append it to the
  // instruction list. For the details on how to allocate instruction
  // operands, please refer to Instruction::addOperands() function.
  template <typename... T>
  Instruction* allocateInstr(
      Instruction::Opcode opcode,
      const hir::Instr* origin,
      T&&... args) {
    instrs_.emplace_back(std::make_unique<Instruction>(this, opcode, origin));
    auto instr = instrs_.back().get();

    instr->addOperands(std::forward<T>(args)...);
    return instr;
  }

  // Allocate an instruction and its operands and insert it before the
  // instruction specified by iter. For the details on how to allocate
  // instruction operands, please refer to Instruction::addOperands() function.
  template <typename... T>
  Instruction* allocateInstrBefore(
      InstrList::iterator iter,
      Instruction::Opcode opcode,
      T&&... args) {
    const hir::Instr* origin = nullptr;
    if (iter != instrs_.end()) {
      origin = (*iter)->origin();
    } else if (iter != instrs_.begin()) {
      origin = (*std::prev(iter))->origin();
    }

    auto instr = std::make_unique<Instruction>(this, opcode, origin);
    auto res = instr.get();
    instrs_.emplace(iter, std::move(instr));

    res->addOperands(std::forward<T>(args)...);
    return res;
  }

  void appendInstr(std::unique_ptr<Instruction> instr) {
    instrs_.emplace_back(std::move(instr));
  }

  std::unique_ptr<Instruction> removeInstr(InstrList::iterator iter) {
    auto instr = std::move(*iter);
    instrs_.erase(iter);
    return instr;
  }

  InstrList& instructions() {
    return instrs_;
  }

  const InstrList& instructions() const {
    return instrs_;
  }

  bool isEmpty() const {
    return instrs_.empty();
  }

  size_t getNumInstrs() const {
    return instrs_.size();
  }

  Instruction* getFirstInstr() {
    return instrs_.empty() ? nullptr : instrs_.begin()->get();
  }

  const Instruction* getFirstInstr() const {
    return instrs_.empty() ? nullptr : instrs_.begin()->get();
  }

  Instruction* getLastInstr() {
    return instrs_.empty() ? nullptr : instrs_.rbegin()->get();
  }

  const Instruction* getLastInstr() const {
    return instrs_.empty() ? nullptr : instrs_.rbegin()->get();
  }

  InstrList::iterator getLastInstrIter() {
    return instrs_.empty() ? instrs_.end() : std::prev(instrs_.end());
  }

  template <typename Func>
  void foreachPhiInstr(const Func& f) const {
    for (auto& instr : instrs_) {
      auto opcode = instr->opcode();
      if (opcode == Instruction::kPhi) {
        f(instr.get());
      }
    }
  }

  void print() const;

  // Split this block before instr.
  // Current basic block contains all instructions up to (but excluding) instr.
  // Return a new block with all instructions (including and) after instr.
  BasicBlock* splitBefore(Instruction* instr);

  // Replace any references to old_pred in this block's Phis with new_pred.
  void fixupPhis(BasicBlock* old_pred, BasicBlock* new_pred);

  jit::codegen::CodeSection section() const {
    return section_;
  }

  void setSection(jit::codegen::CodeSection section) {
    section_ = section;
  }

 private:
  int id_;
  Function* func_;

  std::vector<BasicBlock*> successors_;
  std::vector<BasicBlock*> predecessors_;

  // used in parser, expect unique id
  void setId(int id) {
    id_ = id;
  }

  friend class Parser;

  // TODO(tiansi): consider using IntrusiveList as in HIR
  InstrList instrs_;

  jit::codegen::CodeSection section_;
};

} // namespace jit::lir
