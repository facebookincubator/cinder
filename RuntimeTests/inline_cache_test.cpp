// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"
#include "StrictModules/Objects/objects.h"
#include "cinder/exports.h"
#include "unicodeobject.h"

#include "Jit/inline_cache.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <cstring>

class InlineCacheTest : public RuntimeTest {};

TEST_F(InlineCacheTest, LoadTypeMethodCacheLookUp) {
  const char* src = R"(
from abc import ABCMeta, abstractmethod

class RequestContext:

  @classmethod
  def class_meth(cls):
    pass

  @staticmethod
  def static_meth():
    pass

  def regular_meth():
    pass

class_meth = RequestContext.class_meth.__func__
static_meth = RequestContext.static_meth
regular_meth = RequestContext.regular_meth
)";
  Ref<PyObject> globals(MakeGlobals());
  ASSERT_NE(globals.get(), nullptr) << "Failed creating globals";

  auto locals = Ref<>::steal(PyDict_New());
  ASSERT_NE(locals.get(), nullptr) << "Failed creating locals";

  auto st = Ref<>::steal(PyRun_String(src, Py_file_input, globals, locals));
  ASSERT_NE(st.get(), nullptr) << "Failed executing code";

  PyObject* klass = PyDict_GetItemString(locals, "RequestContext");
  ASSERT_NE(klass, nullptr) << "Couldn't get class RequestContext";

  auto py_class_meth = Ref<>::steal(PyUnicode_FromString("class_meth"));
  jit::LoadTypeMethodCache cache;
  auto res = cache.lookup(klass, py_class_meth);
  ASSERT_EQ(res.inst, klass)
      << "Expected instance to be equal to class from cache look up";
  PyObject* class_meth = PyDict_GetItemString(locals, "class_meth");
  ASSERT_EQ(PyObject_RichCompareBool(res.func, class_meth, Py_EQ), 1)
      << "Expected method " << class_meth << " to be equal from cache lookup";
  ASSERT_EQ(cache.value, res.func)
      << "Expected method " << py_class_meth << " to be cached";

  for (auto& meth : {"static_meth", "regular_meth"}) {
    auto name = Ref<>::steal(PyUnicode_FromString(meth));
    jit::LoadTypeMethodCache cache;
    auto res = cache.lookup(klass, name);
    ASSERT_EQ(res.func, Py_None)
        << "Expected first part of cache result to be Py_None";
    PyObject* py_meth = PyDict_GetItemString(locals, meth);
    ASSERT_EQ(PyObject_RichCompareBool(res.inst, py_meth, Py_EQ), 1)
        << "Expected method " << meth << " to be equal from cache lookup";
    ASSERT_EQ(cache.value, res.inst)
        << "Expected method " << meth << " to be cached";
  }
}
