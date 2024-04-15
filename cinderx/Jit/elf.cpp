// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/Jit/elf.h"

#include "cinderx/Common/log.h"
#include "cinderx/Common/util.h"

#include <ostream>

namespace jit::elf {

namespace {

// ELF structures are all expected to be a set size.
static_assert(sizeof(SectionHeader) == 64);
static_assert(sizeof(SegmentHeader) == 56);
static_assert(sizeof(FileHeader) == FileHeader{}.header_size);

// TODO(T176630720): This should not be a hardcoded value.
constexpr uint64_t kTextStartAddress = 0x1000000;

void initFileHeader(Object& elf) {
  FileHeader& header = elf.file_header;
  header.segment_header_offset = offsetof(Object, segment_headers);
  header.segment_header_count = raw(SegmentIdx::kTotal);
  header.section_header_offset = offsetof(Object, section_headers);
  header.section_header_count = raw(SectionIdx::kTotal);
  header.section_name_index = raw(SectionIdx::kShstrtab);
}

void initTextSection(Object& elf, const std::vector<CodeEntry>& entries) {
  uint64_t text_end_address = kTextStartAddress;

  for (const CodeEntry& entry : entries) {
    Symbol sym;
    sym.name_offset = elf.strtab.insert(entry.func_name);
    sym.info = kGlobal | kFunc;
    sym.section_index = raw(SectionIdx::kText);
    sym.address = text_end_address;
    sym.size = entry.code.size();
    elf.symtab.insert(std::move(sym));

    // TODO(T176630885): Not writing the filename or lineno yet.

    text_end_address += entry.code.size();
  }

  size_t text_size = text_end_address - kTextStartAddress;

  // Program bits. Occupies memory and is executable.  Text immediately follows
  // the section header table.
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kText);
  header.name_offset = elf.shstrtab.insert(".text");
  header.type = kProgram;
  header.flags = kSectionAlloc | kSectionExecutable;
  header.address = kTextStartAddress;
  header.offset = elf.section_offset;
  header.size = text_size;
  header.align = 0x1000;

  elf.section_offset += header.size;
}

void initSymtabSection(Object& elf) {
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kSymtab);
  header.name_offset = elf.shstrtab.insert(".symtab");
  header.type = kSymbolTable;
  header.flags = kSectionInfoLink;
  header.offset = elf.section_offset;
  header.size = elf.symtab.bytes().size();
  header.link = raw(SectionIdx::kStrtab);
  // This is the index of the first global symbol, i.e. the first symbol after
  // the null symbol.
  header.info = 1;
  header.entry_size = sizeof(Symbol);

  elf.section_offset += header.size;
}

void initStrtabSection(Object& elf) {
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kStrtab);
  header.name_offset = elf.shstrtab.insert(".strtab");
  header.type = kStringTable;
  header.flags = kSectionStrings;
  header.offset = elf.section_offset;
  header.size = elf.strtab.bytes().size();

  elf.section_offset += header.size;
}

void initShstrtabSection(Object& elf) {
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kShstrtab);
  header.name_offset = elf.shstrtab.insert(".shstrtab");
  header.type = kStringTable;
  header.flags = kSectionStrings;
  header.offset = elf.section_offset;
  header.size = elf.shstrtab.bytes().size();

  elf.section_offset += header.size;
}

void initTextSegment(Object& elf) {
  SectionHeader& section = elf.getSectionHeader(SectionIdx::kText);

  // The .text section immediately follows all the ELF headers.
  SegmentHeader& header = elf.getSegmentHeader(SegmentIdx::kText);
  header.type = kLoadableSegment;
  header.flags = kSegmentExecutable | kSegmentReadable;
  header.offset = section.offset;
  header.address = section.address;
  header.file_size = section.size;
  header.mem_size = header.file_size;
  header.align = 0x1000;
}

template <class T>
void write(std::ostream& os, T* data, size_t size) {
  os.write(reinterpret_cast<const char*>(data), size);
  JIT_CHECK(!os.bad(), "Failed to write {} bytes of ELF output", size);
}

void write(std::ostream& os, std::span<const std::byte> bytes) {
  write(os, bytes.data(), bytes.size());
}

} // namespace

void writeEntries(std::ostream& os, const std::vector<CodeEntry>& entries) {
  Object elf;
  initFileHeader(elf);

  // Sections begin after all the headers are written out.
  elf.section_offset = offsetof(Object, header_stop);

  // Null section needs no extra initialization.
  initTextSection(elf, entries);
  initSymtabSection(elf);
  initStrtabSection(elf);
  initShstrtabSection(elf);

  initTextSegment(elf);

  // Write out all the headers.
  write(os, &elf.file_header, sizeof(elf.file_header));
  write(os, &elf.section_headers, sizeof(elf.section_headers));
  write(os, &elf.segment_headers, sizeof(elf.segment_headers));

  // Write out the actual sections themselves.
  for (const CodeEntry& entry : entries) {
    write(os, entry.code.data(), entry.code.size());
  }
  write(os, elf.symtab.bytes());
  write(os, elf.strtab.bytes());
  write(os, elf.shstrtab.bytes());
}

} // namespace jit::elf
