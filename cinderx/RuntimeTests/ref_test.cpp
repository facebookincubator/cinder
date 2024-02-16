// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Common/ref.h"

#include "cinderx/RuntimeTests/fixtures.h"

#include <unordered_set>

using BorrowedRefTest = RuntimeTest;
using RefTest = RuntimeTest;

static void takeObject(PyObject*) {}
static void takeType(PyTypeObject*) {}
static void stealRef(Ref<>) {}
static void takeBorrowedRef(BorrowedRef<>) {}

TEST_F(RefTest, Equality) {
  auto obj = Ref<>::create(Py_None);
  EXPECT_EQ(obj, Py_None);

  auto ty = Ref<PyTypeObject>::create(PyExc_StopIteration);
  EXPECT_EQ(ty, reinterpret_cast<PyTypeObject*>(PyExc_StopIteration));

  EXPECT_NE(obj, ty);

  auto obj2 = Ref<>::create(Py_None);
  EXPECT_EQ(obj, obj2);
}

TEST_F(RefTest, ImplicitConversions) {
  auto obj = Ref<>::create(Py_None);
  auto ty = Ref<PyTypeObject>::create(PyExc_StopIteration);
  takeObject(obj);
  takeObject(ty);
  takeType(ty);
}

TEST_F(RefTest, MoveConstruction) {
  auto dict = Ref<>::steal(PyDict_New());
  ASSERT_NE(dict, nullptr);

  Py_ssize_t refcnt = dict->ob_refcnt;
  Ref<> dict2(std::move(dict));
  EXPECT_EQ(dict, nullptr);
  ASSERT_NE(dict2, nullptr);
  EXPECT_EQ(dict2->ob_refcnt, refcnt);

  Ref<> dict3 = std::move(dict2);
  EXPECT_EQ(dict2, nullptr);
  ASSERT_NE(dict3, nullptr);
  EXPECT_EQ(dict3->ob_refcnt, refcnt);

  // Functions that steal refs
  auto src4 = Ref<>::create(Py_None);
  stealRef(std::move(src4));
  EXPECT_EQ(src4, nullptr);
}

TEST_F(RefTest, MoveAssignment) {
  auto list = Ref<>::steal(PyList_New(2));
  ASSERT_NE(list, nullptr);
  Py_ssize_t refcnt = list->ob_refcnt;

  Ref<> list2;
  ASSERT_EQ(list2, nullptr);

  list2 = std::move(list);
  EXPECT_EQ(list, nullptr);
  ASSERT_NE(list2, nullptr);
  EXPECT_EQ(list2->ob_refcnt, refcnt);

  // Self move
  Ref<>* listp = &list2;
  list2 = std::move(*listp);
  ASSERT_NE(list2, nullptr);
  EXPECT_EQ(list2->ob_refcnt, refcnt);
}

TEST_F(RefTest, StolenRefs) {
  // Managing new refs returned from runtime calls
  auto dict = Ref<>::steal(PyDict_New());
  EXPECT_EQ(dict->ob_refcnt, 1);

  // Make the refcount behavior clear
  auto d = Ref<>(Ref<>::create(dict.get()));
  EXPECT_EQ(dict->ob_refcnt, 2);
}

TEST_F(RefTest, Reset) {
  auto list = Ref<>::steal(PyList_New(2));
  ASSERT_NE(list, nullptr);

  Ref<> ref(Ref<>::create(Py_None));
  ASSERT_EQ(ref, Py_None);

  ref.reset(list.get());
  ASSERT_EQ(ref, list.get());

  // Self reset
  Py_ssize_t refcnt = ref->ob_refcnt;
  ref.reset(ref.get());
  ASSERT_EQ(ref, list.get());
  EXPECT_EQ(ref->ob_refcnt, refcnt);

  // Clear
  ref.reset();
  EXPECT_EQ(ref, nullptr);
  EXPECT_EQ(list->ob_refcnt, refcnt - 1);
}

TEST_F(RefTest, UseInContainer) {
  std::unordered_set<Ref<PyObject>> objs;
  auto dict = Ref<>::steal(PyDict_New());
  ASSERT_NE(dict, nullptr);

  auto refcnt = dict->ob_refcnt;
  auto p = objs.emplace(Ref<>::create(dict.get()));
  EXPECT_TRUE(p.second);
  EXPECT_EQ(dict->ob_refcnt, refcnt + 1);
  EXPECT_FALSE(objs.emplace(Ref<>::create(dict.get())).second);

  EXPECT_EQ(objs.erase(dict), 1);
  EXPECT_EQ(objs.erase(Ref<>::create(dict.get())), 0);
  EXPECT_EQ(dict->ob_refcnt, refcnt);
}

TEST_F(BorrowedRefTest, Equality) {
  BorrowedRef<> obj(Py_None);
  EXPECT_EQ(obj, Py_None);

  BorrowedRef<PyTypeObject> ty(PyExc_StopIteration);
  EXPECT_EQ(ty, reinterpret_cast<PyTypeObject*>(PyExc_StopIteration));
  EXPECT_NE(obj, ty);

  BorrowedRef<> obj2(Py_None);
  EXPECT_EQ(obj, obj2);
}

TEST_F(BorrowedRefTest, ImplicitConversions) {
  BorrowedRef<PyObject> obj(Py_None);
  BorrowedRef<PyTypeObject> ty(PyExc_StopIteration);
  takeObject(obj);
  takeObject(ty);
  takeType(ty);

  Ref<> dict = Ref<>::steal(PyDict_New());
  takeBorrowedRef(dict);
}

TEST_F(BorrowedRefTest, Refcounting) {
  auto dict = Ref<>::steal(PyDict_New());
  Py_ssize_t refcnt = dict->ob_refcnt;

  BorrowedRef<> bdict = dict;
  EXPECT_EQ(bdict->ob_refcnt, refcnt);
  EXPECT_EQ(dict->ob_refcnt, refcnt);
}

TEST_F(BorrowedRefTest, MoveConstruction) {
  BorrowedRef<> src(Py_None);
  ASSERT_EQ(src, Py_None);

  BorrowedRef<> dst(std::move(src));
  EXPECT_EQ(src, Py_None);
  EXPECT_EQ(dst, Py_None);

  BorrowedRef<> dst2 = std::move(dst);
  EXPECT_EQ(dst, Py_None);
  EXPECT_EQ(dst2, Py_None);
}

TEST_F(BorrowedRefTest, MoveAssignment) {
  BorrowedRef<> src(Py_None);
  ASSERT_EQ(src, Py_None);

  BorrowedRef<> dst;
  ASSERT_EQ(dst, nullptr);

  dst = std::move(src);
  EXPECT_EQ(src, Py_None);
  EXPECT_EQ(dst, Py_None);

  // Self move
  BorrowedRef<>* dstp = &dst;
  dst = std::move(*dstp);
  EXPECT_EQ(dst, Py_None);
}

TEST_F(BorrowedRefTest, Reset) {
  BorrowedRef<> ref(Py_None);
  ASSERT_EQ(ref, Py_None);

  auto dict = Ref<>::steal(PyDict_New());
  ref.reset(dict.get());
  EXPECT_EQ(ref.get(), dict.get());

  ref.reset();
  EXPECT_EQ(ref, nullptr);
}
