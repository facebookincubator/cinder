// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/log.h"
#include "Jit/ref.h"

typedef struct _typeobject PyTypeObject;

namespace jit {

// A runtime type profiler that remembers frequencies for the first 'rows'
// lists of 'cols' types it sees, grouping any further lists of types into an
// "other" bucket. Types are compared using pointer equality, so no subtype
// relationships are considered. Types may be nullptr; what this means is up to
// the client code.
//
// TypeProfiler holds strong references to any types it remembers; take care to
// ensure that the lifetime of any TypeProfiler objects don't last past
// Py_Finalize().
//
// If you know the number of rows at compile time and cols == 1, consider using
// the simpler (and more compact/efficient) FixedTypeProfiler.
class alignas(Ref<PyTypeObject>) TypeProfiler {
 public:
  static std::unique_ptr<TypeProfiler> create(int rows, int cols);

  // TypeProfilers are dynamically-sized, so make sure we bypass any default
  // operator delete that tries to take a size hint from sizeof(TypeProfiler).
  static void operator delete(void* ptr) {
    ::operator delete(ptr);
  }

  ~TypeProfiler();

  template <typename... Args>
  void recordTypes(Args&&... tys);
  void clear();

  bool empty() const;

  // Return true if and only if this TypeProfiler has recorded more than one
  // type.
  bool isPolymorphic() const;

  int rows() const;
  int cols() const;

  // Get the count for a row, or the PyTypeObject* at a (row, col) position. No
  // rows with count > 0 will follow a row with count == 0.
  int count(int row) const;
  PyTypeObject* type(int row, int col) const;
  int other() const;

 private:
  explicit TypeProfiler(int rows, int cols);

  Ref<PyTypeObject>* typesPtr();
  const Ref<PyTypeObject>* typesPtr() const;
  int* countsPtr();
  const int* countsPtr() const;

  uint8_t const rows_;
  uint8_t const cols_;
  int other_{0};

  // types and counts are dynamically allocated and appear after the end of the
  // object.
};

template <typename... Args>
inline void TypeProfiler::recordTypes(Args&&... args) {
  std::array<PyTypeObject*, sizeof...(Args)> tys{args...};
  JIT_CHECK(
      tys.size() == cols_, "Expected %d arguments, got %d", cols_, tys.size());

  Ref<PyTypeObject>* type_row = typesPtr();
  int* counts = countsPtr();

  auto types_match = [&] {
    for (size_t col = 0; col < cols_; ++col) {
      if (type_row[col] != tys[col]) {
        return false;
      }
    }
    return true;
  };

  for (size_t row = 0; row < rows_; ++row, type_row += cols_) {
    if (counts[row] == 0) {
      for (size_t col = 0; col < cols_; ++col) {
        type_row[col].reset(tys[col]);
      }
    } else if (!types_match()) {
      continue;
    }

    counts[row]++;
    return;
  }

  other_++;
}

inline int TypeProfiler::rows() const {
  return rows_;
}

inline int TypeProfiler::cols() const {
  return cols_;
}

inline PyTypeObject* TypeProfiler::type(int row, int col) const {
  JIT_DCHECK(
      row < rows_ && col < cols_,
      "Invalid position (%d, %d): bounds (%d, %d)",
      row,
      col,
      rows_,
      cols_);
  return typesPtr()[row * cols_ + col];
}

inline int TypeProfiler::count(int row) const {
  JIT_DCHECK(row < rows_, "Invalid row %d: limit %d", row, rows_);
  return countsPtr()[row];
}

inline int TypeProfiler::other() const {
  return other_;
}

inline Ref<PyTypeObject>* TypeProfiler::typesPtr() {
  return reinterpret_cast<Ref<PyTypeObject>*>(this + 1);
}

inline const Ref<PyTypeObject>* TypeProfiler::typesPtr() const {
  return const_cast<TypeProfiler*>(this)->typesPtr();
}

inline int* TypeProfiler::countsPtr() {
  return reinterpret_cast<int*>(typesPtr() + rows_ * cols_);
}

inline const int* TypeProfiler::countsPtr() const {
  return const_cast<TypeProfiler*>(this)->countsPtr();
}

} // namespace jit
