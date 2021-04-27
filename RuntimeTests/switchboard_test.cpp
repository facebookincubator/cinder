// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "gtest/gtest.h"

#include "Jit/ref.h"
#include "fixtures.h"
#include "testutil.h"

#include "Python.h"
#include "switchboard.h"

class SwitchboardTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
    switchboard_ = Switchboard_New();
    ASSERT_NE(switchboard_, nullptr);
  }

  void TearDown() override {
    Py_DECREF(switchboard_);
    RuntimeTest::TearDown();
  }

  Switchboard* switchboard_;
};

static void
callback(PyObject* /* handle */, PyObject* arg, PyObject* /* obj_ref */) {
  PyDict_SetItemString(arg, "testing", PyLong_FromLong(123));
}

TEST_F(SwitchboardTest, NotifyCallsRegisteredSubscriptions) {
  const char* src = R"(
class Foo:
    pass
)";
  Ref<PyObject> klass(compileAndGet(src, "Foo"));
  ASSERT_NE(klass.get(), nullptr) << "Failed creating class";

  auto data = Ref<>::steal(PyDict_New());
  ASSERT_NE(data.get(), nullptr) << "Failed creating data container";

  auto data1 = Ref<>::steal(PyDict_New());
  ASSERT_NE(data1.get(), nullptr) << "Failed creating data container";

  auto subscr =
      Ref<>::steal(Switchboard_Subscribe(switchboard_, klass, callback, data));
  ASSERT_NE(subscr.get(), nullptr) << "Failed subscribing";

  auto subscr1 =
      Ref<>::steal(Switchboard_Subscribe(switchboard_, klass, callback, data1));
  ASSERT_NE(subscr1.get(), nullptr) << "Failed subscribing";

  // Signal that klass was modified
  Switchboard_Notify(switchboard_, klass);

  Ref<PyObject> value(PyDict_GetItemString(data, "testing"));
  ASSERT_NE(value.get(), nullptr) << "Didn't set key in data";
  ASSERT_TRUE(PyLong_CheckExact(value)) << "Didn't set an int in data";
  ASSERT_EQ(PyLong_AsLong(value), 123);

  Ref<PyObject> value1(PyDict_GetItemString(data1, "testing"));
  ASSERT_NE(value1, nullptr) << "Didn't set key in data";
  ASSERT_TRUE(PyLong_CheckExact(value1)) << "Didn't set an int in data";
  ASSERT_EQ(PyLong_AsLong(value1), 123);
}

TEST_F(SwitchboardTest, UnsubscribeRemovesExistingSubscription) {
  const char* src = R"(
class Foo:
    pass
)";
  Ref<PyObject> klass(compileAndGet(src, "Foo"));
  ASSERT_NE(klass.get(), nullptr) << "Failed creating class";

  auto data = Ref<>::steal(PyDict_New());
  ASSERT_NE(data.get(), nullptr) << "Failed creating data container";

  auto subscr =
      Ref<>::steal(Switchboard_Subscribe(switchboard_, klass, callback, data));
  ASSERT_NE(subscr.get(), nullptr) << "Failed subscribing";

  // Signal that klass was modified
  Switchboard_Notify(switchboard_, klass);

  Ref<PyObject> value(PyDict_GetItemString(data, "testing"));
  ASSERT_NE(value.get(), nullptr) << "Didn't set key in data";
  ASSERT_TRUE(PyLong_CheckExact(value)) << "Didn't set an int in data";
  ASSERT_EQ(PyLong_AsLong(value), 123);

  // Clear data, unsubscribe, and make sure it isn't invoked again
  PyDict_Clear(data);
  Switchboard_Unsubscribe(switchboard_, subscr);
  Switchboard_Notify(switchboard_, klass);

  Ref<PyObject> value2(PyDict_GetItemString(data, "testing"));
  ASSERT_EQ(value2.get(), nullptr) << "Should not have invoked callback";
}

TEST_F(SwitchboardTest, SubscribersNotifiedWhenObjectIsGCed) {
  const char* src = R"(
class Foo:
    pass
)";
  Ref<PyObject> klass(compileAndGet(src, "Foo"));
  ASSERT_NE(klass.get(), nullptr) << "Failed creating class";

  auto data = Ref<>::steal(PyDict_New());
  ASSERT_NE(data.get(), nullptr) << "Failed creating data container";

  PyObject* ref;
  {
    auto instance = Ref<>::steal(PyObject_Call(klass, PyTuple_New(0), nullptr));
    ASSERT_NE(instance.get(), nullptr) << "Failed creating instance";

    ref = Switchboard_Subscribe(switchboard_, instance, callback, data);
    ASSERT_NE(ref, nullptr) << "Failed subscribing";
  }

  // Instance should have been reclaimed when we exited the scope above,
  // triggering the subscription
  auto subscr = Ref<>::steal(ref);
  Ref<PyObject> value(PyDict_GetItemString(data, "testing"));
  ASSERT_NE(value.get(), nullptr) << "Didn't set key in data";
  ASSERT_TRUE(PyLong_CheckExact(value)) << "Didn't set an int in data";
  ASSERT_EQ(PyLong_AsLong(value), 123);

  int result = Switchboard_Unsubscribe(switchboard_, subscr);
  ASSERT_EQ(result, 0) << "subscription should have been cleared";
}

TEST_F(SwitchboardTest, SubscribersNotifiedWhenCodeIsSetOnFunction) {
  const char* src = R"(
def func1():
    return 1
)";
  Ref<PyObject> func1(compileAndGet(src, "func1"));
  ASSERT_NE(func1.get(), nullptr) << "Failed creating func1";

  auto data = Ref<>::steal(PyDict_New());
  ASSERT_NE(data.get(), nullptr) << "Failed creating data container";

  Switchboard* switchboard = (Switchboard*)_PyFunction_GetSwitchboard();
  ASSERT_NE(switchboard, nullptr) << "Failed getting function switchboard";

  auto subscr =
      Ref<>::steal(Switchboard_Subscribe(switchboard, func1, callback, data));
  ASSERT_NE(subscr.get(), nullptr) << "Failed subscribing";

  auto globals = Ref<>::steal(PyDict_New());
  ASSERT_NE(globals.get(), nullptr) << "Failed creating globals";
  ASSERT_EQ(PyDict_SetItemString(globals, "func1", func1), 0)
      << "Failed updating globals";

  // Create a new function object so that we can grab its code object and
  // assign it to the original function, at which point our subscription
  // should be triggered
  const char* src2 = R"(
def func2():
    return 2

func1.__code__ = func2.__code__
)";
  auto result =
      Ref<>::steal(PyRun_String(src2, Py_file_input, globals, globals));
  ASSERT_NE(result.get(), nullptr) << "Failed executing code";

  Ref<PyObject> value(PyDict_GetItemString(data, "testing"));
  ASSERT_NE(value.get(), nullptr) << "Didn't set key in data";
  ASSERT_TRUE(PyLong_CheckExact(value)) << "Didn't set an int in data";
  ASSERT_EQ(PyLong_AsLong(value), 123);
}

TEST_F(SwitchboardTest, DISABLED_SubscribersNotifiedWhenTypeIsChanged) {
  const char* src = R"(
class Foo:
    pass
)";
  Ref<PyObject> klass(compileAndGet(src, "Foo"));
  ASSERT_NE(klass.get(), nullptr) << "Failed creating Foo";

  auto data = Ref<>::steal(PyDict_New());
  ASSERT_NE(data.get(), nullptr) << "Failed creating data container";

  Switchboard* switchboard = (Switchboard*)_PyType_GetSwitchboard();
  ASSERT_NE(switchboard, nullptr) << "Failed getting type switchboard";

  Py_DECREF(PyObject_GetAttrString(
      _PyObject_FastCallDict(klass, NULL, 0, NULL), "__hash__"));
  auto subscr =
      Ref<>::steal(Switchboard_Subscribe(switchboard, klass, callback, data));
  ASSERT_NE(subscr.get(), nullptr) << "Failed subscribing";

  auto globals = Ref<>::steal(PyDict_New());
  ASSERT_NE(globals.get(), nullptr) << "Failed creating globals";
  ASSERT_EQ(PyDict_SetItemString(globals, "Foo", klass), 0)
      << "Failed updating globals";

  // Modify the type object which should trigger our subscription
  const char* src2 = R"(
Foo.bar = 12345
)";
  auto result =
      Ref<>::steal(PyRun_String(src2, Py_file_input, globals, globals));
  ASSERT_NE(result.get(), nullptr) << "Failed executing code";

  Ref<PyObject> value(PyDict_GetItemString(data, "testing"));
  ASSERT_NE(value.get(), nullptr) << "Didn't set key in data";
  ASSERT_TRUE(PyLong_CheckExact(value)) << "Didn't set an int in data";
  ASSERT_EQ(PyLong_AsLong(value), 123);
}
