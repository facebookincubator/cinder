// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "sys/mman.h"

#include "Jit/log.h"
#include "Jit/util.h"

#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace jit {

template <typename T>
class SlabIterator {
 public:
  SlabIterator() = default;
  SlabIterator(char* ptr, size_t increment)
      : ptr_{ptr}, increment_{increment} {}

  SlabIterator& operator++() {
    ptr_ += increment_;
    return *this;
  }

  SlabIterator operator++(int) {
    auto copy = *this;
    operator++();
    return copy;
  }

  T& operator*() {
    return *reinterpret_cast<T*>(ptr_);
  }

  const T& operator*() const {
    return *reinterpret_cast<const T*>(ptr_);
  }

  bool operator==(const SlabIterator& o) const {
    return ptr_ == o.ptr_;
  }

  bool operator!=(const SlabIterator& o) const {
    return !operator==(o);
  }

 private:
  char* ptr_{nullptr};
  size_t increment_{0};
};

// A slab of memory.  The total size of the slab is defined statically, but the
// size of each individual object within the slab is controlled by the
// `increment_` field.
template <typename T, size_t kSlabSize>
class Slab {
 public:
  using iterator = SlabIterator<T>;

  explicit Slab(size_t increment) : increment_{increment} {
    JIT_CHECK(
        increment >= sizeof(T),
        "Trying to fit a slab object into too little memory");
    void* ptr;
    int result = posix_memalign(&ptr, kPageSize, kSlabSize);
    JIT_CHECK(result == 0, "Failed to allocate %d bytes", kSlabSize);
    base_.reset(static_cast<char*>(ptr));
    fill_ = base_.get();
  }

  Slab(Slab&& other)
      : base_{std::move(other.base_)},
        fill_{other.fill_},
        increment_{other.increment_} {
    other.fill_ = nullptr;
  }

  ~Slab() {
    for (T& obj : *this) {
      obj.~T();
    }
  }

  // Allocate memory for a new T object. Returns void* because the object is not
  // constructed yet.
  void* allocate() {
    char* new_fill = fill_ + increment_;
    if (new_fill > base_.get() + kSlabSize) {
      return nullptr;
    }

    char* ptr = fill_;
    fill_ = new_fill;
    return ptr;
  }

  void mlock() const {
    if (::mlock(base_.get(), kSlabSize) < 0) {
      JIT_LOG("Failed to mlock slab at %p", base_.get());
    }
  }

  void munlock() const {
    if (::munlock(base_.get(), kSlabSize) < 0) {
      JIT_LOG("Failed to munlock slab at %p", base_.get());
    }
  }

  iterator begin() const {
    return iterator{base_.get(), increment_};
  }

  iterator end() const {
    return iterator{fill_, increment_};
  }

 private:
  unique_c_ptr<char> base_;
  char* fill_{nullptr};
  size_t increment_{0};
};

} // namespace jit
