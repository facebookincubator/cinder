#ifndef __LIR_BBBUILDER_H__
#define __LIR_BBBUILDER_H__

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/format.h"

#include "Jit/codegen/environ.h"

#include "Jit/hir/hir.h"
#include "Jit/lir/lir.h"

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// bbbuilder.h with some interfaces changes so that it works with the new
// LIR.

namespace jit {
namespace lir {

class BasicBlockBuilder {
 public:
  BasicBlockBuilder(jit::codegen::Environ* env, Function* func);

  void setCurrentInstr(const hir::Instr* inst) {
    cur_hir_instr_ = inst;
  }

  void AppendCode(const std::string& s);

  template <typename... T>
  void AppendCode(const char* s, T&&... args) {
    AppendCode(fmt::format(s, std::forward<T>(args)...));
  }

  Instruction* createInstr(Instruction::Opcode opcode);

  Instruction* getDefInstr(const std::string& name);

  void CreateInstrInput(Instruction* instr, const std::string& name_size);
  void CreateInstrImmediateInput(
      Instruction* instr,
      const std::string& val_size);
  void CreateInstrOutput(Instruction* instr, const std::string& name_size);

  void CreateInstrIndirect(
      Instruction* instr,
      const std::string& name_size,
      int offset);
  void CreateInstrIndirectOutput(
      Instruction* instr,
      const std::string& name_size,
      int offset);

  std::vector<BasicBlock*> Generate() {
    return bbs_;
  }

 private:
  const hir::Instr* cur_hir_instr_{nullptr};
  BasicBlock* cur_bb_;
  std::vector<BasicBlock*> bbs_;
  jit::codegen::Environ* env_;
  Function* func_;
  std::unordered_map<std::string, BasicBlock*> label_to_bb_;

  BasicBlock* GetBasicBlockByLabel(const std::string& label);

  void AppendCodeLine(const std::string& s);
  bool IsConstant(const std::string& s) {
    return isdigit(s[0]) || (s[0] == '-');
  }

  static bool IsLabel(const std::string& s) {
    return s.back() == ':';
  }
  static std::vector<std::string> Tokenize(const std::string& s);
};

} // namespace lir
} // namespace jit

#endif
