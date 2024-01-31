// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "Python.h"

#include "cinderx/Jit/live_type_map.h"

#include "cinderx/RuntimeTests/fixtures.h"

using namespace jit;

using LiveTypeMapTest = RuntimeTest;

TEST_F(LiveTypeMapTest, LookupAndEraseTypes) {
  LiveTypeMap map;
  map.insert(&PyLong_Type);
  map.insert(&PyBool_Type);

  ASSERT_EQ(map.size(), 2);
  EXPECT_EQ(map.get("int"), &PyLong_Type);
  EXPECT_EQ(map.get("bool"), &PyBool_Type);
  EXPECT_EQ(map.get("list"), nullptr);

  map.insert(&PyList_Type);

  ASSERT_EQ(map.size(), 3);
  EXPECT_EQ(map.get("list"), &PyList_Type);
  EXPECT_EQ(map.get("dict"), nullptr);

  map.erase(&PyLong_Type);

  ASSERT_EQ(map.size(), 2);
  EXPECT_EQ(map.get("int"), nullptr);
  EXPECT_EQ(map.get("bool"), &PyBool_Type);
  EXPECT_EQ(map.get("list"), &PyList_Type);
}

TEST_F(LiveTypeMapTest, ClearOnlyErasesHeapTypes) {
  const char* py_code = R"(
class C: pass
class D: pass
)";
  ASSERT_TRUE(runCode(py_code));

  Ref<PyTypeObject> c(getGlobal("C"));
  ASSERT_NE(c, nullptr);
  Ref<PyTypeObject> d(getGlobal("D"));
  ASSERT_NE(d, nullptr);

  LiveTypeMap map;

  map.insert(c);
  map.insert(&PyLong_Type);
  map.insert(&PyFloat_Type);

  ASSERT_EQ(map.size(), 3);
  EXPECT_EQ(map.get("jittestmodule:C"), c);
  EXPECT_EQ(map.get("jittestmodule:D"), nullptr);
  EXPECT_EQ(map.get("int"), &PyLong_Type);
  EXPECT_EQ(map.get("float"), &PyFloat_Type);

  map.clear();
  ASSERT_EQ(map.size(), 2);
  EXPECT_EQ(map.get("jittestmodule:C"), nullptr);
  EXPECT_EQ(map.get("jittestmodule:D"), nullptr);
  EXPECT_EQ(map.get("int"), &PyLong_Type);
  EXPECT_EQ(map.get("float"), &PyFloat_Type);
}
