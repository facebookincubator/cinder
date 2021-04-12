#ifndef __HIR_PARSER_H__
#define __HIR_PARSER_H__

#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Jit/hir/hir.h"

namespace jit {
namespace hir {

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
  std::unordered_map<CondBranch*, std::pair<int, int>> cond_branches_;
  std::unordered_map<Branch*, int> branches_;
  std::unordered_map<int, std::vector<PhiInfo>> phis_;

  const char* GetNextToken() {
    JIT_CHECK(token_iter_ != tokens_.end(), "No more tokens");
    return (token_iter_++)->c_str();
  };
  const char* peekNextToken(int n = 0) {
    auto it = token_iter_;
    std::advance(it, n);
    return it->c_str();
  }
  int GetNextInteger() {
    return atoi(GetNextToken());
  };
  int GetNextNameIdx();
  RegState GetNextRegState();

  void expect(const char* expected);

  BasicBlock* ParseBasicBlock(CFG& cfg);

  Instr* parseInstr(const char* opcode, Register* dst, int bb_index);

  Register* ParseRegister();

  Register* allocateRegister(const char* name);

  void realizePhis();

  ListOrTuple parseListOrTuple();
  FrameState parseFrameState();
  std::vector<Register*> parseRegisterVector();
  std::vector<RegState> parseRegStates();

  template <class T, typename... Args>
  Instr* newInstr(Args&&... args) {
    if (strcmp(peekNextToken(), "{") != 0) {
      return T::create(std::forward<Args>(args)..., FrameState{});
    }
    expect("{");
    std::vector<RegState> reg_states;
    if (strcmp(peekNextToken(), "LiveValues") == 0) {
      expect("LiveValues");
      reg_states = parseRegStates();
    }
    FrameState fs = parseFrameState();
    auto instr = T::create(std::forward<Args>(args)..., fs);
    for (auto& rs : reg_states) {
      instr->emplaceLiveReg(rs);
    }
    return instr;
  }
};

} // namespace hir
} // namespace jit

#endif
