// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include <elf.h>
#include <fcntl.h>
#include <link.h> // for ElfW
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace jit {

class Symbolizer {
 public:
  Symbolizer(const char* exe_path = "/proc/self/exe");

  bool isInitialized() const {
    return exe_ != nullptr;
  }

  ~Symbolizer() {
    deinit();
  }

  // Return a string view whose lifetime is tied to the Symbolizer lifetime on
  // success. On failure, return std::nullopt.
  std::optional<std::string_view> symbolize(const void* func);

  std::optional<std::string_view> cache(const void* func, const char* name);

 private:
  void deinit();

  char* exe_{nullptr};
  size_t exe_size_{0};
  ElfW(Shdr) * symtab_{nullptr};
  ElfW(Shdr) * strtab_{nullptr};
  // This cache is useful for performance and also critical for correctness.
  // Some of the symbols (for example, to shared objects) do not return owned
  // pointers. We must keep an object in this map for the string_view to point
  // to.
  std::unordered_map<const void*, std::string> cache_;
};

std::optional<std::string> demangle(const std::string& mangled_name);

} // namespace jit
