// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/util.h"

#include <dis-asm.h>

#include <string>

namespace jit {

struct disassemble_extra_info : disassemble_info {
  int addr_len;
};

struct Disassembler {
  Disassembler(const char* buf, long size);
  Disassembler(const char* buf, long size, vma_t vma);
  ~Disassembler();

  void setPrintAddr(bool print) {
    print_addr_ = print;
  }
  void setPrintInstBytes(bool print) {
    print_instr_bytes_ = print;
  }

  std::string codeAddress();
  std::string disassembleOne(int* instr_length = nullptr);
  std::string disassembleAll();

 private:
  const char* const buf_;
  vma_t const vma_;
  jit_string_t* const sfile_;
  bool const auto_size_;
  long const size_;
  disassemble_extra_info info_;
  long start_;
  int const addr_len_;
  bool print_addr_ = true;
  bool print_instr_bytes_ = true;
};

// this function outputs disassembled code pointed by buf to stdout.
// size specifies the length of the code to be disassembled in byte. when size
// is -1, this function finds out the length automatically by looking for the
// return instruction (RET). Therefore, in order to have the code disassembled
// correctly, only one return instruction can exist in the code when size is -1.
// vma indicates the starting virtual memory address of the function to be
// disassembled.
void disassemble(const char* buf, long size, vma_t vma);

} // namespace jit
