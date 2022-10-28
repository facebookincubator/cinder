// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/slab_arena.h"

#include "RuntimeTests/fixtures.h"

#include <cstring>

namespace {

using namespace jit;

// Simple struct that only fits 3 to a page.
struct BigArray {
  std::array<char, kPageSize / 4 + 1> data;
};

void checkData(BigArray* arr, char c) {
  for (size_t i = 0; i < arr->data.size(); i++) {
    ASSERT_EQ(arr->data[i], c) << "i == " << i;
  }
}

} // namespace

TEST(SlabArenaTest, Allocate) {
  // Allocate at least two pages worth of structs and make sure they don't
  // overlap.
  SlabArena<BigArray, 1> arena;

  BigArray* a = arena.allocate();
  a->data.fill(0xa);
  BigArray* b = arena.allocate();
  b->data.fill(0xb);
  BigArray* c = arena.allocate();
  c->data.fill(0xc);
  BigArray* d = arena.allocate();
  d->data.fill(0xd);

  EXPECT_NE(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);
  EXPECT_NE(b, c);
  EXPECT_NE(b, d);
  EXPECT_NE(c, d);

  EXPECT_NO_FATAL_FAILURE(checkData(a, 0xa));
  EXPECT_NO_FATAL_FAILURE(checkData(b, 0xb));
  EXPECT_NO_FATAL_FAILURE(checkData(c, 0xc));
  EXPECT_NO_FATAL_FAILURE(checkData(d, 0xd));
}

namespace {

class Counter {
 public:
  Counter(int& c) : c_{c} {
    c_++;
  }
  ~Counter() {
    c_--;
  }

 private:
  int& c_;
};

} // namespace

TEST(SlabArenaTest, RunsDestructors) {
  int count = 0;
  {
    SlabArena<Counter, 1> arena;

    // Create at least two slabs full of structs
    const int kNumElems = kPageSize / sizeof(Counter) * 2;
    for (int i = 0; i < kNumElems; i++) {
      arena.allocate(count);
      ASSERT_EQ(count, i + 1);
    }
  }

  ASSERT_EQ(count, 0);
}

TEST(SlabArenaTest, Iterate) {
  SlabArena<int, 1> arena;

  for (UNUSED int value : arena) {
    FAIL() << "Arena should be empty";
  }

  // Create at least two slabs full of ints full of arbitrary data.
  const int kFactor = 3;
  const int kNumElems = kPageSize / sizeof(int) * 2;
  for (int i = 0; i < kNumElems; i++) {
    arena.allocate(i * kFactor);
  }

  int count = 0;
  for (int value : arena) {
    ASSERT_EQ(value, count * kFactor);
    count++;
  }
  ASSERT_EQ(count, kNumElems);
}

namespace {

const int kAlignment = 16;
struct alignas(kAlignment) AlignedStruct {
  int64_t a;
  int64_t b;
  int64_t c;
};

} // namespace

TEST(SlabArenaTest, AllocateWithCorrectAlignment) {
  SlabArena<AlignedStruct> arena;

  auto a = reinterpret_cast<intptr_t>(arena.allocate());
  auto b = reinterpret_cast<intptr_t>(arena.allocate());
  EXPECT_EQ(a, roundUp(a, kAlignment));
  EXPECT_EQ(b, roundUp(b, kAlignment));
}
