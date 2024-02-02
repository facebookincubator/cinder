// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "cinderx/Jit/codegen/code_section.h"
#include "cinderx/Jit/log.h"

#include "cinderx/ThirdParty/asmjit/src/asmjit/asmjit.h"

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

  const asmjit::Environment& asmJitEnvironment() {
    return runtime_->environment();
  }

  virtual asmjit::Error addCode(void** dst, asmjit::CodeHolder* code) noexcept {
    return runtime_->add(dst, code);
  }

 protected:
  std::unique_ptr<asmjit::JitRuntime> runtime_{
      std::make_unique<asmjit::JitRuntime>()};

 private:
  static CodeAllocator* s_global_code_allocator_;
};

// A code allocator which tries to allocate all code on huge pages.
class CodeAllocatorCinder : public CodeAllocator {
 public:
  virtual ~CodeAllocatorCinder();

  asmjit::Error addCode(void** dst, asmjit::CodeHolder* code) noexcept override;

  size_t usedBytes() const {
    return used_bytes_;
  }

  size_t lostBytes() const {
    return lost_bytes_;
  }

  size_t fragmentedAllocs() const {
    return fragmented_allocs_;
  }

  size_t hugeAllocs() const {
    return huge_allocs_;
  }

 private:
  // List of chunks allocated for use in deallocation
  std::vector<void*> allocations_;

  // Pointer to next free address in the current chunk
  uint8_t* current_alloc_{nullptr};
  // Free space in the current chunk
  size_t current_alloc_free_{0};

  size_t used_bytes_{0};
  // Number of bytes in total lost when allocations didn't fit neatly into
  // the bytes remaining in a chunk so a new one was allocated.
  size_t lost_bytes_{0};
  // Number of chunks allocated (= to number of huge pages used)
  size_t huge_allocs_{0};
  // Number of chunks allocated which did not use huge pages.
  size_t fragmented_allocs_{0};
};

class MultipleSectionCodeAllocator : public CodeAllocator {
 public:
  virtual ~MultipleSectionCodeAllocator();

  asmjit::Error addCode(void** dst, asmjit::CodeHolder* code) noexcept override;

 private:
  void createSlabs() noexcept;

  std::unordered_map<codegen::CodeSection, uint8_t*> code_sections_;
  std::unordered_map<codegen::CodeSection, size_t> code_section_free_sizes_;

  uint8_t* code_alloc_{nullptr};
  size_t total_allocation_size_{0};
};

void populateCodeSections(
    std::vector<std::pair<void*, std::size_t>>& output_vector,
    asmjit::CodeHolder& code,
    void* entry);

}; // namespace jit
