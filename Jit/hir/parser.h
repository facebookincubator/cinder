// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/hir/hir.h"
#include "Jit/util.h"

#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jit::hir {

class HIRParser {
 public:
  std::unique_ptr<Function> ParseHIR(const char* hir);

 private:
  enum class ListOrTuple {
    List,
    Tuple,
  };
  struct PhiInput {
    int bb;
    Register* value;
  };
  struct PhiInfo {
    Register* dst;
    std::vector<PhiInput> inputs{};
  };

  std::list<std::string> tokens_;
  std::list<std::string>::iterator token_iter_;
  Environment* env_;
  std::unordered_map<int, BasicBlock*> index_to_bb_;
  std::unordered_map<CondBranchBase*, std::pair<int, int>> cond_branches_;
  std::unordered_map<Branch*, int> branches_;
  std::unordered_map<int, std::vector<PhiInfo>> phis_;
  int max_reg_id_{0};

  std::string_view GetNextToken() {
    JIT_CHECK(token_iter_ != tokens_.end(), "No more tokens");
    return *(token_iter_++);
  }

  std::string_view peekNextToken(int n = 0) const {
    auto it = token_iter_;
    std::advance(it, n);
    return *it;
  }

  int GetNextInteger() {
    auto token = GetNextToken();
    auto n = parseInt<int>(token);
    JIT_CHECK(n.has_value(), "Cannot parse '%s' as an integer", token);
    return *n;
  }

  int GetNextNameIdx();
  RegState GetNextRegState();
  BorrowedRef<> GetNextUnicode();

  void expect(std::string_view expected);

  BasicBlock* ParseBasicBlock(CFG& cfg);

  Instr* parseInstr(std::string_view opcode, Register* dst, int bb_index);

  Register* ParseRegister();

  Register* allocateRegister(std::string_view name);

  void realizePhis();

  ListOrTuple parseListOrTuple();
  FrameState parseFrameState();
  std::vector<Register*> parseRegisterVector();
  std::vector<RegState> parseRegStates();

  template <class T, typename... Args>
  Instr* newInstr(Args&&... args) {
    if (peekNextToken() != "{") {
      return T::create(std::forward<Args>(args)..., FrameState{});
    }
    expect("{");
    std::vector<RegState> reg_states;
    if (peekNextToken() == "LiveValues") {
      expect("LiveValues");
      reg_states = parseRegStates();
    }
    FrameState fs;
    if (peekNextToken() == "FrameState") {
      expect("FrameState");
      fs = parseFrameState();
    }
    expect("}");
    auto instr = T::create(std::forward<Args>(args)..., fs);
    for (auto& rs : reg_states) {
      instr->emplaceLiveReg(rs);
    }
    return instr;
  }
};

} // namespace jit::hir
