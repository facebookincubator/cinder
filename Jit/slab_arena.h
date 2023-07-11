// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/log.h"
#include "Jit/slab.h"
#include "Jit/util.h"

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace jit {

template <class T>
struct ObjectSizeTrait {
  static constexpr size_t size() {
    return roundUp(sizeof(T), alignof(T));
  }
};

template <typename T, size_t kSlabSize>
class SlabArenaIterator {
 public:
  SlabArenaIterator() = default;

  SlabArenaIterator(std::vector<Slab<T, kSlabSize>>* slabs) : slabs_{slabs} {
    if (slabs_ == nullptr) {
      return;
    }
    JIT_CHECK(slabs_->size() > 0, "Unexpected empty slabs list");
    slab_ = slabs_->begin();
    slab_iter_ = currentSlab().begin();
    if (isSlabEnd()) {
      *this = SlabArenaIterator{};
    }
  }

  bool operator==(const SlabArenaIterator& other) const = default;
  bool operator!=(const SlabArenaIterator& other) const = default;

  T& operator*() {
    return *slab_iter_;
  }

  const T& operator*() const {
    return *slab_iter_;
  }

  SlabArenaIterator& operator++() {
    slab_iter_++;
    if (isSlabEnd()) {
      slab_++;
      if (isArenaEnd()) {
        return *this = SlabArenaIterator{};
      }
      slab_iter_ = currentSlab().begin();
      JIT_CHECK(slab_iter_ != currentSlab().end(), "Unexpected empty slab");
    }
    return *this;
  }

  SlabArenaIterator operator++(int) {
    auto ret = *this;
    operator++();
    return ret;
  }

 private:
  bool isArenaEnd() const {
    return slabs_ == nullptr || slab_ == slabs_->end();
  }

  bool isSlabEnd() const {
    return isArenaEnd() || slab_iter_ == slab_->end();
  }

  Slab<T, kSlabSize>& currentSlab() const {
    return *slab_;
  }

  // Store a slab list, iterator to a slab within that list, and an iterator to
  // a position within that slab. Past-the-end and uninitialized iterators will
  // contain all value-initialized members.
  std::vector<Slab<T, kSlabSize>>* slabs_{nullptr};
  typename std::vector<Slab<T, kSlabSize>>::iterator slab_{};
  SlabIterator<T> slab_iter_;
};

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
template <
    typename T,
    typename SizeTrait = ObjectSizeTrait<T>,
    size_t kPagesPerSlab = 4>
class SlabArena {
  static constexpr size_t kSlabSize = kPageSize * kPagesPerSlab;
  static_assert(
      sizeof(T) <= kSlabSize,
      "Cannot allocate objects larger than one slab");

 public:
  using iterator = SlabArenaIterator<T, kSlabSize>;

  SlabArena() {
    slabs_.emplace_back(SizeTrait::size());
  }

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
      mem = slabs_.emplace_back(SizeTrait::size()).allocate();
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
    for (auto& slab : slabs_) {
      slab.mlock();
    }
    mlocked_ = true;
  }

  // Unpin the contents from physical memory.
  void munlock() {
    std::lock_guard<std::mutex> guard{mutex_};

    JIT_CHECK(mlocked_, "must be locked to unlock");
    for (auto& slab : slabs_) {
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
  std::vector<Slab<T, kSlabSize>> slabs_;
  std::mutex mutex_;
  bool mlocked_{false};
};

} // namespace jit
