// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/disassembler.h"

#include "Jit/log.h"
#include "Jit/runtime.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace jit {

namespace {

int get_address_hex_length(vma_t vma, size_t size) {
  vma += size;
  int i = sizeof(vma_t) * 8 - __builtin_clzl(vma);
  int j = i % 4;
  return (j > 0 ? i / 4 + 1 : i / 4) + 2;
}

void print_address(vma_t vma, disassemble_info* info) {
  info->fprintf_func(
      info->stream,
      "%#0*lx",
      static_cast<disassemble_extra_info*>(info)->addr_len,
      vma);
}

void print_symbol(vma_t addr, disassemble_info* info) {
  // At some point in the future we may want a more complete solution like
  // https://github.com/facebook/hhvm/blob/0ff8dca4f1174f3ffa9c5d282ae1f5b5523fe56c/hphp/util/abi-cxx.cpp#L64
  std::optional<std::string> symbol =
      symbolize(reinterpret_cast<const void*>(addr));
  if (symbol.has_value()) {
    info->fprintf_func(info->stream, " (%s)", symbol->c_str());
  }
}

} // namespace

Disassembler::Disassembler(const char* buf, long size)
    : Disassembler(buf, size, reinterpret_cast<vma_t>(buf)) {}

Disassembler::Disassembler(const char* buf, long size, vma_t vma)
    : buf_(buf),
      vma_(vma),
      sfile_(ss_alloc()),
      auto_size_(size == -1),
      size_(auto_size_ ? 0xffff : size),
      start_(0),
      addr_len_(get_address_hex_length(vma_, size_)) {
  memset(&info_, 0, sizeof(info_));
  info_.fprintf_func = (int (*)(void*, const char*, ...))ss_sprintf;
  info_.stream = sfile_;
  info_.octets_per_byte = 1;
  info_.read_memory_func = buffer_read_memory;
  info_.memory_error_func = perror_memory;
  info_.print_address_func = print_address;
  info_.print_symbol_func = print_symbol;
  info_.stop_vma = (uintptr_t)vma_ + size;
  info_.buffer = (unsigned char*)buf;
  info_.buffer_length = size;
  info_.buffer_vma = vma_;

  info_.addr_len = addr_len_;
}

Disassembler::~Disassembler() {
  ss_free(sfile_);
}

std::string Disassembler::codeAddress() {
  return fmt::format("{:#0{}x}", vma_ + start_, addr_len_);
}

std::string Disassembler::disassembleOne(int* instr_length) {
  std::string result;
  if (print_addr_) {
    format_to(result, "{}:{:8}", codeAddress(), "");
  }

  int length = print_insn(vma_ + start_, &info_);
  if (print_instr_bytes_) {
    for (long i = start_; i < start_ + 8; i++) {
      if (i < start_ + length) {
        format_to(result, "{:02x} ", static_cast<unsigned char>(buf_[i]));
      } else {
        format_to(result, "   ");
      }
    }
  }

  result += ss_get_string(sfile_);
  ss_reset(sfile_);

  start_ += length;
  if (instr_length) {
    *instr_length = length;
  }
  return result;
}

std::string Disassembler::disassembleAll() {
  std::string result;
  while (start_ < size_ || auto_size_) {
    std::string instr_str = disassembleOne();

    format_to(result, "{}\n", instr_str);
    // yes, this is a little bit dirty, but this function is mainly for
    // debugging purpose so i think it is okay. otherwise, we have to use other
    // libraries such as libelf to read the symbol table from the shared
    // library.
    const char kRetqEncoding = 0xc3;
    if (auto_size_ && buf_[start_] == kRetqEncoding) {
      break;
    }
  }
  return result;
}

void disassemble(const char* buf, long size, vma_t vma) {
  Disassembler dis(buf, size, vma);
  std::cout << dis.disassembleAll();
}

} // namespace jit
