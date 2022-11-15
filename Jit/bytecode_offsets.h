#pragma once

#include "Python.h"

#include "Jit/util.h"

#include <limits>
#include <ostream>

namespace jit {

/*
 * BCOffsetBase is used to define two related types: BCOffset and BCIndex.
 * BCOffset holds a byte offset into a code object, while BCIndex holds an
 * instruction index into a code object.
 *
 * They are both simple wrappers for an integral value (int, in the current
 * implementation, assuming the JIT won't have to deal with code objects
 * containing more than 2 GiB of bytecode), and support common comparison and
 * arithmetic operations. Conversion to or from a raw int must be explicit, but
 * implicit conversion between BCOffset and BCIndex is allowed, with appropriate
 * adjustments made to the value.
 */
template <typename T>
class BCOffsetBase {
 public:
  constexpr BCOffsetBase() = default;

  template <class TInt>
  explicit BCOffsetBase(TInt value) : value_{check(value)} {}

  // Explicit accessor for the underlying value.
  constexpr int value() const {
    return value_;
  }

  // Comparison operators.
  auto operator<=>(T other) const {
    return value() <=> other.value();
  }

  auto operator<=>(Py_ssize_t other) const {
    return value() <=> other;
  }

  bool operator==(T other) const {
    return operator==(other.value());
  }

  bool operator==(Py_ssize_t other) const {
    return value() == other;
  }

  bool operator>(T other) const {
    return operator>(other.value());
  }

  bool operator>(Py_ssize_t other) const {
    return value() > other;
  }

  // Arithmetic operators.
  T operator+(Py_ssize_t other) const {
    return T{value() + other};
  }

  T operator-(Py_ssize_t other) const {
    return T{value() - other};
  }

  int operator-(T other) const {
    return value_ - other.value();
  }

  T operator*(Py_ssize_t other) const {
    return T{value() * other};
  }

  T& operator++() {
    value_++;
    return asT();
  }

  T operator++(int) {
    T old = asT();
    operator++();
    return old;
  }

  T& operator--() {
    value_--;
    return asT();
  }

  T operator--(int) {
    T old = asT();
    operator--();
    return old;
  }

 private:
  template <class TInt>
  int check(TInt v) {
    static_assert(std::is_integral_v<TInt>, "BCOffsetBase is an integer");
    JIT_DCHECK(
        v <= std::numeric_limits<int>::max(), "Overflow converting from %d", v);
    // Checking that an unsigned value is greater or equal than a negative value
    // would produce a compiler warning.
    if constexpr (std::is_signed_v<TInt>) {
      JIT_DCHECK(
          v >= std::numeric_limits<int>::min(),
          "Underflow converting from %d",
          v);
    }
    return static_cast<int>(v);
  }

  T& asT() {
    return static_cast<T&>(*this);
  }

  const T& asT() const {
    return static_cast<const T&>(*this);
  }

  int value_{0};
};

class BCIndex;

class BCOffset : public BCOffsetBase<BCOffset> {
 public:
  using BCOffsetBase::BCOffsetBase;

  BCOffset(BCIndex idx);

  BCIndex asIndex() const;
};

class BCIndex : public BCOffsetBase<BCIndex> {
 public:
  using BCOffsetBase::BCOffsetBase;

  BCIndex(BCOffset offset);

  BCOffset asOffset() const;
};

inline BCOffset::BCOffset(BCIndex idx)
    : BCOffset{idx.value() * int{sizeof(_Py_CODEUNIT)}} {}

inline BCIndex::BCIndex(BCOffset offset)
    : BCIndex{offset.value() / int{sizeof(_Py_CODEUNIT)}} {}

inline BCIndex BCOffset::asIndex() const {
  return BCIndex{*this};
}

inline BCOffset BCIndex::asOffset() const {
  return BCOffset{*this};
}

inline BCOffset operator+(const BCOffset& a, const BCOffset& b) {
  return BCOffset{a.value() + b.value()};
}

// Convenience operators for array access and printing.
inline _Py_CODEUNIT* operator+(_Py_CODEUNIT* code, BCIndex index) {
  return code + index.value();
}

inline std::ostream& operator<<(std::ostream& os, jit::BCOffset offset) {
  return os << offset.value();
}

inline std::ostream& operator<<(std::ostream& os, jit::BCIndex index) {
  return os << index.value();
}

} // namespace jit

template <>
struct std::hash<jit::BCOffset> {
  size_t operator()(const jit ::BCOffset& offset) const {
    return std::hash<int>{}(offset.value());
  }
};

template <>
struct std::hash<jit::BCIndex> {
  size_t operator()(const jit::BCIndex& index) const {
    return std::hash<int>{}(index.value());
  }
};
