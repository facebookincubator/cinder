// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "gtest/gtest.h"

#include "fixtures.h"
#include "testutil.h"

#include "Jit/ref.h"
#include "Jit/slot_gen.h"
#include "Python.h"

class SlotGenTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
    slot_gen_ = std::make_unique<jit::SlotGen>();
    ASSERT_NE(slot_gen_, nullptr);
  }

  Ref<> makeRawInstance(PyObject* type, PyObject* args, PyObject* kwargs) {
    auto dunder_new = Ref<>::steal(PyUnicode_FromString("__new__"));
    if (dunder_new.get() == nullptr) {
      return Ref<>(nullptr);
    }

    auto func = Ref<>::steal(PyObject_GetAttr(type, dunder_new));
    if (func.get() == nullptr) {
      return Ref<>(nullptr);
    }

    return Ref<>::steal(_PyObject_Call_Prepend(func, type, args, kwargs));
  }

  void TearDown() override {
    RuntimeTest::TearDown();
  }

  std::unique_ptr<jit::SlotGen> slot_gen_;
};

TEST_F(SlotGenTest, SimpleReprFuncGeneration) {
  const char* src = R"(
class Foo:
    def __str__(self):
        return "foo is the magic number"
)";
  Ref<PyTypeObject> foo(compileAndGet(src, "Foo"));
  ASSERT_NE(foo.get(), nullptr) << "Failed creating foo";

  auto dunder_str = Ref<>::steal(PyUnicode_FromString("__str__"));
  ASSERT_NE(dunder_str.get(), nullptr) << "Failed creating __str__ string";

  PyObject* strfunc = _PyType_Lookup(foo, dunder_str);
  ASSERT_NE(strfunc, nullptr) << "Failed looking up __str__";

  reprfunc tp_str = slot_gen_->genReprFuncSlot(foo, strfunc);
  ASSERT_NE(tp_str, nullptr);

  auto args = Ref<>::steal(PyTuple_New(0));
  ASSERT_NE(args, nullptr) << "Failed creating args";

  Ref<PyObject> instance(
      makeRawInstance(reinterpret_cast<PyObject*>(foo.get()), args, nullptr));

  auto result = Ref<>::steal(tp_str(instance));
  ASSERT_NE(result, nullptr);

  int cmp_res =
      PyUnicode_CompareWithASCIIString(result, "foo is the magic number");
  ASSERT_EQ(cmp_res, 0);
}

TEST_F(SlotGenTest, SimpleCallFuncGeneration) {
  const char* src = R"(
class Foo:
    def __call__(self, *args, **kwargs):
        return "foo is the magic number"
)";
  Ref<PyTypeObject> foo(compileAndGet(src, "Foo"));
  ASSERT_NE(foo.get(), nullptr) << "Failed creating foo";

  auto dunder_call = Ref<>::steal(PyUnicode_FromString("__call__"));
  ASSERT_NE(dunder_call.get(), nullptr) << "Failed creating __call__ string";

  PyObject* callfunc = _PyType_Lookup(foo, dunder_call);
  ASSERT_NE(callfunc, nullptr) << "Failed looking up __call__";

  ternaryfunc tp_call = slot_gen_->genCallSlot(foo, callfunc);
  ASSERT_NE(tp_call, nullptr);

  auto args = Ref<>::steal(PyTuple_New(0));
  ASSERT_NE(args, nullptr) << "Failed creating args";

  Ref<PyObject> instance(
      makeRawInstance(reinterpret_cast<PyObject*>(foo.get()), args, nullptr));

  auto result = Ref<>::steal(tp_call(instance, args, nullptr));
  ASSERT_NE(args, nullptr);

  int cmp_res =
      PyUnicode_CompareWithASCIIString(result, "foo is the magic number");
  ASSERT_EQ(cmp_res, 0);
}

TEST_F(SlotGenTest, SimpleGetAttrReturnsValue) {
  const char* src = R"(
class Foo:
  def __getattr__(self, name):
    return 42
)";
  Ref<PyTypeObject> foo(compileAndGet(src, "Foo"));
  ASSERT_NE(foo.get(), nullptr) << "Failed creating foo";

  auto getattr = Ref<>::steal(PyUnicode_FromString("__getattr__"));
  ASSERT_NE(getattr.get(), nullptr) << "Failed creating __getattr__ string";

  PyObject* dunder_getattr = _PyType_Lookup(foo, getattr);
  ASSERT_NE(dunder_getattr, nullptr) << "Failed looking up __getattr__";

  getattrofunc getattro = slot_gen_->genGetAttrSlot(foo, dunder_getattr);
  ASSERT_NE(getattro, nullptr);

  auto args = Ref<>::steal(PyTuple_New(0));
  Ref<PyObject> instance(
      makeRawInstance(reinterpret_cast<PyObject*>(foo.get()), args, nullptr));

  auto abc = Ref<>::steal(PyUnicode_FromString("abc"));
  ASSERT_NE(abc.get(), nullptr) << "Failed creating abc string";

  auto result = Ref<>::steal(getattro(instance, abc));

  ASSERT_EQ(Py_TYPE(result.get()), &PyLong_Type);
}

TEST_F(SlotGenTest, SimpleGetAttrClassValue) {
  const char* src = R"(
class Foo:
  abc = 'abc'
  def __getattr__(self, name):
    return 42
)";
  Ref<PyTypeObject> foo(compileAndGet(src, "Foo"));
  ASSERT_NE(foo.get(), nullptr) << "Failed creating foo";

  auto getattr = Ref<>::steal(PyUnicode_FromString("__getattr__"));
  ASSERT_NE(getattr.get(), nullptr) << "Failed creating __getattr__ string";

  PyObject* dunder_getattr = _PyType_Lookup(foo, getattr);
  ASSERT_NE(dunder_getattr, nullptr) << "Failed looking up __getattr__";

  auto abc = Ref<>::steal(PyUnicode_FromString("abc"));
  ASSERT_NE(abc.get(), nullptr) << "Failed creating abc string";

  getattrofunc getattro = slot_gen_->genGetAttrSlot(foo, dunder_getattr);
  ASSERT_NE(getattro, nullptr);

  auto args = Ref<>::steal(PyTuple_New(0));

  Ref<PyObject> instance(
      makeRawInstance(reinterpret_cast<PyObject*>(foo.get()), args, nullptr));

  auto result = Ref<>::steal(getattro(instance, abc));

  ASSERT_EQ(Py_TYPE(result.get()), &PyUnicode_Type);
}

TEST_F(SlotGenTest, SimpleDescrGet) {
  const char* src = R"(
class Foo:
  abc = 'abc'
  def __get__(self, obj, ctx):
    if obj is None:
      return 100
    elif ctx is None:
      return 200
    return 39 + obj + ctx
)";
  Ref<PyTypeObject> foo(compileAndGet(src, "Foo"));
  ASSERT_NE(foo.get(), nullptr) << "Failed creating foo";

  auto get = Ref<>::steal(PyUnicode_FromString("__get__"));
  ASSERT_NE(get.get(), nullptr) << "Failed creating __get__ string";

  PyObject* dunder_get = _PyType_Lookup(foo, get);
  ASSERT_NE(dunder_get, nullptr) << "Failed looking up __get__";

  auto abc = Ref<>::steal(PyUnicode_FromString("abc"));
  ASSERT_NE(abc.get(), nullptr) << "Failed creating abc string";

  descrgetfunc getfunc = slot_gen_->genGetDescrSlot(foo, dunder_get);
  ASSERT_NE(getfunc, nullptr);

  auto one = Ref<>::steal(PyLong_FromLong(1));
  auto two = Ref<>::steal(PyLong_FromLong(2));

  auto args = Ref<>::steal(PyTuple_New(0));

  Ref<PyObject> instance(
      makeRawInstance(reinterpret_cast<PyObject*>(foo.get()), args, nullptr));

  auto result = Ref<>::steal(getfunc(instance, one, two));

  ASSERT_EQ(Py_TYPE(result.get()), &PyLong_Type);
  ASSERT_EQ(PyLong_AsLong(result), 42);

  auto result2 = Ref<>::steal(getfunc(instance, nullptr, two));

  ASSERT_EQ(Py_TYPE(result2.get()), &PyLong_Type);
  ASSERT_EQ(PyLong_AsLong(result2), 100);

  auto result3 = Ref<>::steal(getfunc(instance, one, NULL));

  ASSERT_EQ(Py_TYPE(result3.get()), &PyLong_Type);
  ASSERT_EQ(PyLong_AsLong(result3), 200);
}
