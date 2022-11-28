// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/codegen/code_section.h"
#include "Jit/lir/instruction.h"

#include <asmjit/asmjit.h>
#include <json.hpp>

#include <string>
#include <vector>

namespace jit {
namespace hir {
class Instr;
}

namespace codegen {

// An annotation for a region of assembly code, containing the HIR instruction
// the region came from and/or a text description of its contents.
struct Annotation {
  Annotation(const hir::Instr* instr, asmjit::Label begin, asmjit::Label end)
      : instr(instr), begin(begin), end(end) {
    JIT_DCHECK(instr != nullptr, "instr can't be null");
  }

  Annotation(std::string str, asmjit::Label begin, asmjit::Label end)
      : str(std::move(str)), begin(begin), end(end) {
    JIT_DCHECK(!this->str.empty(), "str can't be empty");
  }

  const hir::Instr* const instr = nullptr;
  std::string const str;
  asmjit::Label const begin;
  asmjit::Label const end;
};

class Annotations {
 public:
  // If any code has been emitted since start_cursor, add an annotation region
  // ending at the current position with the given LIR instruction or text
  // description.
  template <typename T>
  void add(T&& item, asmjit::x86::Builder* as, asmjit::BaseNode* start_cursor) {
    if (!g_dump_asm && !g_dump_hir_passes_json) {
      return;
    }
    auto end_cursor = as->cursor();
    if (start_cursor != end_cursor) {
      asmjit::Label start = as->newLabel();
      as->setCursor(start_cursor);
      as->bind(start);
      asmjit::Label end = as->newLabel();
      as->setCursor(end_cursor);
      as->bind(end);
      annotations_.emplace_back(
          canonicalize(std::forward<T>(item)), start, end);
    }
  }

  // Return an annotated disassembly of the given code.
  std::string disassemble(void* entry, const asmjit::CodeHolder& code);

  // Disassemble JSON representation of the given code.
  void disassembleJSON(
      nlohmann::json& json,
      void* entry,
      const asmjit::CodeHolder& code);

 private:
  // Annotations mapping Label ranges to either an LIR instruction or a string
  // description.
  std::vector<Annotation> annotations_;

  static const std::string& canonicalize(const std::string& str) {
    return str;
  }
  static const hir::Instr* canonicalize(const lir::Instruction* instr) {
    return instr->origin();
  }

  std::string disassembleSection(
      void* entry,
      const asmjit::CodeHolder& code,
      CodeSection section);

  void disassembleSectionJSON(
      nlohmann::json& json,
      void* entry,
      const asmjit::CodeHolder& code,
      CodeSection section);
};

} // namespace codegen
} // namespace jit
