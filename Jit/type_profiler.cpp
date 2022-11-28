// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/type_profiler.h"

#include <limits>
#include <memory>

namespace jit {

std::unique_ptr<TypeProfiler> TypeProfiler::create(int rows, int cols) {
  const int kMaxDim = std::numeric_limits<decltype(rows_)>::max();
  JIT_CHECK(
      rows >= 1 && rows < kMaxDim,
      "rows (%d) must be in [1, %d)",
      rows,
      kMaxDim);
  JIT_CHECK(
      cols >= 1 && cols < kMaxDim,
      "cols (%d) must be in [1, %d)",
      cols,
      kMaxDim);
  size_t alloc_bytes = sizeof(TypeProfiler) +
      sizeof(Ref<PyTypeObject>) * rows * cols + sizeof(int) * rows;
  void* mem = ::operator new(alloc_bytes);
  return std::unique_ptr<TypeProfiler>(new (mem) TypeProfiler(rows, cols));
}

TypeProfiler::TypeProfiler(int rows, int cols) : rows_(rows), cols_(cols) {
  new (typesPtr()) Ref<PyTypeObject>[ rows_ * cols_ ];
  new (countsPtr()) int[rows_]{};
}

TypeProfiler::~TypeProfiler() {
  Ref<PyTypeObject>* types = typesPtr();
  for (size_t i = 0; i < rows_ * cols_; ++i) {
    types[i].~Ref<PyTypeObject>();
  }
}

bool TypeProfiler::empty() const {
  if (other_ > 0) {
    return false;
  }

  const int* counts = countsPtr();
  for (size_t row = 0; row < rows_; ++row) {
    if (counts[row] > 0) {
      return false;
    }
  }
  return true;
}

bool TypeProfiler::isPolymorphic() const {
  return other() > 0 || (rows() > 1 && count(1) > 0);
}

void TypeProfiler::clear() {
  Ref<PyTypeObject>* types = typesPtr();
  int* counts = countsPtr();
  for (size_t i = 0; i < rows_ * cols_; ++i) {
    types[i].reset();
  }
  for (size_t i = 0; i < rows_; ++i) {
    counts[i] = 0;
  }
  other_ = 0;
}

} // namespace jit
