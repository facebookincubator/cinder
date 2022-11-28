// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "sys/mman.h"

#include "Jit/log.h"
#include "Jit/util.h"

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace jit {

// SlabArena is a simple arena allocator, using slabs that are multiples of the
// system's page size. Allocated objects never move after creation, and all
// objects will be kept alive until the SlabArena they came from is destroyed.
//
// It is intended to keep objects of a given type together on the same page,
// either to achieve desired certain copy-on-write behavior, or to mlock() all
// of the objects with minimal collateral damage (which can be managed with
// SlabArena::mlock() and SlabArena::munlock()).
//
// allocate(), mlock(), and munlock() are thread-safe. begin(), end(), and all
// operations on SlabArena::iterator are not thread-safe.
template <typename T, size_t pages_per_slab = 4>
class SlabArena {
  static constexpr size_t kSlabSize = kPageSize * pages_per_slab;
  static_assert(
      sizeof(T) <= kSlabSize,
      "Cannot allocate objects larger than one slab");

  class Slab;

 public:
  class iterator;

  // Allocate a new instance of T using the given constructor arguments.
  template <typename... Args>
  T* allocate(Args&&... args) {
    std::lock_guard<std::mutex> guard{mutex_};

    if (mlocked_) {
      // It's not necessarily an error to allocate after locking but it's
      // probably not what we expect to happen in the common forking case.
      JIT_DLOG("Allocating while locked");
    }

    void* mem = slabs_.back().allocate();
    if (mem == nullptr) {
      mem = slabs_.emplace_back().allocate();
      JIT_CHECK(mem != nullptr, "Empty slab failed to allocate");
      if (mlocked_) {
        slabs_.back().mlock();
      }
    }
    return new (mem) T(std::forward<Args>(args)...);
  }

  // Pin the contents to physical memory.
  void mlock() {
    std::lock_guard<std::mutex> guard{mutex_};

    JIT_CHECK(!mlocked_, "must be unlocked to lock");
    for (Slab& slab : slabs_) {
      slab.mlock();
    }
    mlocked_ = true;
  }

  // Unpin the contents from physical memory.
  void munlock() {
    std::lock_guard<std::mutex> guard{mutex_};

    JIT_CHECK(mlocked_, "must be locked to unlock");
    for (Slab& slab : slabs_) {
      slab.munlock();
    }
    mlocked_ = false;
  }

  iterator begin() {
    return iterator{&slabs_};
  }

  iterator end() {
    return iterator{};
  }

 private:
  std::vector<Slab> slabs_{1};
  std::mutex mutex_;
  bool mlocked_{false};
};

template <typename T, size_t pages_per_slab>
class SlabArena<T, pages_per_slab>::Slab {
 public:
  Slab() {
    void* ptr;
    int result = posix_memalign(&ptr, kPageSize, kSlabSize);
    JIT_CHECK(result == 0, "Failed to allocate %d bytes", kSlabSize);
    base_.reset(static_cast<char*>(ptr));
    fill_ = base_.get();
  }

  Slab(Slab&& other) : base_{std::move(other.base_)}, fill_{other.fill_} {
    other.fill_ = nullptr;
  }

  ~Slab() {
    for (T& obj : *this) {
      obj.~T();
    }
  }

  void* allocate() {
    char* end = fill_ + roundUp(sizeof(T), alignof(T));
    if (end > base_.get() + kSlabSize) {
      return nullptr;
    }

    char* ptr = fill_;
    fill_ = end;
    return ptr;
  }

  void mlock() {
    if (::mlock(base_.get(), kSlabSize) < 0) {
      JIT_LOG("Failed to mlock slab at %p", base_.get());
    }
  }

  void munlock() {
    if (::munlock(base_.get(), kSlabSize) < 0) {
      JIT_LOG("Failed to munlock slab at %p", base_.get());
    }
  }

  T* begin() {
    return reinterpret_cast<T*>(base_.get());
  }

  T* end() {
    return reinterpret_cast<T*>(fill_);
  }

 private:
  unique_c_ptr<char> base_;
  char* fill_{nullptr};
};

template <typename T, size_t pages_per_slab>
class SlabArena<T, pages_per_slab>::iterator {
 public:
  iterator() = default;

  iterator(std::vector<Slab>* slabs) : slabs_{slabs} {
    if (slabs_ != nullptr) {
      slab_ = slabs_->begin();
      JIT_CHECK(slab_ != slabs_->end(), "Unexpected empty Slab list");
      slab_iter_ = slab_->begin();
      if (slab_iter_ == slab_->end()) {
        *this = iterator{};
      }
    }
  }

  bool operator==(const iterator& other) const {
    return slabs_ == other.slabs_ && slab_ == other.slab_ &&
        slab_iter_ == other.slab_iter_;
  }

  bool operator!=(const iterator& other) const {
    return !operator==(other);
  }

  T& operator*() {
    return *slab_iter_;
  }

  iterator& operator++() {
    slab_iter_++;
    if (slab_iter_ == slab_->end()) {
      slab_++;
      if (slab_ == slabs_->end()) {
        return *this = iterator{};
      }
      slab_iter_ = slab_->begin();
      JIT_CHECK(slab_iter_ != slab_->end(), "Unexpected empty Slab");
    }
    return *this;
  }

  iterator operator++(int) {
    iterator ret{*this};
    operator++();
    return ret;
  }

 private:
  // Store a slab list, iterator to a slab within that list, and an iterator to
  // a position within that slab. Past-the-end and uninitialized iterators will
  // contain all value-initialized members.
  std::vector<Slab>* slabs_{nullptr};
  typename std::vector<Slab>::iterator slab_{};
  T* slab_iter_{};
};

} // namespace jit
