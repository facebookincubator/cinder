// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef STRICTM_TEST_H
#define STRICTM_TEST_H

/** Include this file instead of including the gtest headers to
avoid macro naming conflict between gtest and python ast.h
*/
#include <memory>
#include <vector>

#include "StrictModules/py_headers.h"

#include "StrictModules/Compiler/abstract_module_loader.h"
#include "StrictModules/Compiler/module_info.h"
#include "StrictModules/analyzer.h"
#include "StrictModules/error_sink.h"
#include "StrictModules/exceptions.h"
#include "StrictModules/parser_util.h"

// #ifdef Compare
// #undef Compare
// #endif
// #ifdef Set
// #undef Set
// #endif

#include "StrictModules/Tests/test_util.h"
#include "gtest/gtest.h"

class PythonTest : public ::testing::Test {
 public:
  void SetUp() override {
    Py_Initialize();
    ASSERT_TRUE(Py_IsInitialized());
  }

  void TearDown() override {
    int result = Py_FinalizeEx();
    ASSERT_EQ(result, 0) << "Failed finalizing the interpreter";
  }
};

class AnalyzerTest : public PythonTest {
 public:
  bool analyzeFile(const char* name) {
    PyArena* arena = PyArena_New();
    auto result = strictmod::readFromFile(name, arena);
    EXPECT_NE(result, std::nullopt);
    auto errors = std::make_unique<strictmod::ErrorSink>();
    strictmod::Analyzer analyzer(
        result.value().ast,
        nullptr,
        strictmod::Symtable(std::move(result.value().symbols)),
        errors.get(),
        name,
        "<module>",
        nullptr);
    bool success;
    try {
      analyzer.analyze();
    } catch (strictmod::StrictModuleException& e) {
    }
    success = !errors->hasError();
    if (arena != nullptr) {
      PyArena_Free(arena);
    }
    return success;
  }

  bool analyzeSource(const char* source, const char* filename) {
    if (filename == nullptr) {
      filename = "<string>";
    }
    PyArena* arena = PyArena_New();
    auto result = strictmod::readFromSource(source, filename, arena);
    EXPECT_NE(result, std::nullopt);
    auto errors = std::make_unique<strictmod::ErrorSink>();
    strictmod::Analyzer analyzer(
        result.value().ast,
        nullptr,
        strictmod::Symtable(std::move(result.value().symbols)),
        errors.get(),
        filename,
        "<module>",
        nullptr);
    bool success;
    try {
      analyzer.analyze();
    } catch (strictmod::StrictModuleException& e) {
    }
    success = !errors->hasError();
    if (arena != nullptr) {
      PyArena_Free(arena);
    }
    return success;
  }

  bool analyzeSource(const char* source) {
    return analyzeSource(source, nullptr);
  }
};

class ModuleLoaderTest : public PythonTest {
 public:
  std::unique_ptr<strictmod::compiler::ModuleLoader> getLoader(
      const char* importPath,
      strictmod::compiler::ModuleLoader::ForceStrictFunc func) {
    return getLoader(importPath, func, [] {
      return std::make_unique<strictmod::ErrorSink>();
    });
  }

  std::unique_ptr<strictmod::compiler::ModuleLoader> getLoader(
      const char* importPath) {
    if (importPath == nullptr) {
      importPath = "StrictModules/Tests/python_tests";
    }
    std::vector<std::string> importPaths;
    importPaths.emplace_back(importPath);
    return std::make_unique<strictmod::compiler::ModuleLoader>(
        std::move(importPaths));
  }

  std::unique_ptr<strictmod::compiler::ModuleLoader> getLoader(
      const char* importPath,
      strictmod::compiler::ModuleLoader::ForceStrictFunc func,
      strictmod::compiler::ModuleLoader::ErrorSinkFactory factory) {
    if (importPath == nullptr) {
      importPath = "StrictModules/Tests/python_tests";
    }
    std::vector<std::string> importPaths;
    importPaths.emplace_back(importPath);
    return std::make_unique<strictmod::compiler::ModuleLoader>(
        std::move(importPaths), func, factory);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadFile(
      const char* name,
      const char* importPath) {
    auto loader = getLoader(importPath);
    loader->loadModule(name);
    return loader->passModule(name);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadFile(
      const char* name) {
    return loadFile(name, nullptr);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadSingleFile(
      const char* name,
      const char* importPath) {
    auto loader = getLoader(importPath);
    loader->loadSingleModule(name);
    return loader->passModule(name);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadSingleFile(
      const char* name) {
    return loadSingleFile(name, nullptr);
  }

  std::unique_ptr<strictmod::compiler::ModuleInfo> findModule(
      const char* name,
      const char* importPath) {
    auto loader = getLoader(importPath);
    return loader->findModule(
        name, strictmod::compiler::FileSuffixKind::kPythonFile);
  }

  std::unique_ptr<strictmod::compiler::ModuleInfo> findModule(
      const char* name) {
    return findModule(name, nullptr);
  }
};

class ModuleLoaderComparisonTest : public ModuleLoaderTest {
 public:
  ModuleLoaderComparisonTest(
      std::string src,
      std::vector<std::string> vars,
      std::vector<std::string> exceptions)
      : source_(src),
        varNames_(std::move(vars)),
        exceptions_(std::move(exceptions)) {}

  void TestBody() override {
    auto loader = getLoader(
        nullptr,
        [](const std::string&, const std::string&) { return true; },
        [] { return std::make_unique<strictmod::CollectingErrorSink>(); });
    strictmod::compiler::AnalyzedModule* mod =
        loader->loadModuleFromSource(source_, "<string>", "<string>", {});
    // analysis side
    ASSERT_NE(mod, nullptr);
    auto modValue = mod->getModuleValue();
    ASSERT_NE(modValue.get(), nullptr);
    // python side
    PyObject* global = PyDict_New();
    ASSERT_NE(global, nullptr);
    auto v = PyRun_String(source_.c_str(), Py_file_input, global, global);
    if (varNames_.empty()) {
      // Only care about errors. In this case we allow python code
      // to throw
      PyErr_Clear();
    } else {
      ASSERT_NE(v, nullptr);
    }

    for (auto vName : varNames_) {
      auto value = modValue->getAttr(vName);
      ASSERT_NE(value, nullptr);
      auto pyValue = PyDict_GetItemString(global, vName.c_str());
      ASSERT_NE(pyValue, nullptr);
      auto strictPyValue = value->getPyObject();
      ASSERT_NE(strictPyValue, nullptr);
      EXPECT_TRUE(
          PyObject_RichCompareBool(pyValue, strictPyValue.get(), Py_EQ)) << value->getDisplayName();
    }
    Py_DECREF(global);

    auto& errors = mod->getErrorSink().getErrors();
    ASSERT_EQ(errors.size(), exceptions_.size());
    for (unsigned i = 0; i < errors.size(); ++i) {
      EXPECT_EQ(errors[i]->testString(), exceptions_[i]);
    }
  }

 private:
  std::string source_;
  std::vector<std::string> varNames_;
  std::vector<std::string> exceptions_;
};

#endif // STRICTM_TEST_H
