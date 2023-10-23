// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include <gtest/gtest.h>

#include "Python.h"
#include "internal/pycore_interp.h"

#include "Jit/code_allocator.h"
#include "Jit/hir/builder.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/parser.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/ref.h"

#include "RuntimeTests/testutil.h"

#define JIT_TEST_MOD_NAME "jittestmodule"

class RuntimeTest : public ::testing::Test {
 public:
  RuntimeTest(bool compile_static = false) : compile_static_(compile_static) {}

  void SetUp() override {
    Py_Initialize();
    ASSERT_TRUE(Py_IsInitialized());
    if (compile_static_) {
      globals_ = MakeGlobalsStrict();
    } else {
      globals_ = MakeGlobals();
    }
    ASSERT_NE(globals_, nullptr);
  }

  bool runCode(const char* src) {
    return runCodeModuleExec(src, "compiler", "exec_cinder");
  }

  bool runStaticCode(const char* src) {
    return runCodeModuleExec(src, "compiler.static", "exec_static");
  }

  bool runCodeModuleExec(
      const char* src,
      const char* compiler_module,
      const char* exec_fn) {
    auto compiler = Ref<>::steal(PyImport_ImportModule(compiler_module));
    if (compiler == nullptr) {
      return false;
    }
    auto exec_static = Ref<>::steal(PyObject_GetAttrString(compiler, exec_fn));
    if (exec_static == nullptr) {
      return false;
    }
    auto src_code = Ref<>::steal(PyUnicode_FromString(src));
    if (src_code == nullptr) {
      return false;
    }
    auto mod_name = Ref<>::steal(PyUnicode_FromString(JIT_TEST_MOD_NAME));
    if (mod_name == nullptr) {
      return false;
    }
    auto res = Ref<>::steal(PyObject_CallFunctionObjArgs(
        exec_static,
        src_code.get(),
        globals_.get(),
        globals_.get(),
        mod_name.get(),
        nullptr));
    return res != nullptr;
  }

  // Run some code with profiling enabled, and save the resulting profile data.
  void runAndProfileCode(const char* src);

  Ref<> compileAndGet(const char* src, const char* name) {
    if (!runCode(src)) {
      return Ref<>(nullptr);
    }
    return getGlobal(name);
  }

  Ref<> compileStaticAndGet(const char* src, const char* name) {
    if (!runStaticCode(src)) {
      if (PyErr_Occurred()) {
        PyErr_Print();
      }
      return Ref<>(nullptr);
    }
    return getGlobal(name);
  }

  Ref<> getGlobal(const char* name) {
    PyObject* obj = PyDict_GetItemString(globals_, name);
    return Ref<>::create(obj);
  }

  ::testing::AssertionResult isIntEquals(BorrowedRef<> obj, long expected) {
    EXPECT_NE(obj, nullptr) << "object is null";
    EXPECT_TRUE(PyLong_CheckExact(obj)) << "object is not an exact int";
    int overflow;
    long result = PyLong_AsLongAndOverflow(obj, &overflow);
    EXPECT_EQ(overflow, 0) << "conversion to long overflowed";
    if (result == expected) {
      return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
        << "expected " << expected << " but found " << result;
  }

  Ref<> MakeGlobals() {
    auto module = Ref<>::steal(PyModule_New(JIT_TEST_MOD_NAME));
    if (module == nullptr) {
      return module;
    }
    auto globals = Ref<>::create(PyModule_GetDict(module));

    if (AddModuleWithBuiltins(module, globals)) {
      return Ref<>(nullptr);
    }
    return globals;
  }

  Ref<> MakeGlobalsStrict() {
    auto globals = Ref<>::steal(PyDict_New());
    if (globals == nullptr) {
      return globals;
    }
    if (PyDict_SetItemString(
            globals,
            "__name__",
            Ref<>::steal(PyUnicode_FromString(JIT_TEST_MOD_NAME)))) {
      return Ref<>(nullptr);
    }
    auto args = Ref<>::steal(PyTuple_New(2));
    if (args == nullptr) {
      return args;
    }
    if (PyTuple_SetItem(args, 0, globals.get())) {
      return Ref<>(nullptr);
    }
    Py_INCREF(globals.get());
    auto kwargs = Ref<>::steal(PyDict_New());
    if (kwargs == nullptr) {
      return kwargs;
    }
    auto module = Ref<>::steal(
        PyStrictModule_New(&PyStrictModule_Type, args.get(), kwargs.get()));
    if (module == nullptr) {
      return module;
    }
    auto dict = Ref<>::steal(PyDict_New());
    if (dict == nullptr) {
      return dict;
    }
    reinterpret_cast<PyModuleObject*>(module.get())->md_dict = dict.release();
    if (AddModuleWithBuiltins(module, globals)) {
      return Ref<>(nullptr);
    }
    return globals;
  }

  bool AddModuleWithBuiltins(BorrowedRef<> module, BorrowedRef<> globals) {
    // Look up the builtins module to mimic real code, rather than using its
    // dict.
    auto modules = PyThreadState_Get()->interp->modules;
    auto builtins = PyDict_GetItemString(modules, "builtins");
    if (PyDict_SetItemString(globals, "__builtins__", builtins) != 0 ||
        PyDict_SetItemString(modules, JIT_TEST_MOD_NAME, module) != 0) {
      return true;
    }
    return false;
  }

  // Out param is a limitation of googletest.
  // See https://fburl.com/z3fzhl9p for more details.
  void CompileToHIR(
      const char* src,
      const char* func_name,
      std::unique_ptr<jit::hir::Function>& irfunc) {
    Ref<PyFunctionObject> func(compileAndGet(src, func_name));
    ASSERT_NE(func.get(), nullptr) << "failed creating function";

    irfunc = jit::hir::buildHIR(func);
    ASSERT_NE(irfunc, nullptr) << "failed constructing HIR";
  }

  void CompileToHIRStatic(
      const char* src,
      const char* func_name,
      std::unique_ptr<jit::hir::Function>& irfunc) {
    Ref<PyFunctionObject> func(compileStaticAndGet(src, func_name));
    ASSERT_NE(func.get(), nullptr) << "failed creating function";

    irfunc = jit::hir::buildHIR(func);
    ASSERT_NE(irfunc, nullptr) << "failed constructing HIR";
  }

  void TearDown() override {
    globals_.reset();
    int result = Py_FinalizeEx();
    ASSERT_EQ(result, 0) << "Failed finalizing the interpreter";
  }

 protected:
  bool compile_static_;

 private:
  Ref<> globals_;
};

class HIRTest : public RuntimeTest {
 public:
  enum Flags {
    kCompileStatic = 1 << 0,
    kUseProfileData = 1 << 1,
  };

  HIRTest(
      bool src_is_hir,
      const std::string& src,
      const std::string& expected_hir,
      Flags flags)
      : RuntimeTest(flags & kCompileStatic),
        src_is_hir_(src_is_hir),
        src_(src),
        expected_hir_(expected_hir),
        use_profile_data_(flags & kUseProfileData) {
    JIT_CHECK(
        !src_is_hir || !use_profile_data_,
        "Profile data tests can't have HIR input");
  }

  void setPasses(std::vector<std::unique_ptr<jit::hir::Pass>> passes) {
    passes_ = std::move(passes);
  }

  void TestBody() override;

 private:
  std::vector<std::unique_ptr<jit::hir::Pass>> passes_;
  bool src_is_hir_;
  std::string src_;
  std::string expected_hir_;
  bool use_profile_data_;
};

inline HIRTest::Flags operator|(HIRTest::Flags a, HIRTest::Flags b) {
  return static_cast<HIRTest::Flags>(static_cast<int>(a) | static_cast<int>(b));
}

class HIRJSONTest : public RuntimeTest {
 public:
  HIRJSONTest(const std::string& src, const std::string& expected_json)
      : RuntimeTest(), src_(src), expected_json_(expected_json) {}

  void TestBody() override;

 private:
  std::string src_;
  std::string expected_json_;
};
