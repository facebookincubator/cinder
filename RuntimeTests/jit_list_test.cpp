// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/jit_list.h"

#include "RuntimeTests/fixtures.h"

using JITListTest = RuntimeTest;
using WildcardJITListTest = RuntimeTest;

using jit::JITList;
using jit::WildcardJITList;

TEST_F(JITListTest, ParseLine) {
  auto jitlist = JITList::create();
  ASSERT_NE(jitlist, nullptr);

  // Valid
  EXPECT_TRUE(jitlist->parseLine("foo:bar"));
  EXPECT_TRUE(jitlist->parseLine(""));
  EXPECT_TRUE(jitlist->parseLine("# foo"));
  EXPECT_TRUE(jitlist->parseLine("    foo:bar"));
  EXPECT_TRUE(jitlist->parseLine("foo:bar   "));
  EXPECT_TRUE(jitlist->parseLine("    foo:bar   "));

  // Invalid
  EXPECT_FALSE(jitlist->parseLine("foo"));
}

TEST_F(JITListTest, LookupFO) {
  auto jitlist = JITList::create();
  ASSERT_NE(jitlist, nullptr);

  ASSERT_TRUE(jitlist->parseLine("foo:bar"));
  ASSERT_TRUE(jitlist->parseLine("foo:baz"));

  auto foo = Ref<>::steal(PyUnicode_FromString("foo"));
  ASSERT_NE(foo, nullptr);
  auto bar = Ref<>::steal(PyUnicode_FromString("bar"));
  ASSERT_NE(bar, nullptr);
  auto baz = Ref<>::steal(PyUnicode_FromString("baz"));
  ASSERT_NE(baz, nullptr);
  auto quux = Ref<>::steal(PyUnicode_FromString("quux"));
  ASSERT_NE(quux, nullptr);

  EXPECT_TRUE(jitlist->lookupFO(foo, bar));
  EXPECT_TRUE(jitlist->lookupFO(foo, baz));
  EXPECT_FALSE(jitlist->lookupFO(foo, quux));
  EXPECT_FALSE(jitlist->lookupFO(quux, bar));
}

TEST_F(JITListTest, LookupCO) {
  auto jitlist = JITList::create();
  ASSERT_NE(jitlist, nullptr);

  auto func = compileAndGet("def f(): pass", "f");
  ASSERT_NE(func, nullptr);

  BorrowedRef<PyCodeObject> code(
      reinterpret_cast<PyFunctionObject*>(func.get())->func_code);
  ASSERT_NE(code, nullptr);

  ASSERT_EQ(jitlist->lookupCO(code), 0);
}

TEST_F(WildcardJITListTest, ParseLine) {
  auto jitlist = WildcardJITList::create();
  ASSERT_NE(jitlist, nullptr);
  ASSERT_FALSE(jitlist->parseLine("*:*"));
}

TEST_F(WildcardJITListTest, Lookup) {
  auto jitlist = WildcardJITList::create();
  ASSERT_NE(jitlist, nullptr);

  ASSERT_TRUE(jitlist->parseLine("foo:*"));
  ASSERT_TRUE(jitlist->parseLine("*:baz"));
  ASSERT_TRUE(jitlist->parseLine("bar:quux"));
  ASSERT_TRUE(jitlist->parseLine("*:*.__init__"));
  ASSERT_TRUE(jitlist->parseLine("foo:*.evaluate"));

  auto foo = Ref<>::steal(PyUnicode_FromString("foo"));
  ASSERT_NE(foo, nullptr);
  auto bar = Ref<>::steal(PyUnicode_FromString("bar"));
  ASSERT_NE(bar, nullptr);
  auto baz = Ref<>::steal(PyUnicode_FromString("baz"));
  ASSERT_NE(baz, nullptr);
  auto quux = Ref<>::steal(PyUnicode_FromString("quux"));
  ASSERT_NE(quux, nullptr);
  auto foo_init = Ref<>::steal(PyUnicode_FromString("Foo.__init__"));
  ASSERT_NE(foo_init, nullptr);
  auto foo_evaluate = Ref<>::steal(PyUnicode_FromString("Foo.evaluate"));
  ASSERT_NE(foo_evaluate, nullptr);
  auto foo_bar_evaluate =
      Ref<>::steal(PyUnicode_FromString("Foo.Bar.evaluate"));
  ASSERT_NE(foo_bar_evaluate, nullptr);

  // All funcs in foo are enabled
  EXPECT_TRUE(jitlist->lookupFO(foo, bar));
  EXPECT_TRUE(jitlist->lookupFO(foo, baz));
  EXPECT_TRUE(jitlist->lookupFO(foo, quux));

  // All qualnames of baz are enabled
  EXPECT_TRUE(jitlist->lookupFO(quux, baz));

  // Can't wildcard everything
  EXPECT_FALSE(jitlist->lookupFO(bar, foo));

  // Exact lookups should still work
  EXPECT_TRUE(jitlist->lookupFO(bar, quux));

  // Unconditionally wildcarded instance methods
  EXPECT_TRUE(jitlist->lookupFO(bar, foo_init));
  EXPECT_TRUE(jitlist->lookupFO(quux, foo_init));

  // Per-module wildcarded instance methods
  EXPECT_TRUE(jitlist->lookupFO(foo, foo_evaluate));
  EXPECT_TRUE(jitlist->lookupFO(foo, foo_bar_evaluate));
  EXPECT_FALSE(jitlist->lookupFO(bar, foo_evaluate));
}
