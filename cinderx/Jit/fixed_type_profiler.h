// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/ref.h"

#include <array>

typedef struct _typeobject PyTypeObject;

namespace jit {

// A simple runtime type profiler that remembers frequencies for the first N
// types it sees, grouping any further types into an "other" bucket. Types are
// compared using pointer equality, so no subtype relationships are considered.
//
// FixedTypeProfiler holds strong references to any types it remembers; take
// care to ensure that the lifetime of any FixedTypeProfiler objects don't last
// past Py_Finalize().
//
// For a similar class that can profile vectors of types and doesn't require
// the size as a template parameter, see TypeProfiler.
template <size_t N>
struct FixedTypeProfiler {
  static constexpr size_t size = N;

  void recordType(PyTypeObject* ty);

  void clear();

  bool empty() const;

  std::array<Ref<PyTypeObject>, N> types;
  std::array<int, N> counts{};
  int other{0};
};

template <size_t N>
void FixedTypeProfiler<N>::recordType(PyTypeObject* ty) {
  for (size_t i = 0; i < N; ++i) {
    if (types[i] == nullptr) {
      types[i].reset(ty);
    } else if (types[i] != ty) {
      continue;
    }

    counts[i]++;
    return;
  }

  other++;
}

template <size_t N>
void FixedTypeProfiler<N>::clear() {
  other = 0;
  for (size_t i = 0; i < N; ++i) {
    types[i].reset();
    counts[i] = 0;
  }
}

template <size_t N>
bool FixedTypeProfiler<N>::empty() const {
  if (other > 0) {
    return false;
  }
  for (int c : counts) {
    if (c > 0) {
      return false;
    }
  }
  return true;
}

} // namespace jit
