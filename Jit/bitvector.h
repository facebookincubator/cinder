// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/log.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <type_traits>
#include <utility>
#include <vector>

namespace jit::util {

class BitVector {
 public:
  BitVector() : num_bits_(0) {
    bits_.bits = 0;
  }

  ~BitVector();

  template <typename T>
  BitVector(size_t nb, T val) {
    static_assert(
        std::is_integral<T>::value, "val must be of an integral type.");
    JIT_CHECK(nb <= sizeof(void*) * 8, "Bit width is too large.")
    JIT_CHECK(
        nb == 64 || (val & ~((T{1} << nb) - 1)) == 0,
        "Val has too many bits for bit width");
    num_bits_ = nb;
    bits_.bits = val;
  }

  BitVector(size_t size);

  BitVector(const BitVector& bv) : num_bits_(0) {
    *this = bv;
  }
  BitVector(BitVector&& bv) : num_bits_(0) {
    *this = std::move(bv);
  }

  BitVector& operator=(const BitVector& bv);
  BitVector& operator=(BitVector&& bv);

  // Operators for the bit vector. Due to the purpose of this class (used in DFG
  // analysis), we only support operations between two bit vectors with the same
  // width.
  bool operator==(const BitVector& rhs) const;
  bool operator!=(const BitVector& rhs) const {
    return !(*this == rhs);
  }
  BitVector operator&(const BitVector& rhs) const;
  BitVector operator|(const BitVector& rhs) const;
  BitVector operator-(const BitVector& rhs) const;
  BitVector& operator&=(const BitVector& rhs);
  BitVector& operator|=(const BitVector& rhs);
  BitVector& operator-=(const BitVector& rhs);

  // Reset all bits to 0 with num_bits_ unchanged.
  void ResetAll();

  // Set all bits to v.
  void fill(bool v);

  // Get and set a bit in the position specified in bit. The bit index should
  // be in the range of the bit vector, i.e. less than num_bits_.
  bool GetBit(size_t bit) const;

  void forEachSetBit(std::function<void(size_t)> per_bit_func) const;

  void SetBit(size_t bit, bool v = true);
  // Get or set a 64-bit chunk of bits.
  uint64_t GetBitChunk(size_t chunk = 0) const;
  void SetBitChunk(size_t chunk, uint64_t bits);

  // Add number of bits specified in i to the bit vector. Returns the new size.
  size_t AddBits(size_t i);
  // Resize the bit vector to the number of bits specified in size. If size is
  // less than the current number of bits, the bit vector will be truncated.
  void SetBitWidth(size_t size);

  size_t GetNumBits() const {
    return num_bits_;
  }
  size_t GetPopCount() const;
  bool IsEmpty() const;

 private:
  size_t num_bits_;

  /*
   * For a bit vector <= 64 bits (which is the bit width of a pointer), the bits
   * are saved in bits. For a larger bit vector, it is divided into 64-bit
   * chunks and saved in bit_vec.
   */
  union {
    uintptr_t bits;
    std::vector<uint64_t>* bit_vec;
  } bits_;

  static constexpr size_t PTR_WIDTH = sizeof(void*) * 8;

  bool IsShortVector() const {
    return num_bits_ <= PTR_WIDTH;
  }

  template <typename Op>
  BitVector BinaryOp(const BitVector& rhs, const Op& op) const;

  template <typename Op>
  BitVector& BinaryOpAssign(const BitVector& rhs, const Op& op);
};

std::ostream& operator<<(std::ostream& os, const BitVector& bv);

} // namespace jit::util
