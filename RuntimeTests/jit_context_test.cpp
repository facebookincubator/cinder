// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/jit_context.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <memory>

class PyJITContextTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
    jit_ctx_ = new _PyJITContext();
    ASSERT_NE(jit_ctx_, nullptr) << "Failed creating jit context";
  }

  void TearDown() override {
    delete jit_ctx_;
    jit_ctx_ = nullptr;
    RuntimeTest::TearDown();
  }

  _PyJITContext* jit_ctx_;
};

TEST_F(PyJITContextTest, CompiledFunctionsAreDeoptimizedWhenCodeChanges) {
  const char* src = R"(
def func():
    return 12345
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "func"));
  ASSERT_NE(func.get(), nullptr) << "Failed creating func";

  vectorcallfunc old_entrypoint = func->vectorcall;
  _PyJIT_Result st = _PyJITContext_CompileFunction(jit_ctx_, func);
  ASSERT_EQ(st, PYJIT_RESULT_OK) << "Failed compiling";

  // Create a new function object so that we can grab its code object and
  // assign it to the original function, at which point func should be
  // de-optimized
  const char* src2 = R"(
def func2():
    return 2

func.__code__ = func2.__code__
)";
  auto globals = Ref<>::steal(PyDict_New());
  ASSERT_NE(globals.get(), nullptr) << "Failed creating globals";
  ASSERT_EQ(PyDict_SetItemString(globals, "func", func), 0)
      << "Failed updating globals";

  auto result =
      Ref<>::steal(PyRun_String(src2, Py_file_input, globals, globals));
  ASSERT_NE(result.get(), nullptr) << "Failed executing code";

  // After de-optimization, the entrypoint should have been restored to the
  // original value
  ASSERT_EQ(func->vectorcall, old_entrypoint) << "entrypoint wasn't restored";
}

TEST_F(PyJITContextTest, UnwatchableBuiltins) {
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
  ASSERT_EQ(_PyJITContext_CompileFunction(jit_ctx_, func), PYJIT_RESULT_OK);

  auto empty_tuple = Ref<>::steal(PyTuple_New(0));
  auto result = Ref<>::steal(PyObject_Call(func, empty_tuple, nullptr));
  ASSERT_EQ(result, Py_None);
}
