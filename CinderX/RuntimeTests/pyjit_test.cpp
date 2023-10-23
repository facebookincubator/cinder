// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/pyjit.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <cstdlib>
#include <memory>
#include <string>

static const char* PYTHONJIT = "PYTHONJIT";

class PyJITTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();

    if (_PyJIT_IsEnabled()) {
      is_enabled = 1;
      _PyJIT_Finalize();
    }

    auto tmp_env = getenv(PYTHONJIT);
    if (tmp_env != nullptr) {
      _pythonjit_env = std::make_unique<std::string>(tmp_env);
    }
  }

  void TearDown() override {
    setenv(
        PYTHONJIT,
        _pythonjit_env == nullptr ? nullptr : _pythonjit_env->c_str(),
        1);

    if (is_enabled) {
      _PyJIT_Initialize();
    }

    RuntimeTest::TearDown();
  }

  int is_enabled;
  std::unique_ptr<std::string> _pythonjit_env;
};

TEST_F(PyJITTest, PyInitialization) {
  // test without the environment variable
  unsetenv(PYTHONJIT);
  auto result = _PyJIT_Initialize();
  ASSERT_EQ(result, 0);
  ASSERT_EQ(_PyJIT_IsEnabled(), 0);

  // test without the environment variable
  setenv(PYTHONJIT, "1", 1);
  result = _PyJIT_Initialize();
  ASSERT_EQ(result, 0);
  ASSERT_EQ(_PyJIT_IsEnabled(), 1);

  _PyJIT_Finalize();
}

TEST_F(RuntimeTest, ReadingFromCodeRuntimeReadsCode) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);
  PyCodeObject* code = func->func_code;
  Runtime* ngen_rt = Runtime::get();
  CodeRuntime* code_rt = runtime.allocateCodeRuntime(
      code, func->func_globals, FrameMode::kShadow, 0, 0, 0, 0);
  EXPECT_EQ(
      *reinterpret_cast<PyCodeObject**>(
          reinterpret_cast<byte*>(code_rt) + __strobe_CodeRuntime_py_code),
      reinterpret_cast<byte*>(code));
}

TEST_F(RuntimeTest, ReadingFromRuntimeFrameStateReadsCode) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);
  PyCodeObject* code = func->func_code;
  RuntimeFrameState rtfs(code, func->func_globals);
  EXPECT_EQ(
      *reinterpret_cast<PyCodeObject**>(
          reinterpret_cast<byte*>(&rtfs) + __strobe_RuntimeFrameState_py_code),
      reinterpret_cast<byte*>(code));
}
