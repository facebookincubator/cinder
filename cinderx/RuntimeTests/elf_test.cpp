// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "cinderx/Jit/elf.h"

#include "cinderx/RuntimeTests/fixtures.h"

#include <sstream>

using namespace jit;

using ElfTest = RuntimeTest;

void verifyMagic(std::string_view s) {
  ASSERT_EQ(s[0], 0x7f);
  ASSERT_EQ(s.substr(1, 3), "ELF");
}

TEST_F(ElfTest, EmptyEntries) {
  std::stringstream ss;
  writeElfEntries(ss, {});
  std::string result = ss.str();

  verifyMagic(result);
}

TEST_F(ElfTest, OneEntry) {
  std::stringstream ss;

  ElfCodeEntry entry;
  entry.code = {reinterpret_cast<uint8_t*>(writeElfEntries), 0x40};
  entry.func_name = "funcABC";
  entry.file_name = "spaghetti.exe";
  entry.lineno = 15;

  writeElfEntries(ss, {entry});
  std::string result = ss.str();

  verifyMagic(result);
}
