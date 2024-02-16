// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Common/util.h"

#include "cinderx/Jit/symbolizer.h"

#include "cinderx/RuntimeTests/fixtures.h"

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

TEST(UtilTest, CombineHash) {
  EXPECT_EQ(jit::combineHash(123, 456), 0x9e379a24);
  EXPECT_EQ(jit::combineHash(123, 456, 789), 0x28cd9c7673);

  // Hash combining is left-associative, not right-associative.
  EXPECT_EQ(
      jit::combineHash(123, 456, 789),
      jit::combineHash(jit::combineHash(123, 456), 789));
  EXPECT_NE(
      jit::combineHash(123, 456, 789),
      jit::combineHash(123, jit::combineHash(456, 789)));
}

TEST(UtilTest, ScopeExitRunsAtScopeEnd) {
  int value = 0;
  {
    EXPECT_EQ(value, 0);
    SCOPE_EXIT(value++);
    EXPECT_EQ(value, 0);
  }
  EXPECT_EQ(value, 1);
}

TEST(UtilTest, SymbolizerWithNonexistentSymbolReturnsNull) {
  jit::Symbolizer symbolizer;
  std::optional<std::string_view> result =
      symbolizer.symbolize(reinterpret_cast<void*>(0xffffffffffffffff));
  EXPECT_FALSE(result.has_value());
}

TEST(UtilTest, SymbolizerResolvesDynamicSymbol) {
  jit::Symbolizer symbolizer;
  std::optional<std::string_view> result =
      symbolizer.symbolize(reinterpret_cast<void*>(std::labs));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "labs");
}

TEST(UtilTest, SymbolizerResolvesStaticSymbol) {
  jit::Symbolizer symbolizer;
  std::optional<std::string_view> result =
      symbolizer.symbolize(reinterpret_cast<void*>(PyObject_Size));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "PyObject_Size");
}

TEST(UtilTest, DemangleWithCNameReturnsName) {
  jit::Symbolizer symbolizer;
  std::optional<std::string> result = jit::demangle("PyObject_Size");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "PyObject_Size");
}

TEST(UtilTest, DemangleWithCXXNameReturnsDemangledName) {
  jit::Symbolizer symbolizer;
  std::optional<std::string> result = jit::demangle("_ZN3jit7Runtime3getEv");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "jit::Runtime::get()");
}

TEST(UtilTest, DemangleWithInvalidCXXNameReturnsInput) {
  jit::Symbolizer symbolizer;
  std::optional<std::string> result = jit::demangle("_ZWTFBBQ");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "_ZWTFBBQ");
}
