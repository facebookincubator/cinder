// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/profile_data.h"

#include "RuntimeTests/fixtures.h"

using namespace jit;

using ProfileDataTest = RuntimeTest;

TEST_F(ProfileDataTest, UnregistersTypeWithModifiedName) {
  const char* src = R"(
class MyType:
    bar = 12

def foo(o):
    return o.bar

foo(MyType())
)";
  std::string data;
  ASSERT_NO_FATAL_FAILURE(runCodeAndCollectProfile(src, data));

  std::istringstream stream{data};
  ASSERT_TRUE(readProfileData(stream));
  ASSERT_TRUE(runCode(src));

  Ref<> my_type = getGlobal("MyType");
  ASSERT_NE(my_type, nullptr);

  Ref<PyFunctionObject> foo(getGlobal("foo"));
  ASSERT_NE(foo, nullptr);
  BorrowedRef<PyCodeObject> foo_code = foo->func_code;
  BorrowedRef<PyBytesObject> foo_bc = foo_code->co_code;
  ASSERT_TRUE(PyBytes_CheckExact(foo_bc));

  // Find the offset of the LOAD_ATTR in foo_bc so we can look up its profile
  // data.
  const char* raw_bc = PyBytes_AS_STRING(foo_bc);
  BCOffset load_attr{-1};
  for (Py_ssize_t i = 0, n = PyBytes_Size(foo_bc); i < n;
       i += sizeof(_Py_CODEUNIT)) {
    if (raw_bc[i] == LOAD_ATTR) {
      load_attr = BCOffset{i};
      break;
    }
  }
  ASSERT_NE(load_attr, -1);

  const CodeProfileData* profile_data = getProfileData(foo_code);
  ASSERT_NE(profile_data, nullptr);
  PolymorphicTypes types = getProfiledTypes(*profile_data, load_attr);
  ASSERT_EQ(types.size(), 1);
  ASSERT_EQ(types[0].size(), 1);
  ASSERT_EQ(types[0][0], my_type);

  // Change MyType's name and check that it no longer shows up in
  // getProfiledTypes().
  auto new_name = Ref<>::steal(PyUnicode_FromString("YourType"));
  ASSERT_NE(new_name, nullptr);
  ASSERT_EQ(PyObject_SetAttrString(my_type, "__name__", new_name), 0);
  types = getProfiledTypes(*profile_data, load_attr);
  ASSERT_EQ(types.size(), 1);
  ASSERT_EQ(types[0].size(), 1);
  ASSERT_EQ(types[0][0], nullptr);
}
