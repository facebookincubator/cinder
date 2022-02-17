#include "Jit/code_allocator.h"

#include "Jit/pyjit.h"
#include "Jit/threaded_compile.h"

#include <sys/mman.h>

#include <cstring>

namespace jit {

// 2MiB to match Linux's huge-page size.
const size_t kAllocSize = 1024 * 1024 * 2;

CodeAllocator* CodeAllocator::s_global_code_allocator_ = nullptr;

std::vector<void*> CodeAllocatorCinder::s_allocations_{};
uint8_t* CodeAllocatorCinder::s_current_alloc_ = nullptr;
size_t CodeAllocatorCinder::s_current_alloc_free_ = 0;

size_t CodeAllocatorCinder::s_used_bytes_ = 0;
size_t CodeAllocatorCinder::s_lost_bytes_ = 0;
size_t CodeAllocatorCinder::s_huge_allocs_ = 0;
size_t CodeAllocatorCinder::s_fragmented_allocs_ = 0;

CodeAllocator::~CodeAllocator() {}

void CodeAllocator::makeGlobalCodeAllocator() {
  JIT_CHECK(
      s_global_code_allocator_ == nullptr, "Global allocator already set");
  if (_PyJIT_UseHugePages()) {
    s_global_code_allocator_ = new CodeAllocatorCinder;
  } else {
    s_global_code_allocator_ = new CodeAllocatorAsmJit;
  }
}

void CodeAllocator::freeGlobalCodeAllocator() {
  JIT_CHECK(s_global_code_allocator_ != nullptr, "Global allocator not set");
  delete s_global_code_allocator_;
  s_global_code_allocator_ = nullptr;
}

CodeAllocatorCinder::~CodeAllocatorCinder() {
  for (void* alloc : s_allocations_) {
    JIT_CHECK(munmap(alloc, kAllocSize) == 0, "Freeing code memory failed");
  }
  s_allocations_.clear();
  s_current_alloc_ = nullptr;
  s_current_alloc_free_ = 0;

  s_used_bytes_ = 0;
  s_lost_bytes_ = 0;
  s_huge_allocs_ = 0;
  s_fragmented_allocs_ = 0;
}

asmjit::Error CodeAllocatorCinder::addCode(
    void** dst,
    asmjit::CodeHolder* code) noexcept {
  ThreadedCompileSerialize guard;

  *dst = nullptr;

  ASMJIT_PROPAGATE(code->flatten());
  ASMJIT_PROPAGATE(code->resolveUnresolvedLinks());

  size_t max_code_size = code->codeSize();
  size_t alloc_size = ((max_code_size / kAllocSize) + 1) * kAllocSize;
  if (s_current_alloc_free_ < max_code_size) {
    s_lost_bytes_ += s_current_alloc_free_;
    void* res = mmap(
        NULL,
        alloc_size,
        PROT_EXEC | PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
        -1,
        0);
    if (res == MAP_FAILED) {
      res = mmap(
          NULL,
          alloc_size,
          PROT_EXEC | PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANONYMOUS,
          -1,
          0);
      JIT_CHECK(res != MAP_FAILED, "Failed to allocate memory for code");
      s_fragmented_allocs_++;
    } else {
      s_huge_allocs_++;
    }
    s_current_alloc_ = static_cast<uint8_t*>(res);
    s_allocations_.emplace_back(res);
    s_current_alloc_free_ = alloc_size;
  }

  ASMJIT_PROPAGATE(code->relocateToBase(uintptr_t(s_current_alloc_)));

  size_t actual_code_size = code->codeSize();
  JIT_CHECK(actual_code_size <= max_code_size, "Code grew during relocation");

  for (asmjit::Section* section : code->_sections) {
    size_t offset = section->offset();
    size_t buffer_size = section->bufferSize();
    size_t virtual_size = section->virtualSize();

    JIT_CHECK(
        offset + buffer_size <= actual_code_size, "Inconsistent code size");
    std::memcpy(s_current_alloc_ + offset, section->data(), buffer_size);

    if (virtual_size > buffer_size) {
      JIT_CHECK(
          offset + virtual_size <= actual_code_size, "Inconsistent code size");
      std::memset(
          s_current_alloc_ + offset + buffer_size,
          0,
          virtual_size - buffer_size);
    }
  }

  *dst = s_current_alloc_;

  s_current_alloc_ += actual_code_size;
  s_current_alloc_free_ -= actual_code_size;
  s_used_bytes_ += actual_code_size;

  return asmjit::kErrorOk;
}

}; // namespace jit
