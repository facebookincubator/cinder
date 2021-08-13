// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/util.h"

#include "RuntimeTests/fixtures.h"

using UtilTest = RuntimeTest;

TEST(UtilTestNoFixture, Worklist) {
  jit::Worklist<int> wl;
  ASSERT_TRUE(wl.empty());
  wl.push(5);
  wl.push(12);
  wl.push(5);
  wl.push(14);
  wl.push(14);
  ASSERT_FALSE(wl.empty());
  EXPECT_EQ(wl.front(), 5);
  wl.pop();
  ASSERT_FALSE(wl.empty());
  EXPECT_EQ(wl.front(), 12);
  wl.pop();
  ASSERT_FALSE(wl.empty());
  EXPECT_EQ(wl.front(), 14);
  wl.pop();
  EXPECT_TRUE(wl.empty());
}
