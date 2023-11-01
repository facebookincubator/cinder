// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/codegen/annotations.h"

#include "Jit/codegen/code_section.h"
#include "Jit/disassembler.h"
#include "Jit/hir/printer.h"

#include <map>
#include <utility>

namespace jit {
namespace codegen {

std::string Annotations::disassembleSection(
    void* entry,
    const asmjit::CodeHolder& code,
    CodeSection section) {
  // i386-dis is not thread-safe
  JIT_CHECK(
      g_dump_asm, "Annotations are not recorded without -X jit-disas-funcs");
  auto text = code.sectionByName(codeSectionName(section));
  if (text == nullptr) {
    return "";
  }
  auto base = static_cast<const char*>(entry);
  auto section_start = base + text->offset();
  auto size = text->realSize();

  std::map<const char*, std::pair<Annotation*, const char*>> annot_bounds;
  for (auto& annot : annotations_) {
    auto begin = base + code.labelOffsetFromBase(annot.begin);
    auto end = base + code.labelOffsetFromBase(annot.end);
    if (begin < section_start || end > section_start + size) {
      // Ensure that we only consider annotations that correspond to the section
      // we're looking at.
      continue;
    }
    auto inserted =
        annot_bounds.emplace(begin, std::make_pair(&annot, end)).second;
    JIT_DCHECK(inserted, "Duplicate start address for annotation");
  }

  Annotation* prev_annot = nullptr;
  auto annot_it = annot_bounds.begin();
  const char* annot_end = nullptr;

  std::string result;
  Disassembler dis(section_start, size);
  dis.setPrintInstBytes(false);
  for (auto cursor = section_start, end = cursor + size; cursor < end;) {
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

std::string Annotations::disassemble(
    void* entry,
    const asmjit::CodeHolder& code) {
  ThreadedCompileSerialize guard;
  JIT_CHECK(code.hasBaseAddress(), "code not generated!");
  std::string result;
  forEachSection([&](CodeSection section) {
    result += disassembleSection(entry, code, section);
  });
  return result;
}

/* Disassembles the section of the code at the given section, writing results
   to the `blocks` parameter.
 */
void Annotations::disassembleSectionJSON(
    nlohmann::json& blocks,
    void* entry,
    const asmjit::CodeHolder& code,
    CodeSection section) {
  asmjit::Section* text = code.sectionByName(codeSectionName(section));
  if (text == nullptr) {
    return;
  }
  auto base = static_cast<const char*>(entry);
  auto section_start = base + text->offset();
  uint64_t size = text->realSize();

  std::map<const char*, std::pair<Annotation*, const char*>> annot_bounds;
  for (auto& annot : annotations_) {
    auto begin = base + code.labelOffsetFromBase(annot.begin);
    auto end = base + code.labelOffsetFromBase(annot.end);
    if (begin < section_start || end > section_start + size) {
      // Ensure that we only consider annotations that correspond to the section
      // we're looking at.
      continue;
    }
    auto inserted =
        annot_bounds.emplace(begin, std::make_pair(&annot, end)).second;
    JIT_DCHECK(inserted, "Duplicate start address for annotation");
  }

  Annotation* prev_annot = nullptr;
  auto annot_it = annot_bounds.begin();
  const char* annot_end = nullptr;

  Disassembler dis(section_start, size);
  dis.setPrintAddr(false);
  dis.setPrintInstBytes(false);
  nlohmann::json block;
  for (const char *cursor = section_start, *end = cursor + size;
       cursor < end;) {
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
      bool new_block = true;
      std::string annot_str;
      nlohmann::json origin;
      if (new_annot == nullptr) {
        annot_str = "--unassigned--";
      } else {
        auto new_hir = new_annot->instr;
        auto prev_hir = prev_annot ? prev_annot->instr : nullptr;
        if (new_hir != nullptr && new_hir != prev_hir) {
          origin = hir::JSONPrinter().Print(*new_hir);
        } else if (!new_annot->str.empty()) {
          annot_str = new_annot->str;
        } else {
          new_block = false;
        }
      }
      if (new_block) {
        if (prev_annot != nullptr) {
          blocks.emplace_back(block);
        }
        block = nlohmann::json();
        block["instrs"] = nlohmann::json::array();
        if (origin != nlohmann::json()) {
          block["origin"] = origin;
        }
        if (!annot_str.empty()) {
          block["name"] = annot_str;
        }
      }
      prev_annot = new_annot;
    }

    // TODO(emacs): Store and use LIR instruction instead
    // Fetch a linenumber off the origin HIR instruction
    auto hir_instr = new_annot ? new_annot->instr : nullptr;
    nlohmann::json instr;
    // Print the raw instruction.
    if (hir_instr != nullptr) {
      instr["line"] = hir_instr->lineNumber();
    }
    int length;
    instr["address"] = dis.codeAddress();
    instr["opcode"] = dis.disassembleOne(&length);
    block["instrs"].emplace_back(instr);
    cursor += length;
    prev_annot = new_annot;
  }
  // There might be a leftover block that we need to add.
  if (block["instrs"].size() > 0) {
    blocks.emplace_back(block);
  }
}

void Annotations::disassembleJSON(
    nlohmann::json& json,
    void* entry,
    const asmjit::CodeHolder& code) {
  // i386-dis is not thread-safe
  ThreadedCompileSerialize guard;
  nlohmann::json blocks;

  forEachSection([&](CodeSection section) {
    disassembleSectionJSON(blocks, entry, code, section);
  });

  nlohmann::json result;
  result["name"] = "Assembly";
  result["type"] = "asm";
  result["blocks"] = blocks;

  json["cols"].emplace_back(result);
}

} // namespace codegen
} // namespace jit
