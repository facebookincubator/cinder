// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/codegen/annotations.h"

#include "Jit/disassembler.h"
#include "Jit/hir/printer.h"

#include <map>
#include <utility>

namespace jit {
namespace codegen {

std::string Annotations::disassemble(
    void* entry,
    const asmjit::CodeHolder& code) {
  JIT_CHECK(
      g_dump_asm, "Annotations are not recorded without -X jit-disas-funcs");
  auto text = code.textSection();
  JIT_DCHECK(text->offset() == 0, "Unexpected .text section offset");
  auto base = static_cast<const char*>(entry);
  auto size = text->realSize();

  std::map<const char*, std::pair<Annotation*, const char*>> annot_bounds;
  for (auto& annot : annotations_) {
    auto begin = base + code.labelOffsetFromBase(annot.begin);
    auto end = base + code.labelOffsetFromBase(annot.end);
    auto inserted =
        annot_bounds.emplace(begin, std::make_pair(&annot, end)).second;
    JIT_DCHECK(inserted, "Duplicate start address for annotation");
  }

  Annotation* prev_annot = nullptr;
  auto annot_it = annot_bounds.begin();
  const char* annot_end = nullptr;

  std::string result;
  Disassembler dis(base, size);
  dis.setPrintInstBytes(false);
  for (auto cursor = base, end = cursor + size; cursor < end;) {
    auto new_annot = prev_annot;
    // If we're not out of annotations and we've crossed the start of the next
    // one, switch to it.
    if (annot_it != annot_bounds.end() && cursor >= annot_it->first) {
      new_annot = annot_it->second.first;
      JIT_DCHECK(
          new_annot->instr == nullptr || new_annot->str.empty(),
          "Annotations with both an instruction and str aren't yet supported");
      annot_end = annot_it->second.second;
      ++annot_it;
    }
    // If we've reached the end of the annotation, clear it.
    if (cursor >= annot_end) {
      new_annot = nullptr;
    }

    // If our annotation has changed since the last instruction, add it to the
    // end of the line.
    if (new_annot != prev_annot) {
      std::string annot_str;
      if (new_annot == nullptr) {
        annot_str = "--unassigned--";
      } else {
        auto new_hir = new_annot->instr;
        auto prev_hir = prev_annot ? prev_annot->instr : nullptr;
        if (new_hir != nullptr && new_hir != prev_hir) {
          annot_str = hir::HIRPrinter().ToString(*new_hir);
        } else if (!new_annot->str.empty()) {
          annot_str = new_annot->str;
        }
      }
      if (!annot_str.empty()) {
        format_to(result, "\n{}\n", annot_str);
      }
      prev_annot = new_annot;
    }

    // Print the raw instruction.
    int length;
    format_to(result, "  {}\n", dis.disassembleOne(&length));
    cursor += length;
  }

  return result;
}

} // namespace codegen
} // namespace jit
