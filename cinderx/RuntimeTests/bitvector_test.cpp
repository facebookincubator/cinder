// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/bitvector.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

#include <ostream>

using namespace jit::util;

TEST(BitVectorTest, ShortVectors) {
  BitVector bv1(34, uint64_t(0x310305070));
  BitVector bv2(34, uint64_t(0x102040608));

  auto bv = bv1 | bv2;

  ASSERT_EQ(bv, BitVector(34, uint64_t(0x312345678)));

  bv = bv1 & bv2;
  ASSERT_EQ(bv, BitVector(34, uint64_t(0x100000000)));

  bv = bv2 - bv;
  ASSERT_EQ(bv, BitVector(34, uint64_t(0x002040608)));
}

TEST(BitVectorTest, LongVectors) {
  BitVector bv1(129);
  BitVector bv2(129);

  bv1.SetBit(67);
  bv2.SetBit(68);

  BitVector bv3(129);
  bv3.SetBit(67);
  bv3.SetBit(68);

  auto bv = bv1 | bv2;

  ASSERT_EQ(bv, bv3);

  bv = bv1 & bv2;
  ASSERT_EQ(bv, BitVector(129));

  bv = bv3 - bv1;
  ASSERT_EQ(bv, bv2);
}

TEST(BitVectorTest, Others) {
  BitVector bv1(127);
  ASSERT_EQ(bv1.GetPopCount(), 0);

  bv1.SetBit(126);
  ASSERT_EQ(bv1.GetPopCount(), 1);

  auto size = bv1.AddBits(2);
  ASSERT_EQ(size, 129);
  ASSERT_EQ(bv1.GetPopCount(), 1);
  ASSERT_EQ(bv1.GetBit(126), true);

  bv1.SetBitWidth(124);
  ASSERT_EQ(bv1.GetPopCount(), 0);
  bv1.SetBitWidth(128);
  ASSERT_EQ(bv1.GetPopCount(), 0);
}

TEST(BitVectorTest, Print) {
  std::ostringstream os;
  BitVector bv(13);
  bv.SetBit(3);
  bv.SetBit(6);
  os << bv;
  EXPECT_EQ(os.str(), "[00010010;00000]");
}

TEST(BitVectorTest, PrintMultipleOf8) {
  std::ostringstream os;
  BitVector bv(16);
  bv.SetBit(10);
  os << bv;
  EXPECT_EQ(os.str(), "[00000000;00100000]");
}

TEST(BitVectorTest, Fill) {
  // Short vector
  BitVector shortvec(7);
  shortvec.fill(true);
  EXPECT_EQ(shortvec.GetBitChunk(0), 0x7full);
  shortvec.fill(false);
  EXPECT_EQ(shortvec.GetBitChunk(0), 0);

  // Long vector
  BitVector longvec(78);
  longvec.fill(true);
  EXPECT_EQ(longvec.GetBitChunk(0), -1);
  EXPECT_EQ(longvec.GetBitChunk(1), 0x3fff);
  longvec.fill(false);
  EXPECT_EQ(longvec.GetBitChunk(0), 0);
  EXPECT_EQ(longvec.GetBitChunk(1), 0);

  longvec.SetBitWidth(128);
  longvec.fill(true);
  EXPECT_EQ(longvec.GetBitChunk(0), -1);
  EXPECT_EQ(longvec.GetBitChunk(1), -1);
}

TEST(BitVectorTest, SetBitChunk) {
  BitVector s(7);
  EXPECT_EQ(s.GetBitChunk(0), 0);
  s.SetBitChunk(0, 0x70);
  EXPECT_EQ(s.GetBitChunk(0), 0x70);
  EXPECT_DEATH(s.SetBitChunk(0, 0x80), "invalid bit chunk");

  BitVector l(130);
  EXPECT_EQ(l.GetBitChunk(2), 0);
  l.SetBitChunk(2, 0x3);
  EXPECT_EQ(l.GetBitChunk(2), 0x3);
  EXPECT_DEATH(l.SetBitChunk(2, 0x4), "invalid bit chunk");
}

TEST(BitVectorTest, IsEmpty) {
  BitVector shortvec(8);
  EXPECT_TRUE(shortvec.IsEmpty());
  shortvec.SetBit(2, true);
  EXPECT_FALSE(shortvec.IsEmpty());
  shortvec.SetBit(2, false);
  EXPECT_TRUE(shortvec.IsEmpty());

  BitVector longvec(123);
  EXPECT_TRUE(longvec.IsEmpty());
  longvec.SetBit(80, true);
  EXPECT_FALSE(longvec.IsEmpty());
  longvec.SetBit(80, false);
  EXPECT_TRUE(longvec.IsEmpty());
}

TEST(BitVectorTest, forEachSetBitShort) {
  BitVector shortvec(8);
  shortvec.SetBit(2, true);
  shortvec.SetBit(7, true);

  int saw_2_n = 0;
  int saw_7_n = 0;
  shortvec.forEachSetBit([&](size_t bit) {
    if (bit == 2) {
      saw_2_n++;
    }
    if (bit == 7) {
      saw_7_n++;
    }
  });
  EXPECT_EQ(saw_2_n, 1);
  EXPECT_EQ(saw_7_n, 1);
}

TEST(BitVectorTest, forEachSetBitLong) {
  BitVector shortvec(123);
  shortvec.SetBit(1, true);
  shortvec.SetBit(3, true);
  shortvec.SetBit(65, true);
  shortvec.SetBit(122, true);

  int saw_1_n = 0;
  int saw_3_n = 0;
  int saw_65_n = 0;
  int saw_122_n = 0;
  shortvec.forEachSetBit([&](size_t bit) {
    if (bit == 1) {
      saw_1_n++;
    }
    if (bit == 3) {
      saw_3_n++;
    }
    if (bit == 65) {
      saw_65_n++;
    }
    if (bit == 122) {
      saw_122_n++;
    }
  });
  EXPECT_EQ(saw_1_n, 1);
  EXPECT_EQ(saw_3_n, 1);
  EXPECT_EQ(saw_65_n, 1);
  EXPECT_EQ(saw_122_n, 1);
}
