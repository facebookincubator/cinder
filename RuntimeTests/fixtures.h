// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef RUNTIME_TEST_FIXTURES_H
#define RUNTIME_TEST_FIXTURES_H

#include "Jit/hir/builder.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/parser.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/ref.h"
#include "gtest/gtest.h"
#include "testutil.h"

#include "Python.h"
#include "internal/pycore_pystate.h"

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
    auto compile_result =
        Ref<>::steal(PyRun_String(src, Py_file_input, globals_, globals_));
    return compile_result != nullptr;
  }

  bool runStaticCode(const char* src) {
    auto compiler = Ref<>::steal(PyImport_ImportModule("compiler.static"));
    if (compiler == nullptr) {
      return false;
    }
    auto exec_static =
        Ref<>::steal(PyObject_GetAttrString(compiler, "exec_static"));
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
    return Ref<>(obj);
  }

  Ref<> MakeGlobals() {
    auto module = Ref<>::steal(PyModule_New(JIT_TEST_MOD_NAME));
    if (module == nullptr) {
      return module;
    }
    Ref<> globals(PyModule_GetDict(module));

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

    jit::hir::HIRBuilder builder;
    irfunc = builder.BuildHIR(func);
    ASSERT_NE(irfunc, nullptr) << "failed constructing HIR";
  }

  void CompileToHIRStatic(
      const char* src,
      const char* func_name,
      std::unique_ptr<jit::hir::Function>& irfunc) {
    Ref<PyFunctionObject> func(compileStaticAndGet(src, func_name));
    ASSERT_NE(func.get(), nullptr) << "failed creating function";

    jit::hir::HIRBuilder builder;
    irfunc = builder.BuildHIR(func);
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
  HIRTest(
      std::vector<std::unique_ptr<jit::hir::Pass>>&& passes,
      bool src_is_hir,
      const std::string& src,
      const std::string& expected_hir,
      bool compile_static = false)
      : RuntimeTest(compile_static),
        passes_(std::move(passes)),
        src_is_hir_(src_is_hir),
        src_(src),
        expected_hir_(expected_hir) {}
  HIRTest(
      bool src_is_hir,
      const std::string& src,
      const std::string& expected_hir,
      bool compile_static = false)
      : RuntimeTest(compile_static),
        src_is_hir_(src_is_hir),
        src_(src),
        expected_hir_(expected_hir) {}

  void TestBody() override {
    using namespace jit::hir;
    std::unique_ptr<Function> irfunc;
    if (src_is_hir_) {
      irfunc = HIRParser{}.ParseHIR(src_.c_str());
      ASSERT_FALSE(passes_.empty())
          << "HIR tests don't make sense without a pass to test";
      ASSERT_NE(irfunc, nullptr);
      ASSERT_TRUE(checkFunc(*irfunc, std::cout));
      reflowTypes(*irfunc);
    } else if (compile_static_) {
      ASSERT_NO_FATAL_FAILURE(CompileToHIRStatic(src_.c_str(), "test", irfunc));
    } else {
      ASSERT_NO_FATAL_FAILURE(CompileToHIR(src_.c_str(), "test", irfunc));
    }

    if (!passes_.empty()) {
      if (!src_is_hir_) {
        SSAify{}.Run(*irfunc);
        // Perform some straightforward cleanup on Python inputs to make the
        // output more reasonable. This implies that tests for the passes used
        // here are most useful as HIR-only tests.
        Simplify{}.Run(*irfunc);
        CopyPropagation{}.Run(*irfunc);
        PhiElimination{}.Run(*irfunc);
      }
      for (auto& pass : passes_) {
        pass->Run(*irfunc);
      }
      ASSERT_TRUE(checkFunc(*irfunc, std::cout));
    }
    HIRPrinter printer;
    auto hir = printer.ToString(*irfunc.get());
    EXPECT_EQ(hir, expected_hir_);
  }

 private:
  std::vector<std::unique_ptr<jit::hir::Pass>> passes_;
  bool src_is_hir_;
  std::string src_;
  std::string expected_hir_;
};

#endif
