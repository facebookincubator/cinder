#include "gtest/gtest.h"

#include "fixtures.h"
#include "testutil.h"

#include "Jit/jit_context.h"
#include "Jit/ref.h"
#include "switchboard.h"

class PyJITContextTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
    _PyJITContext_Init();
    auto compiler = new jit::Compiler();
    ASSERT_NE(compiler, nullptr);
    jit_ctx_ = _PyJITContext_New(compiler);
    ASSERT_NE(jit_ctx_, nullptr) << "Failed creating jit context";
  }

  void TearDown() override {
    Py_CLEAR(jit_ctx_);
    _PyJITContext_Finalize();
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

func.__code__ = func.__code__
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

  // And there should be no subscriptions for the function
  Switchboard* sb = (Switchboard*)_PyFunction_GetSwitchboard();
  ASSERT_NE(sb, nullptr) << "Failed getting function switchboard";
  ASSERT_EQ(Switchboard_GetNumSubscriptions(sb, func), 0)
      << "Didn't remove subscription";
}

TEST_F(
    PyJITContextTest,
    DISABLED_CompiledFunctionsAreDeoptimizedWhenTypeDependenciesChange) {
  const char* src = R"(
def func():
    return 12345
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "func"));
  ASSERT_NE(func.get(), nullptr) << "Failed creating func";

  vectorcallfunc old_entrypoint = func->vectorcall;
  _PyJIT_Result st = _PyJITContext_CompileFunction(jit_ctx_, func);
  ASSERT_EQ(st, PYJIT_RESULT_OK) << "Failed compiling";

  // Create a type object so that we can register it as a dependency of func
  const char* foo_src = R"(
class Foo:
    pass
)";
  Ref<PyObject> foo(compileAndGet(foo_src, "Foo"));
  ASSERT_NE(foo.get(), nullptr) << "Failed creating Foo";

  // Register type as a dependency
  int st2 = _PyJITContext_AddTypeDependency(jit_ctx_, func, foo);
  Py_DECREF(PyObject_GetAttrString(
      _PyObject_FastCallDict(foo, NULL, 0, NULL), "__hash__"));
  ASSERT_TRUE(PyType_HasFeature(
      ((PyTypeObject*)foo.get()), Py_TPFLAGS_VALID_VERSION_TAG));
  ASSERT_EQ(st2, 0) << "Failed registering Foo as a dependency of func";

  // Create a type object so that we can register it as a dependency of func
  const char* bar_src = R"(
class Bar:
    pass
)";
  Ref<PyObject> bar(compileAndGet(bar_src, "Bar"));
  ASSERT_NE(bar.get(), nullptr) << "Failed creating Bar";

  // Register type as a dependency
  st2 = _PyJITContext_AddTypeDependency(jit_ctx_, func, bar);
  PyObject_GetAttrString(
      _PyObject_FastCallDict(foo, NULL, 0, NULL), "__hash__");
  ASSERT_TRUE(PyType_HasFeature(
      ((PyTypeObject*)foo.get()), Py_TPFLAGS_VALID_VERSION_TAG));
  ASSERT_EQ(st2, 0) << "Failed registering type as a dependency of func";

  // Mutate Foo, which should deoptimize func
  Ref<PyObject> globals(PyDict_New());
  ASSERT_NE(globals.get(), nullptr) << "Failed creating globals";
  ASSERT_EQ(PyDict_SetItemString(globals, "Foo", foo), 0)
      << "Failed updating globals";
  const char* mut_src = R"(
Foo.bar = 12345
)";
  Ref<PyObject> result(PyRun_String(mut_src, Py_file_input, globals, globals));
  PyErr_Print();
  ASSERT_NE(result.get(), nullptr) << "Failed executing code";

  // After de-optimization, the entrypoint should have been restored to the
  // original value
  ASSERT_EQ(func->vectorcall, old_entrypoint) << "entrypoint wasn't restored";

  // And there should be no subscriptions for the function
  Switchboard* sb = (Switchboard*)_PyFunction_GetSwitchboard();
  ASSERT_NE(sb, nullptr) << "Failed getting function switchboard";
  ASSERT_EQ(Switchboard_GetNumSubscriptions(sb, func), 0)
      << "Didn't remove subscription";

  // And there should be no subscriptions for either type
  sb = (Switchboard*)_PyType_GetSwitchboard();
  ASSERT_NE(sb, nullptr) << "Failed getting type switchboard";
  ASSERT_EQ(Switchboard_GetNumSubscriptions(sb, foo), 0)
      << "Didn't remove subscription";
  ASSERT_EQ(Switchboard_GetNumSubscriptions(sb, bar), 0)
      << "Didn't remove subscription";
}
