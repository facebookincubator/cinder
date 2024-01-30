// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace jit {

// Code entry to add to an ELF file.
struct ElfCodeEntry {
  std::span<uint8_t> code;
  std::string func_name;
  std::string file_name;
  size_t lineno{0};
};

// Write function or code objects out to a new ELF file.
//
// The output ELF file is always a shared library.
void writeElfEntries(
    std::ostream& os,
    const std::vector<ElfCodeEntry>& entries);

} // namespace jit
