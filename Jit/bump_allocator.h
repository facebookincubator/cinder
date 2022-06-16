// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/log.h"

#include <sys/mman.h>

#include <cinttypes>
#include <cstddef>

namespace {
template <typename T>
constexpr bool isPowerOfTwo(T x) {
  return (x & (x - 1)) == 0;
}

template <typename T>
constexpr T roundDown(T x, int n) {
  JIT_DCHECK(isPowerOfTwo(n), "must be power of 2");
  return (x & -n);
}

template <typename T>
constexpr T roundUp(T x, int n) {
  return roundDown(x + n - 1, n);
}

const int kKiB = 1024;
const int kMiB = kKiB * kKiB;
const int kGiB = kKiB * kKiB * kKiB;
enum { kPageSize = 4 * kKiB };

} // namespace

template <typename T>
class BumpAllocator {
 public:
  BumpAllocator(size_t max_elements) {
    size_t size = max_elements * element_size_;
    size = roundUp(max_elements * element_size_, kPageSize);
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    raw_ = ::mmap(nullptr, size, prot, flags, -1, 0);
    JIT_CHECK(raw_ != MAP_FAILED, "mmap failure");
    fill_ = reinterpret_cast<uintptr_t>(raw_);
    end_ = fill_ + size;
  }

  ~BumpAllocator() {
    for (auto& obj : *this) {
      // Since the objects are placement-new'd, they must be manually
      // destructed.
      obj.~T();
    }
    int result = ::munmap(raw_, size());
    JIT_CHECK(result != -1, "munmap failure");
  }

  template <typename... Args>
  T* allocate(Args&&... args) {
    if (locked_) {
      // It's not necessarily an error to allocate after locking but it's
      // probably not what we expect to happen in the common forking case.
      // Unfortunately, this locking is hard to test in the unit test suite if
      // we make this an error.
      JIT_DLOG("Allocated after locking!");
    }
    uintptr_t fill = fill_;
    uintptr_t free = end_ - fill;
    if (element_size_ > free) {
      return nullptr;
    }
    fill_ = fill + element_size_;
    T* mem = reinterpret_cast<T*>(fill);
    return new (mem) T(std::forward<Args>(args)...);
  }

  int lock() {
    JIT_CHECK(locked_ == false, "must be unlocked to lock");
    int result = ::mlock(raw_, size());
    if (result < 0) {
      return result;
    }
    locked_ = true;
    return 0;
  }

  int unlock() {
    JIT_CHECK(locked_ == true, "must be locked to unlock");
    int result = ::munlock(raw_, size());
    if (result < 0) {
      return result;
    }
    locked_ = false;
    return 0;
  }

  uintptr_t fill() {
    return fill_;
  }
  uintptr_t size() {
    return end_ - reinterpret_cast<uintptr_t>(raw_);
  }
  T* begin() {
    return reinterpret_cast<T*>(raw_);
  }
  T* end() {
    return reinterpret_cast<T*>(fill_);
  }
  const T* begin() const {
    return reinterpret_cast<T*>(raw_);
  }
  const T* end() const {
    return reinterpret_cast<T*>(fill_);
  }

 private:
  size_t element_size_ = roundUp(sizeof(T), alignof(T));
  bool locked_{false};
  uintptr_t end_{0};
  uintptr_t fill_{0};
  void* raw_{nullptr};
};
