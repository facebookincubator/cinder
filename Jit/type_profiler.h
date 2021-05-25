// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/ref.h"

#include <array>

typedef struct _typeobject PyTypeObject;

namespace jit {

// A simple runtime type profiler that remembers frequencies for the first N
// types it sees, grouping any further types into an "other" bucket. Types are
// compared using pointer equality, so no subtype relationships are considered.
//
// TypeProfiler holds strong references to any types it remembers; take care to
// ensure that the lifetime of any TypeProfile objects don't last past
// Py_Finalize().
template <size_t N>
struct TypeProfiler {
  static const size_t size = N;

  void recordType(PyTypeObject* ty);

  bool empty() const;

  std::array<Ref<PyTypeObject>, N> types;
  std::array<int, N> counts{};
  int other{0};
};

template <size_t N>
void TypeProfiler<N>::recordType(PyTypeObject* ty) {
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
bool TypeProfiler<N>::empty() const {
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
