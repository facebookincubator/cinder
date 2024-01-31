// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/runtime.h"

#include "cinderx/RuntimeTests/fixtures.h"

using namespace jit;

using ProfileRuntimeTest = RuntimeTest;

TEST_F(ProfileRuntimeTest, BasicProfileExample) {
  const char* src = R"(
class MyType:
    bar = 12

def foo(o):
    return o.bar

foo(MyType())
)";
  ASSERT_NO_FATAL_FAILURE(runAndProfileCode(src));

  Ref<PyTypeObject> my_type = getGlobal("MyType");
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

  auto& profile_runtime = Runtime::get()->profileRuntime();

  auto types = profile_runtime.getProfiledTypes(foo_code, load_attr);
  ASSERT_EQ(types.size(), 1);
  ASSERT_EQ(types[0], hir::Type::fromTypeExact(my_type));
}
