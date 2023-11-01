// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/bitvector.h"

#include <algorithm>
#include <cstdint>

namespace jit::util {

BitVector::~BitVector() {
  if (!IsShortVector()) {
    delete bits_.bit_vec;
  }
}

BitVector::BitVector(size_t nb) : num_bits_(nb) {
  if (IsShortVector()) {
    bits_.bits = 0;
  } else {
    size_t size = num_bits_ / PTR_WIDTH + (num_bits_ % PTR_WIDTH == 0 ? 0 : 1);
    bits_.bit_vec = new std::vector<uint64_t>(size, 0);
  }
}

BitVector& BitVector::operator=(const BitVector& bv) {
  if (this == &bv) {
    return *this;
  }

  bool lhs_short = IsShortVector();
  bool rhs_short = bv.IsShortVector();

  num_bits_ = bv.num_bits_;

  if (lhs_short && rhs_short) {
    bits_.bits = bv.bits_.bits;
  } else if (lhs_short && !rhs_short) {
    bits_.bit_vec = new std::vector<uint64_t>(*bv.bits_.bit_vec);
  } else if (!lhs_short && rhs_short) {
    delete bits_.bit_vec;
    bits_.bits = bv.bits_.bits;
  } else { // if (!lhs_short && !rhs_short)
    *bits_.bit_vec = *bv.bits_.bit_vec;
  }

  return *this;
}

BitVector& BitVector::operator=(BitVector&& bv) {
  if (this == &bv) {
    return *this;
  }

  if (!IsShortVector()) {
    delete bits_.bit_vec;
  }

  num_bits_ = bv.num_bits_;
  bits_ = bv.bits_;
  bv.num_bits_ = 0;

  return *this;
}

bool BitVector::operator==(const BitVector& rhs) const {
  JIT_CHECK(num_bits_ == rhs.num_bits_, "LHS and RHS are of different widths.");

  if (IsShortVector()) {
    return bits_.bits == rhs.bits_.bits;
  }

  return std::equal(
      bits_.bit_vec->begin(), bits_.bit_vec->end(), rhs.bits_.bit_vec->begin());
}

template <typename Op>
BitVector BitVector::BinaryOp(const BitVector& rhs, const Op& op) const {
  JIT_CHECK(num_bits_ == rhs.num_bits_, "LHS and RHS are of different widths.");

  if (IsShortVector()) {
    return BitVector(num_bits_, op(bits_.bits, rhs.bits_.bits));
  }

  BitVector bv;
  bv.num_bits_ = num_bits_;
  bv.bits_.bit_vec = new std::vector<uint64_t>;
  bv.bits_.bit_vec->reserve(bits_.bit_vec->size());
  std::transform(
      bits_.bit_vec->begin(),
      bits_.bit_vec->end(),
      rhs.bits_.bit_vec->begin(),
      std::back_inserter(*bv.bits_.bit_vec),
      [op](uint64_t a, uint64_t b) -> uint64_t { return op(a, b); });
  return bv;
}

BitVector BitVector::operator&(const BitVector& rhs) const {
  return BinaryOp(
      rhs, [](uint64_t a, uint64_t b) -> uint64_t { return a & b; });
}

BitVector BitVector::operator|(const BitVector& rhs) const {
  return BinaryOp(
      rhs, [](uint64_t a, uint64_t b) -> uint64_t { return a | b; });
}

BitVector BitVector::operator-(const BitVector& rhs) const {
  return BinaryOp(
      rhs, [](uint64_t a, uint64_t b) -> uint64_t { return a & ~b; });
}

template <typename Op>
BitVector& BitVector::BinaryOpAssign(const BitVector& rhs, const Op& op) {
  JIT_CHECK(num_bits_ == rhs.num_bits_, "LHS and RHS are of different widths.");

  if (IsShortVector()) {
    bits_.bits = op(bits_.bits, rhs.bits_.bits);
  } else {
    std::transform(
        bits_.bit_vec->begin(),
        bits_.bit_vec->end(),
        rhs.bits_.bit_vec->begin(),
        bits_.bit_vec->begin(),
        [op](uint64_t a, uint64_t b) -> uint64_t { return op(a, b); });
  }

  return *this;
}

BitVector& BitVector::operator&=(const BitVector& rhs) {
  return BinaryOpAssign(
      rhs, [](uint64_t a, uint64_t b) -> uint64_t { return a & b; });
}

BitVector& BitVector::operator|=(const BitVector& rhs) {
  return BinaryOpAssign(
      rhs, [](uint64_t a, uint64_t b) -> uint64_t { return a | b; });
}

BitVector& BitVector::operator-=(const BitVector& rhs) {
  return BinaryOpAssign(
      rhs, [](uint64_t a, uint64_t b) -> uint64_t { return a & ~b; });
}

void BitVector::ResetAll() {
  if (IsShortVector()) {
    bits_.bits = 0;
  } else {
    for (auto& v : *(bits_.bit_vec)) {
      v = 0;
    }
  }
}

void BitVector::fill(bool v) {
  if (!v) {
    return ResetAll();
  }

  if (IsShortVector()) {
    if (num_bits_ == PTR_WIDTH) {
      bits_.bits = -1;
    } else {
      bits_.bits = (uintptr_t{1} << num_bits_) - 1;
    }
  } else {
    auto& vec = *bits_.bit_vec;
    for (size_t i = 0; i < vec.size() - 1; ++i) {
      vec[i] = -1;
    }

    auto remainder = num_bits_ % PTR_WIDTH;
    if (remainder == 0) {
      vec.back() = -1;
    } else {
      vec.back() = (uintptr_t{1} << remainder) - 1;
    }
  }
}

void BitVector::SetBit(size_t bit, bool v) {
  JIT_CHECK(bit < num_bits_, "bit is too large.");
  if (IsShortVector()) {
    auto b = uintptr_t(1) << bit;
    bits_.bits = v ? (bits_.bits | b) : (bits_.bits & ~b);
  } else {
    size_t index = bit / PTR_WIDTH;
    size_t offset = bit % PTR_WIDTH;
    auto& val = bits_.bit_vec->at(index);
    uintptr_t b = uintptr_t(1) << offset;
    val = v ? (val | b) : (val & ~b);
  }
}

size_t BitVector::AddBits(size_t i) {
  auto new_num_bits = num_bits_ + i;
  SetBitWidth(new_num_bits);
  return new_num_bits;
}

void BitVector::SetBitWidth(size_t size) {
  if (num_bits_ == size) {
    return;
  }

  bool old_short = IsShortVector();
  auto new_num_bits = size;
  num_bits_ = new_num_bits;
  bool new_short = IsShortVector();

  if (old_short && !new_short) {
    size_t size = num_bits_ / PTR_WIDTH + (num_bits_ % PTR_WIDTH == 0 ? 0 : 1);

    auto old_bits = bits_.bits;
    bits_.bit_vec = new std::vector<uint64_t>(size);
    bits_.bit_vec->at(0) = old_bits;
  } else if (!old_short && !new_short) {
    size_t size = num_bits_ / PTR_WIDTH + (num_bits_ % PTR_WIDTH == 0 ? 0 : 1);
    bits_.bit_vec->resize(size);
  } else if (!old_short && new_short) {
    auto low_bits = bits_.bit_vec->at(0);
    delete bits_.bit_vec;
    bits_.bits = low_bits;
  }

  // need to clear the unused upper bits
  // could use BZHI instruction, but this function is not frequently called,
  // so it is okay.
  auto high_mask = (uint64_t(1) << (num_bits_ % PTR_WIDTH)) - 1;
  if (new_short) {
    bits_.bits &= high_mask;
  } else {
    auto& chunk = *bits_.bit_vec->rbegin();
    chunk &= high_mask;
  }
}

bool BitVector::GetBit(size_t bit) const {
  JIT_CHECK(bit < num_bits_, "bit is out of range.");
  if (IsShortVector()) {
    auto b = uintptr_t(1) << bit;
    return bits_.bits & b;
  }

  size_t index = bit / PTR_WIDTH;
  size_t offset = bit % PTR_WIDTH;

  return bits_.bit_vec->at(index) & (uintptr_t(1) << offset);
}

void BitVector::forEachSetBit(std::function<void(size_t)> per_bit_func) const {
  auto forEachBitInChunk = [&](uint64_t chunk, size_t base) {
    while (chunk) {
      int bit = __builtin_ctzl(chunk);
      chunk ^= chunk & -chunk;
      per_bit_func(bit + base);
    }
  };

  if (IsShortVector()) {
    forEachBitInChunk(bits_.bits, 0);
  } else {
    size_t chunk_base = 0;
    for (uint64_t chunk : *bits_.bit_vec) {
      forEachBitInChunk(chunk, chunk_base);
      chunk_base += PTR_WIDTH;
    }
  }
}

uint64_t BitVector::GetBitChunk(size_t chunk) const {
  if (IsShortVector()) {
    JIT_CHECK(chunk == 0, "chunk is out of range.");
    return bits_.bits;
  }

  JIT_CHECK(chunk < bits_.bit_vec->size(), "chunk is out of range.");
  return bits_.bit_vec->at(chunk);
}

void BitVector::SetBitChunk(size_t chunk, uint64_t bits) {
  auto num_chunks = (num_bits_ + PTR_WIDTH - 1) / PTR_WIDTH;
  JIT_CHECK(chunk < num_chunks, "chunk is out of range");

  if (chunk == num_chunks - 1) {
    auto remainder = num_bits_ % PTR_WIDTH;
    if (remainder != 0) {
      auto mask = ~((uint64_t{1} << remainder) - 1);
      JIT_CHECK((mask & bits) == 0, "invalid bit chunk");
    }
  }

  if (IsShortVector()) {
    bits_.bits = bits;
    return;
  }

  (*bits_.bit_vec)[chunk] = bits;
}

size_t BitVector::GetPopCount() const {
  if (IsShortVector()) {
    return __builtin_popcountll(bits_.bits);
  }

  size_t count = 0;
  for (auto& b : *bits_.bit_vec) {
    count += __builtin_popcountll(b);
  }
  return count;
}

bool BitVector::IsEmpty() const {
  if (IsShortVector()) {
    return bits_.bits == 0;
  }

  for (auto& b : *bits_.bit_vec) {
    if (b != 0) {
      return false;
    }
  }
  return true;
}

std::ostream& operator<<(std::ostream& os, const BitVector& bv) {
  os << '[';
  for (std::size_t i = 0, n = bv.GetNumBits(); i < n; ++i) {
    if (i > 0 && (i % 8) == 0) {
      os << ';';
    }
    os << (bv.GetBit(i) ? '1' : '0');
  }
  os << ']';
  return os;
}

} // namespace jit::util
