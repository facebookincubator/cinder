// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/jit_context.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <memory>

class JITContextTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
    jit_ctx_ = std::make_unique<jit::Context>();
    ASSERT_NE(jit_ctx_, nullptr) << "Failed creating jit context";
  }

  void TearDown() override {
    jit_ctx_.reset();
    RuntimeTest::TearDown();
  }

  std::unique_ptr<jit::Context> jit_ctx_;
};

TEST_F(JITContextTest, UnwatchableBuiltins) {
  // This is a C++ test rather than in test_cinderjit so we can guarantee a
  // fresh runtime state with a watchable builtins dict when the test begins.
  const char* py_src = R"(
import builtins

def del_foo():
    global foo
    del foo

def func():
    foo
    builtins.__dict__[42] = 42
    del_foo()

foo = "hello"
)";

  Ref<PyFunctionObject> func(compileAndGet(py_src, "func"));
  ASSERT_EQ(jit_ctx_->compileFunc(func), PYJIT_RESULT_OK);

  auto empty_tuple = Ref<>::steal(PyTuple_New(0));
  auto result = Ref<>::steal(PyObject_Call(func, empty_tuple, nullptr));
  ASSERT_EQ(result, Py_None);
}
