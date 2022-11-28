// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "asmjit/asmjit.h"

#include "Jit/codegen/code_section.h"
#include "Jit/log.h"

#include <memory>
#include <vector>

namespace jit {

/*
  A CodeAllocator allocates memory for live JIT code. This is an abstract
  interface for now to allow us to easily switch between implementations based
  on an AsmJIT "Runtime", or an implemenation which uses huge pages.

  For now we only support one global per-process CodeAllocator, accessible via
  ::get(). This is primarily to maximize the efficiency when using huge pages
  by avoiding independent huge-page pools which are all a little under-utilized.

  We may one day need non-global code allocators if we want to do fancy things
  like accomodate memory pools with different allocation characteristics, or
  have multiple threads which might compile independently.
*/
class CodeAllocator {
 public:
  virtual ~CodeAllocator();

  // Get the global code allocator for this process.
  static CodeAllocator* get() {
    JIT_CHECK(s_global_code_allocator_ != nullptr, "No global code allocator");
    return s_global_code_allocator_;
  }

  // To be called once by JIT initialization after enough configuration has been
  // loaded to determine which global code allocator type to use.
  static void makeGlobalCodeAllocator();

  static void freeGlobalCodeAllocator();

  const asmjit::CodeInfo& asmJitCodeInfo() {
    return _runtime->codeInfo();
  }

  virtual asmjit::Error addCode(
      void** dst,
      asmjit::CodeHolder* code) noexcept = 0;

 protected:
  std::unique_ptr<asmjit::JitRuntime> _runtime{
      std::make_unique<asmjit::JitRuntime>()};

 private:
  static CodeAllocator* s_global_code_allocator_;
};

class CodeAllocatorAsmJit : public CodeAllocator {
 public:
  virtual ~CodeAllocatorAsmJit() {}

  asmjit::Error addCode(void** dst, asmjit::CodeHolder* code) noexcept
      override {
    return _runtime->add(dst, code);
  }
};

// A code allocator which tries to allocate all code on huge pages.
class CodeAllocatorCinder : public CodeAllocator {
 public:
  virtual ~CodeAllocatorCinder();

  asmjit::Error addCode(void** dst, asmjit::CodeHolder* code) noexcept override;

  static size_t usedBytes() {
    return s_used_bytes_;
  }

  static size_t lostBytes() {
    return s_lost_bytes_;
  }

  static size_t fragmentedAllocs() {
    return s_fragmented_allocs_;
  }

  static size_t hugeAllocs() {
    return s_huge_allocs_;
  }

 private:
  // List of chunks allocated for use in deallocation
  static std::vector<void*> s_allocations_;

  // Pointer to next free address in the current chunk
  static uint8_t* s_current_alloc_;
  // Free space in the current chunk
  static size_t s_current_alloc_free_;

  static size_t s_used_bytes_;
  // Number of bytes in total lost when allocations didn't fit neatly into
  // the bytes remaining in a chunk so a new one was allocated.
  static size_t s_lost_bytes_;
  // Number of chunks allocated (= to number of huge pages used)
  static size_t s_huge_allocs_;
  // Number of chunks allocated which did not use huge pages.
  static size_t s_fragmented_allocs_;
};

class MultipleSectionCodeAllocator : public CodeAllocator {
 public:
  MultipleSectionCodeAllocator()
      : total_allocation_size_{0}, code_alloc_{nullptr} {}
  virtual ~MultipleSectionCodeAllocator();

  asmjit::Error addCode(void** dst, asmjit::CodeHolder* code) noexcept override;

 private:
  void createSlabs() noexcept;

  std::unordered_map<jit::codegen::CodeSection, uint8_t*> code_sections_;
  std::unordered_map<jit::codegen::CodeSection, size_t>
      code_section_free_sizes_;

  size_t total_allocation_size_;
  uint8_t* code_alloc_;
};

void populateCodeSections(
    std::vector<std::pair<void*, std::size_t>>& output_vector,
    asmjit::CodeHolder& code,
    void* entry);

}; // namespace jit
