// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/code_allocator.h"

#include "Jit/pyjit.h"
#include "Jit/threaded_compile.h"

#include <sys/mman.h>

#include <cstring>

using namespace jit::codegen;

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
  if (_PyJIT_MultipleCodeSectionsEnabled()) {
    s_global_code_allocator_ = new MultipleSectionCodeAllocator;
  } else if (_PyJIT_UseHugePages()) {
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
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    JIT_CHECK(
        res != MAP_FAILED,
        "Failed to allocate %d bytes of memory for code",
        alloc_size);

    if (madvise(res, alloc_size, MADV_HUGEPAGE) == -1) {
      JIT_LOG(
          "Failed to madvise [%p, %p) with MADV_HUGEPAGE",
          res,
          static_cast<char*>(res) + alloc_size);
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

MultipleSectionCodeAllocator::~MultipleSectionCodeAllocator() {
  if (code_alloc_ == nullptr) {
    return;
  }
  int result = munmap(code_alloc_, total_allocation_size_);
  JIT_CHECK(result == 0, "Freeing sections failed");
}

/*
 * At startup, we allocate a contiguous chunk of memory for all code sections
 * equal to the sum of individual section sizes and subdivide internally. The
 * code is contiguously allocated internally, but logically has pointers into
 * each CodeSection.
 */
void MultipleSectionCodeAllocator::createSlabs() noexcept {
  // Linux's huge-page sizes are 2 MiB.
  const size_t kHugePageSize = 1024 * 1024 * 2;
  size_t hot_section_size =
      asmjit::Support::alignUp(_PyJIT_HotCodeSectionSize(), kHugePageSize);
  JIT_CHECK(
      hot_section_size > 0,
      "Hot code section must have non-zero size when using multiple sections.");
  code_section_free_sizes_[CodeSection::kHot] = hot_section_size;

  size_t cold_section_size = _PyJIT_ColdCodeSectionSize();
  JIT_CHECK(
      cold_section_size > 0,
      "Cold code section must have non-zero size when using multiple "
      "sections.");
  code_section_free_sizes_[CodeSection::kCold] = cold_section_size;

  total_allocation_size_ = hot_section_size + cold_section_size;

  auto region = static_cast<uint8_t*>(mmap(
      NULL,
      total_allocation_size_,
      PROT_EXEC | PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0));
  JIT_CHECK(region != MAP_FAILED, "Allocating the code sections failed.");

  if (madvise(region, hot_section_size, MADV_HUGEPAGE) == -1) {
    JIT_LOG("Was unable to use huge pages for the hot code section.");
  }

  code_alloc_ = region;
  code_sections_[CodeSection::kHot] = region;
  region += hot_section_size;
  code_sections_[CodeSection::kCold] = region;
}

asmjit::Error MultipleSectionCodeAllocator::addCode(
    void** dst,
    asmjit::CodeHolder* code) noexcept {
  ThreadedCompileSerialize guard;

  if (code_sections_.empty()) {
    createSlabs();
  }
  *dst = nullptr;

  size_t potential_code_size = code->codeSize();
  // We fall back to the default size of code allocation if the
  // code doesn't fit into either section, and we can make this check more
  // granular by comparing sizes section-by-section.
  if (code_section_free_sizes_[CodeSection::kHot] < potential_code_size ||
      code_section_free_sizes_[CodeSection::kCold] < potential_code_size) {
    JIT_LOG(
        "Not enough memory to split code across sections, falling back to "
        "normal allocation.");
    return _runtime->add(dst, code);
  }
  // Fix up the offsets for each code section before resolving links.
  // Both the `.text` and `.addrtab` sections are written to the hot section,
  // and we need to resolve offsets between them properly.
  // In order to properly keep track of multiple text sections corresponding to
  // the same physical section to allocate to, we keep a map from
  // section->offset from start of hot section.
  std::unordered_map<CodeSection, uint64_t> offsets;
  offsets[CodeSection::kHot] = 0;
  offsets[CodeSection::kCold] =
      code_sections_[CodeSection::kCold] - code_sections_[CodeSection::kHot];

  for (asmjit::Section* section : code->sections()) {
    CodeSection code_section = codeSectionFromName(section->name());
    uint64_t offset = offsets[code_section];
    uint64_t realSize = section->realSize();
    section->setOffset(offset);
    // Since all sections lie on a contiguous slab, we rely on setting the
    // offsets of sections to allow AsmJit to properly resolve links across
    // different sections (offset 0 being the start of the hot code section).
    offsets[code_section] = offset + realSize;
  }

  // Assuming that the offsets are set properly, relocating all code to be
  // relative to the start of the hot code will ensure jumps are correct.
  ASMJIT_PROPAGATE(code->resolveUnresolvedLinks());
  ASMJIT_PROPAGATE(
      code->relocateToBase(uintptr_t(code_sections_[CodeSection::kHot])));

  // We assume that the hot section of the code is non-empty. This would be
  // incorrect for a completely cold function.
  JIT_CHECK(
      code->textSection()->realSize() > 0,
      "Every function must have a non-empty hot section.");
  *dst = code_sections_[CodeSection::kHot];

  for (asmjit::Section* section : code->_sections) {
    size_t buffer_size = section->bufferSize();
    CodeSection code_section = codeSectionFromName(section->name());
    code_section_free_sizes_[code_section] -= buffer_size;
    std::memcpy(code_sections_[code_section], section->data(), buffer_size);
    code_sections_[code_section] += buffer_size;
  }

  return asmjit::kErrorOk;
}

}; // namespace jit
