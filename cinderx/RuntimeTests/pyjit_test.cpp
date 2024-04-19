// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/pyjit.h"
#include "cinderx/Jit/runtime.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

#include <cstdlib>
#include <memory>
#include <string>

using namespace jit;
using std::byte;

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
    if (_pythonjit_env == nullptr) {
      unsetenv(PYTHONJIT);
    } else {
      setenv(PYTHONJIT, _pythonjit_env->c_str(), 1);
    }

    if (is_enabled) {
      _PyJIT_Initialize();
    }

    RuntimeTest::TearDown();
  }

  int is_enabled;
  std::unique_ptr<std::string> _pythonjit_env;
};

TEST_F(RuntimeTest, ReadingFromCodeRuntimeReadsCode) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);
  auto code = reinterpret_cast<PyCodeObject*>(func->func_code);
  Runtime* ngen_rt = Runtime::get();
  CodeRuntime* code_rt = ngen_rt->allocateCodeRuntime(
      code, func->func_builtins, func->func_globals);
  EXPECT_EQ(
      *reinterpret_cast<PyCodeObject**>(
          reinterpret_cast<byte*>(code_rt) + __strobe_CodeRuntime_py_code),
      code);
}

TEST_F(RuntimeTest, ReadingFromRuntimeFrameStateReadsCode) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);
  auto code = reinterpret_cast<PyCodeObject*>(func->func_code);
  RuntimeFrameState rtfs(code, func->func_globals, func->func_builtins);
  EXPECT_EQ(
      *reinterpret_cast<PyCodeObject**>(
          reinterpret_cast<byte*>(&rtfs) + __strobe_RuntimeFrameState_py_code),
      code);
}
