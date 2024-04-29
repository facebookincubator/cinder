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

constexpr uint64_t kPageSize = 0x1000;

constexpr uint64_t kTextStartAddress = kPageSize;

constexpr uint64_t alignUp(uint64_t n, uint64_t align) {
  if (align == 0) {
    return n;
  }
  uint64_t raised = n + align - 1;
  return raised - (raised % align);
}

constexpr bool isAligned(uint64_t n, uint64_t align) {
  return n == alignUp(n, align);
}

uint64_t alignOffset(Object& elf, uint64_t align) {
  uint64_t new_offset = alignUp(elf.section_offset, align);
  uint64_t delta = new_offset - elf.section_offset;
  elf.section_offset = new_offset;
  return delta;
}

void checkAlignedSection(const SectionHeader& header, std::string_view name) {
  JIT_CHECK(
      isAligned(header.offset, header.align),
      "{} section has offset {:#x} which doesn't match alignment of {:#x}",
      name,
      header.offset,
      header.align);
}

void checkAlignedSegment(const SegmentHeader& header) {
  JIT_CHECK(
      isAligned(header.address - header.offset, header.align),
      "Segment with address {:#x} and offset {:#x} doesn't match alignment of "
      "{:#x}",
      header.address,
      header.offset,
      header.align);
}

void initFileHeader(Object& elf) {
  FileHeader& header = elf.file_header;
  header.segment_header_offset = offsetof(Object, segment_headers);
  header.segment_header_count = raw(SegmentIdx::kTotal);
  header.section_header_offset = offsetof(Object, section_headers);
  header.section_header_count = raw(SectionIdx::kTotal);
  header.section_name_index = raw(SectionIdx::kShstrtab);
}

void initTextSection(Object& elf, uint64_t text_size) {
  // Program bits. Occupies memory and is executable.  Text follows the section
  // header table after some padding.

  SectionHeader& header = elf.getSectionHeader(SectionIdx::kText);
  header.name_offset = elf.shstrtab.insert(".text");
  header.type = kProgram;
  header.flags = kSectionAlloc | kSectionExecutable;
  header.address = elf.section_offset;
  header.offset = elf.section_offset;
  header.size = text_size;
  header.align = 0x10;

  checkAlignedSection(header, ".text");

  elf.section_offset += header.size;
}

void initDynsymSection(Object& elf) {
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kDynsym);
  header.name_offset = elf.shstrtab.insert(".dynsym");
  header.type = kSymbolTable;
  header.flags = kSectionAlloc | kSectionInfoLink;
  header.address = elf.section_offset;
  header.offset = elf.section_offset;
  header.size = elf.dynsym.bytes().size();
  header.link = raw(SectionIdx::kDynstr);
  // This is the index of the first global symbol, i.e. the first symbol after
  // the null symbol.
  header.info = 1;
  header.align = 0x8;
  header.entry_size = sizeof(Symbol);

  checkAlignedSection(header, ".dynsym");

  elf.section_offset += header.size;
}

void initDynstrSection(Object& elf) {
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kDynstr);
  header.name_offset = elf.shstrtab.insert(".dynstr");
  header.type = kStringTable;
  header.flags = kSectionAlloc;
  header.address = elf.section_offset;
  header.offset = elf.section_offset;
  header.size = elf.dynstr.bytes().size();
  header.align = 0x1;

  checkAlignedSection(header, ".dynstr");

  elf.section_offset += header.size;
}

void initDynamicSection(Object& elf) {
  JIT_CHECK(
      isAligned(elf.section_offset, kPageSize),
      "Dynamic section starts at unaligned address {:#x}",
      elf.section_offset);

  SectionHeader& header = elf.getSectionHeader(SectionIdx::kDynamic);
  header.name_offset = elf.shstrtab.insert(".dynamic");
  header.type = kDynamic;
  header.flags = kSectionAlloc | kSectionWritable;
  header.address = elf.section_offset;
  header.offset = elf.section_offset;
  header.size = elf.dynamic.bytes().size();
  header.link = raw(SectionIdx::kDynstr);
  header.entry_size = sizeof(Dyn);

  elf.section_offset += header.size;
}

void initShstrtabSection(Object& elf) {
  SectionHeader& header = elf.getSectionHeader(SectionIdx::kShstrtab);
  header.name_offset = elf.shstrtab.insert(".shstrtab");
  header.type = kStringTable;
  header.offset = elf.section_offset;
  header.size = elf.shstrtab.bytes().size();
  header.align = 0x1;

  checkAlignedSection(header, ".shstrtab");

  elf.section_offset += header.size;
}

void initTextSegment(Object& elf) {
  SectionHeader& section = elf.getSectionHeader(SectionIdx::kText);

  // The .text section immediately follows all the ELF headers.
  SegmentHeader& header = elf.getSegmentHeader(SegmentIdx::kText);
  header.type = kSegmentLoadable;
  header.flags = kSegmentExecutable | kSegmentReadable;
  header.offset = section.offset;
  header.address = section.address;
  header.file_size = section.size;
  header.mem_size = header.file_size;
  header.align = 0x1000;

  checkAlignedSegment(header);
}

void initReadonlySegment(Object& elf) {
  SectionHeader& dynsym = elf.getSectionHeader(SectionIdx::kDynsym);
  SectionHeader& dynstr = elf.getSectionHeader(SectionIdx::kDynstr);
  JIT_CHECK(
      dynsym.address < dynstr.address,
      "Expecting sections to be in a specific order");

  SegmentHeader& header = elf.getSegmentHeader(SegmentIdx::kReadonly);
  header.type = kSegmentLoadable;
  header.flags = kSegmentReadable;
  header.offset = dynsym.offset;
  header.address = dynsym.address;
  header.file_size = dynsym.size + dynstr.size;
  header.mem_size = header.file_size;
  header.align = 0x1000;

  checkAlignedSegment(header);
}

void initReadwriteSegment(Object& elf) {
  SectionHeader& dynamic = elf.getSectionHeader(SectionIdx::kDynamic);

  SegmentHeader& header = elf.getSegmentHeader(SegmentIdx::kReadwrite);
  header.type = kSegmentLoadable;
  header.flags = kSegmentReadable | kSegmentWritable;
  header.offset = dynamic.offset;
  header.address = dynamic.address;
  header.file_size = dynamic.size;
  header.mem_size = header.file_size;
  header.align = 0x1000;

  checkAlignedSegment(header);
}

void initDynamicSegment(Object& elf) {
  SectionHeader& dynamic = elf.getSectionHeader(SectionIdx::kDynamic);

  SegmentHeader& header = elf.getSegmentHeader(SegmentIdx::kDynamic);
  header.type = kSegmentDynamic;
  header.flags = kSegmentReadable | kSegmentWritable;
  header.offset = dynamic.offset;
  header.address = dynamic.address;
  header.file_size = dynamic.size;
  header.mem_size = header.file_size;
  header.align = 0x1000;
}

void initDynamics(Object& elf) {
  // Has to be run after .dynsym and .dynstr are mapped out.
  SectionHeader& dynsym = elf.getSectionHeader(SectionIdx::kDynsym);
  SectionHeader& dynstr = elf.getSectionHeader(SectionIdx::kDynstr);

  // TODO(T183002717): kNeeded for _cinderx.so and kHash for .hash.
  elf.dynamic.insert(DynTag::kNeeded, elf.libpython_name);

  elf.dynamic.insert(DynTag::kStrtab, dynstr.address);
  elf.dynamic.insert(DynTag::kStrSz, dynstr.size);
  elf.dynamic.insert(DynTag::kSymtab, dynsym.address);
  elf.dynamic.insert(DynTag::kSymEnt, sizeof(Symbol));
}

template <class T>
void write(std::ostream& os, T* data, size_t size) {
  os.write(reinterpret_cast<const char*>(data), size);
  JIT_CHECK(!os.bad(), "Failed to write {} bytes of ELF output", size);
}

void write(std::ostream& os, std::span<const std::byte> bytes) {
  write(os, bytes.data(), bytes.size());
}

void pad(std::ostream& os, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    os.put(0);
  }
}

} // namespace

void writeEntries(std::ostream& os, const std::vector<CodeEntry>& entries) {
  Object elf;
  initFileHeader(elf);

  // Initialize symbols before any of the sections.
  uint64_t text_end_address = kTextStartAddress;
  for (const CodeEntry& entry : entries) {
    Symbol sym;
    sym.name_offset = elf.dynstr.insert(entry.func_name);
    sym.info = kGlobal | kFunc;
    sym.section_index = raw(SectionIdx::kText);
    sym.address = text_end_address;
    sym.size = entry.code.size();
    elf.dynsym.insert(std::move(sym));

    // TODO(T176630885): Not writing the filename or lineno yet.

    text_end_address += entry.code.size();
  }
  uint64_t text_size = text_end_address - kTextStartAddress;

  elf.libpython_name = elf.dynstr.insert("libpython3.10.so");

  // The headers are all limited to the zeroth page, sections begin on the next
  // page.
  elf.section_offset = offsetof(Object, header_stop);
  uint64_t header_padding = alignOffset(elf, kPageSize);
  JIT_CHECK(
      elf.section_offset == kTextStartAddress,
      "ELF headers were too big and went past the zeroth page: {:#x}",
      elf.section_offset);

  // Null section needs no extra initialization.

  initTextSection(elf, text_size);
  uint64_t text_padding = alignOffset(elf, kPageSize);

  initDynsymSection(elf);
  initDynstrSection(elf);
  uint64_t dynsym_padding = alignOffset(elf, kPageSize);

  initDynamics(elf);

  initDynamicSection(elf);
  initShstrtabSection(elf);

  initTextSegment(elf);
  initReadonlySegment(elf);
  initReadwriteSegment(elf);
  initDynamicSegment(elf);

  // Write out all the headers.
  write(os, &elf.file_header, sizeof(elf.file_header));
  write(os, &elf.section_headers, sizeof(elf.section_headers));
  write(os, &elf.segment_headers, sizeof(elf.segment_headers));
  pad(os, header_padding);

  // Write out the actual sections themselves.
  for (const CodeEntry& entry : entries) {
    write(os, entry.code.data(), entry.code.size());
  }
  pad(os, text_padding);

  write(os, elf.dynsym.bytes());
  write(os, elf.dynstr.bytes());
  pad(os, dynsym_padding);

  write(os, elf.dynamic.bytes());
  write(os, elf.shstrtab.bytes());
}

} // namespace jit::elf
