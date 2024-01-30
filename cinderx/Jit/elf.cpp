// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "cinderx/Jit/elf.h"

#include "cinderx/Jit/log.h"
#include "cinderx/Jit/util.h"

#include <cstring>
#include <ostream>

namespace jit {

namespace {

// See https://en.wikipedia.org/wiki/Executable_and_Linkable_Format#File_layout
// for how the ELF 64-bit file layout is structured.

// Program header types.
constexpr uint32_t kLoadableSegment = 0x1;

// Program header flags.
constexpr uint32_t kSegmentExecutable = 0x1;
constexpr uint32_t kSegmentReadable = 0x4;

// Section header types.
constexpr uint32_t kProgram = 0x01;
constexpr uint32_t kSymbolTable = 0x02;
constexpr uint32_t kStringTable = 0x03;

// Section header flags.
constexpr uint64_t kSectionAlloc = 0x02;
constexpr uint64_t kSectionExecutable = 0x04;
constexpr uint64_t kSectionStrings = 0x20;
constexpr uint64_t kSectionInfoLink = 0x40;

// Symbol flags.
constexpr uint8_t kGlobal = 0x10;
constexpr uint8_t kFunc = 0x02;

// Section header indices / ordering.
constexpr uint32_t kNullSectionHeaderIndex = 0;
constexpr uint32_t kTextSectionHeaderIndex = 1;
constexpr uint32_t kSymbolSectionHeaderIndex = 2;
constexpr uint32_t kSymbolStringSectionHeaderIndex = 3;
constexpr uint32_t kSectionStringSectionHeaderIndex = 4;
constexpr uint32_t kNumSections = 5;

// Header that describes a memory segment loaded from an ELF file.
struct ElfProgramHeader {
  // Type of this program segment.
  uint32_t type{0};

  // Executable, writable, and readable bits.
  uint32_t flags{0};

  // Offset of the memory segment in the file.
  uint64_t offset{0};

  // Virtual address of the memory segment.
  uint64_t address{0};

  // Unused, only applies to systems where raw physical memory access is
  // relevant.
  const uint64_t physical_address{0};

  // Size of the memory segment.
  //
  // `file_size` is the number of bytes inside of the ELF file, but `mem_size`
  // is how big the segment should be after it is loaded.  If `mem_size` is
  // bigger than `file_size`, then the remaining bytes will be padded with zeros
  // when it is loaded.  This is how .bss gets implemented.
  uint64_t file_size{0};
  uint64_t mem_size{0};

  // Required alignment of the memory segment.  An alignment of 0 or 1 means the
  // segment is unaligned, otherwise the value must be a power of two.
  uint64_t align{0};
};

// Header that describes an ELF section.
struct ElfSectionHeader {
  // Offset into .shstrtab section for the name of this section.
  uint32_t name_offset{0};

  // Type of this section (e.g. program data, symbol table, etc.).
  uint32_t type{0};

  // Flags for the section (e.g. writable, executable, contains strings, etc.).
  uint64_t flags{0};

  // Virtual address of the section in memory, if it is loaded into memory.
  uint64_t address{0};

  // Offset of the section in the file.
  uint64_t offset{0};

  // Size of the section in the file.
  uint64_t size{0};

  // Use depends on the section type.  Generally only needed for the symbol
  // table for Cinder's purposes.
  uint32_t link{0};
  uint32_t info{0};

  // Required alignment of the section.  An alignment of 0 means the section is
  // unaligned, otherwise the value must be a power of two.
  uint64_t align{0};

  // If this is a special section type that contains fixed-size entries
  // (e.g. symbol table), then this will be the entry size.  Zero for all other
  // sections.
  uint64_t entry_size{0};
};

// Header of an ELF file.  Comes with some default values for convenience.
struct ElfFileHeader {
  const uint8_t magic[4]{0x7f, 'E', 'L', 'F'};

  // 64-bit.
  uint8_t elf_class{2};

  // Little endian.
  uint8_t endian{1};

  // ELF version is always 1.
  const uint8_t elf_version{1};

  // Linux.
  uint8_t osabi{3};

  // Unused on Linux.
  uint8_t abi_version{0};
  uint8_t padding[7] = {0};

  // Dynamic library.
  uint16_t type{3};

  // AMD x86-64.
  uint16_t machine{0x3e};

  // Duplicate of the previous version field.
  const uint32_t version{1};

  // For executable files, this is where the program starts.
  const uint64_t entry_address{0};

  // Will point to the start of the program header and section header tables.
  uint64_t program_header_offset{0};
  uint64_t section_header_offset{0};

  // Unused for x86, very likely unused for x86-64 as well.
  const uint32_t flags{0};

  // The size of this struct itself.
  const uint16_t header_size{64};

  // Size and number of program headers.
  const uint16_t program_header_size{sizeof(ElfProgramHeader)};
  uint16_t program_header_count{0};

  // Size and number of section headers.
  const uint16_t section_header_size{sizeof(ElfSectionHeader)};
  uint16_t section_header_count{0};

  // Section header table index that contains the section names table.
  uint16_t section_name_index{0};
};

// The layout of all of the ELF headers, combined.
struct ElfHeader {
  ElfFileHeader file_header;
  ElfProgramHeader prog_header;
  std::array<ElfSectionHeader, kNumSections> section_headers;
};

// String table encoded for ELF.
class ElfStringTable {
 public:
  ElfStringTable() {
    // All string tables begin with a NUL character.
    bytes_.push_back(0);
  }

  // Insert a string into the symbol table, return its offset.
  uint32_t insert(std::string_view s) {
    size_t start_off = bytes_.size();
    // Strings are always encoded with a NUL terminator.
    bytes_.resize(bytes_.size() + s.size() + 1);
    std::memcpy(&bytes_[start_off], s.data(), s.size());
    JIT_CHECK(
        fitsInt32(start_off), "ELF symbol table only deals in 32-bit offsets");
    return static_cast<uint32_t>(start_off);
  }

  const uint8_t* start() const {
    return bytes_.data();
  }

  size_t size() const {
    return bytes_.size();
  }

 private:
  std::vector<uint8_t> bytes_;
};

struct ElfSymbol {
  uint32_t name_offset{0};

  // Type of symbol this is.
  uint8_t info{kGlobal | kFunc};

  // Controls symbol visibility.  Zero means to compute visibility from the
  // `info` field.
  const uint8_t other{0};

  // Index of the section that this symbol points to.
  uint16_t section_index{0};
  uint64_t address{0};
  uint64_t size{0};
};

class ElfSymbolTable {
 public:
  ElfSymbolTable() {
    // Symbol table must always start with an undefined symbol.
    ElfSymbol null_sym;
    std::memset(&null_sym, 0, sizeof(null_sym));
    insert(null_sym);
  }

  template <class T>
  void insert(T&& sym) {
    syms_.emplace_back(std::forward<T&&>(sym));
  }

  const uint8_t* start() const {
    return reinterpret_cast<const uint8_t*>(syms_.data());
  }

  size_t size() const {
    return syms_.size() * sizeof(syms_[0]);
  }

 private:
  std::vector<ElfSymbol> syms_;
};

// ELF structures are all expected to be a set size.
static_assert(sizeof(ElfProgramHeader) == 56);
static_assert(sizeof(ElfSectionHeader) == 64);
static_assert(sizeof(ElfFileHeader) == ElfFileHeader{}.header_size);

template <class T>
void write(std::ostream& os, T* data, size_t size) {
  os.write(reinterpret_cast<const char*>(data), size);
  JIT_CHECK(!os.bad(), "Failed to write {} bytes of ELF output", size);
}

} // namespace

void writeElfEntries(
    std::ostream& os,
    const std::vector<ElfCodeEntry>& entries) {
  ElfStringTable section_names;

  ElfSymbolTable symbols;
  ElfStringTable symbol_names;

  // TODO(T176630720): This should not be a hardcoded value.
  uint64_t text_start_address = 0x1000000;
  uint64_t text_end_address = text_start_address;

  for (const ElfCodeEntry& entry : entries) {
    ElfSymbol sym;
    sym.name_offset = symbol_names.insert(entry.func_name);
    sym.section_index = kTextSectionHeaderIndex;
    sym.address = text_end_address;
    sym.size = entry.code.size();
    symbols.insert(std::move(sym));

    // TODO(T176630885): Not writing the filename or lineno yet.

    text_end_address += entry.code.size();
  }

  size_t text_size = text_end_address - text_start_address;

  ElfHeader elf;

  ElfFileHeader& file_header = elf.file_header;
  file_header.program_header_offset = offsetof(ElfHeader, prog_header);
  file_header.program_header_count = 1;
  file_header.section_header_offset = offsetof(ElfHeader, section_headers);
  file_header.section_header_count = kNumSections;
  file_header.section_name_index = kSectionStringSectionHeaderIndex;

  // The memory segment loaded into memory immediately follows all the ELF
  // headers.
  ElfProgramHeader& prog_header = elf.prog_header;
  prog_header.type = kLoadableSegment;
  prog_header.flags = kSegmentExecutable | kSegmentReadable;
  prog_header.offset = sizeof(ElfHeader);
  prog_header.address = text_start_address;
  prog_header.file_size = text_size;
  prog_header.mem_size = text_size;
  prog_header.align = 0x1000;

  // Sections begin after all the headers are written out.
  uint32_t section_offset = sizeof(ElfHeader);

  // Null section needs no extra initialization.

  // Program bits. Occupies memory and is executable.  Text immediately follows
  // the section header table.
  ElfSectionHeader& text_header = elf.section_headers[kTextSectionHeaderIndex];
  text_header.name_offset = section_names.insert(".text");
  text_header.type = kProgram;
  text_header.flags = kSectionAlloc | kSectionExecutable;
  text_header.address = prog_header.address;
  text_header.offset = section_offset;
  text_header.size = text_size;
  text_header.align = 0x1000;
  section_offset += text_header.size;

  ElfSectionHeader& symbol_header =
      elf.section_headers[kSymbolSectionHeaderIndex];
  symbol_header.name_offset = section_names.insert(".symtab");
  symbol_header.type = kSymbolTable;
  symbol_header.flags = kSectionInfoLink;
  symbol_header.offset = section_offset;
  symbol_header.size = symbols.size();
  symbol_header.link = kSymbolStringSectionHeaderIndex;
  // This is the index of the first global symbol, i.e. the first symbol after
  // the null symbol.
  symbol_header.info = 1;
  symbol_header.entry_size = sizeof(ElfSymbol);
  section_offset += symbol_header.size;

  ElfSectionHeader& symbol_string_header =
      elf.section_headers[kSymbolStringSectionHeaderIndex];
  symbol_string_header.name_offset = section_names.insert(".strtab");
  symbol_string_header.type = kStringTable;
  symbol_string_header.flags = kSectionStrings;
  symbol_string_header.offset = section_offset;
  symbol_string_header.size = symbol_names.size();
  section_offset += symbol_string_header.size;

  ElfSectionHeader& section_string_header =
      elf.section_headers[kSectionStringSectionHeaderIndex];
  section_string_header.name_offset = section_names.insert(".shstrtab");
  section_string_header.type = kStringTable;
  section_string_header.flags = kSectionStrings;
  section_string_header.offset = section_offset;
  section_string_header.size = section_names.size();
  section_offset += section_string_header.size;

  // Write out all the headers.
  write(os, &elf, sizeof(elf));

  // Write out the actual sections themselves.
  for (const ElfCodeEntry& entry : entries) {
    write(os, entry.code.data(), entry.code.size());
  }
  write(os, symbols.start(), symbols.size());
  write(os, symbol_names.start(), symbol_names.size());
  write(os, section_names.start(), section_names.size());
}

} // namespace jit
