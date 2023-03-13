// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

/** Include this file instead of including the gtest headers to
avoid macro naming conflict between gtest and python ast.h
*/
#include "StrictModules/Compiler/abstract_module_loader.h"
#include "StrictModules/Compiler/module_info.h"
#include "StrictModules/Tests/test_util.h"
#include "StrictModules/analyzer.h"
#include "StrictModules/error_sink.h"
#include "StrictModules/exceptions.h"
#include "StrictModules/parser_util.h"
#include "StrictModules/py_headers.h"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

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
    PyArena* arena = _PyArena_New();
    auto result = strictmod::readFromFile(name, arena, {});
    EXPECT_NE(result, std::nullopt);
    auto errors = std::make_unique<strictmod::ErrorSink>();
    strictmod::Analyzer analyzer(
        result.value().ast,
        nullptr,
        strictmod::Symtable(std::move(result.value().symbols)),
        errors.get(),
        name,
        "",
        "<module>",
        nullptr);
    bool success;
    try {
      analyzer.analyze();
    } catch (...) {
    }
    success = !errors->hasError();
    if (arena != nullptr) {
      _PyArena_Free(arena);
    }
    return success;
  }

  bool analyzeSource(const char* source, const char* filename) {
    if (filename == nullptr) {
      filename = "<string>";
    }
    PyArena* arena = _PyArena_New();
    auto result =
        strictmod::readFromSource(source, filename, Py_file_input, arena);
    EXPECT_NE(result, std::nullopt);
    auto errors = std::make_unique<strictmod::ErrorSink>();
    auto loader = strictmod::compiler::ModuleLoader();
    strictmod::Analyzer analyzer(
        result.value().ast,
        &loader,
        strictmod::Symtable(std::move(result.value().symbols)),
        errors.get(),
        filename,
        "",
        "<module>",
        nullptr);
    bool success;
    try {
      analyzer.analyze();
    } catch (...) {
    }
    success = !errors->hasError();
    if (arena != nullptr) {
      _PyArena_Free(arena);
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
      const char* stubPath,
      strictmod::compiler::ModuleLoader::ForceStrictFunc func) {
    return getLoader(importPath, stubPath, func, [] {
      return std::make_unique<strictmod::ErrorSink>();
    });
  }

  std::unique_ptr<strictmod::compiler::ModuleLoader> getLoader(
      const char* importPath,
      const char* stubPath) {
    if (importPath == nullptr) {
      importPath = "StrictModules/Tests/python_tests";
    }
    if (stubPath == nullptr) {
      stubPath = "StrictModules/Tests/python_tests/stubs";
    }
    std::vector<std::string> importPaths;
    importPaths.emplace_back(importPath);
    std::vector<std::string> stubImportPaths;
    stubImportPaths.emplace_back(stubPath);
    auto loader = std::make_unique<strictmod::compiler::ModuleLoader>(
        std::move(importPaths), std::move(stubImportPaths));
    loader->loadStrictModuleModule();
    return loader;
  }

  std::unique_ptr<strictmod::compiler::ModuleLoader> getLoader(
      const char* importPath,
      const char* stubPath,
      strictmod::compiler::ModuleLoader::ForceStrictFunc func,
      strictmod::compiler::ModuleLoader::ErrorSinkFactory factory) {
    if (importPath == nullptr) {
      importPath = "StrictModules/Tests/python_tests";
    }
    if (stubPath == nullptr) {
      stubPath = "StrictModules/Tests/python_tests/stubs";
    }
    std::vector<std::string> importPaths;
    importPaths.emplace_back(importPath);
    std::vector<std::string> stubImportPaths;
    stubImportPaths.emplace_back(stubPath);
    auto loader = std::make_unique<strictmod::compiler::ModuleLoader>(
        std::move(importPaths),
        std::move(stubImportPaths),
        strictmod::compiler::ModuleLoader::AllowListType{},
        func,
        factory);
    loader->loadStrictModuleModule();
    return loader;
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule>
  loadFile(const char* name, const char* importPath, const char* stubPath) {
    auto loader = getLoader(importPath, stubPath);
    loader->loadModule(name);
    return loader->passModule(name);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadFile(
      const char* name) {
    return loadFile(name, nullptr, nullptr);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadSingleFile(
      const char* name,
      const char* importPath,
      const char* stubPath) {
    auto loader = getLoader(importPath, stubPath);
    loader->loadSingleModule(name);
    return loader->passModule(name);
  }

  std::unique_ptr<strictmod::compiler::AnalyzedModule> loadSingleFile(
      const char* name) {
    return loadSingleFile(name, nullptr, nullptr);
  }

  std::unique_ptr<strictmod::compiler::ModuleInfo> findModule(
      const char* name,
      const char* importPath) {
    auto loader = getLoader(importPath, nullptr);
    return loader->findModule(
        name, strictmod::compiler::FileSuffixKind::kPythonFile);
  }

  std::unique_ptr<strictmod::compiler::ModuleInfo> findModule(
      const char* name) {
    return findModule(name, nullptr);
  }

  Ref<> getPreprocessedASTDump(
      const std::string& source,
      const std::string& filename,
      const std::string& modName) {
    auto loader = getLoader("", "");
    loader->loadStrictModuleModule();

    auto mod = loader->loadModuleFromSource(source, modName, filename, {});
    PyArena* arena = loader->getArena();
    auto astPreprocessed = mod->getPyAst(true, arena);

    Ref<> astMod = Ref<>::steal(PyImport_ImportModule("ast"));
    EXPECT_TRUE(astMod != nullptr);
    Ref<> astDumpFunc = Ref<>::steal(PyObject_GetAttrString(astMod, "dump"));
    EXPECT_NE(astDumpFunc, nullptr);

    Ref<> astPreprocessedStrObj = Ref<>::steal(PyObject_CallFunctionObjArgs(
        astDumpFunc, astPreprocessed.get(), nullptr));

    EXPECT_NE(astPreprocessedStrObj, nullptr);
    return astPreprocessedStrObj;
  }
};

class ModuleLoaderComparisonTest : public ModuleLoaderTest {
 public:
  ModuleLoaderComparisonTest(
      std::string src,
      std::vector<VarMatcher> vars,
      std::vector<std::string> exceptions)
      : source_(src),
        vars_(std::move(vars)),
        exceptions_(std::move(exceptions)) {}

  void TestBody() override {
    auto errorSink = std::make_shared<strictmod::CollectingErrorSink>();
    auto loader = getLoader(
        nullptr,
        "Lib/compiler/strict/stubs",
        [](const std::string&, const std::string&) { return true; },
        [errorSink] { return errorSink; });
    loader->setImportPath(
        {"StrictModules/Tests/comparison_tests/imports", "Lib"});
    loader->loadStrictModuleModule();
    const char* modname = "<string>";
    strictmod::compiler::AnalyzedModule* mod =
        loader->loadModuleFromSource(source_, modname, modname, {});
    // analysis side
    ASSERT_NE(mod, nullptr);
    auto modValue = mod->getModuleValue();
    ASSERT_NE(modValue.get(), nullptr);

    std::wstring path{Py_GetPath()};
    path.append(L":StrictModules/Tests/comparison_tests/imports");
    PySys_SetPath(path.c_str());

    PyObject* code = Py_CompileString(source_.c_str(), modname, Py_file_input);
    auto pyMod = PyImport_ExecCodeModule(modname, code);
    PyObject* global = nullptr;
    if (vars_.empty()) {
      // Only care about errors. In this case we allow python code
      // to throw
      PyErr_Clear();
    } else {
      ASSERT_NE(pyMod, nullptr);
      global = PyObject_GenericGetDict(pyMod, nullptr);
      ASSERT_NE(global, nullptr);
    }

    for (auto [vName, vType] : vars_) {
      auto value = modValue->getAttr(vName);
      ASSERT_NE(value, nullptr);
      auto pyValue = PyDict_GetItemString(global, vName.c_str());
      ASSERT_NE(pyValue, nullptr);
      auto strictPyValue = value->getPyObject();
      ASSERT_NE(strictPyValue, nullptr);
      auto repr = PyObject_Repr(pyValue);
      EXPECT_TRUE(PyObject_RichCompareBool(pyValue, strictPyValue.get(), Py_EQ))
          << value->getDisplayName() << " : " << PyUnicode_AsUTF8(repr);
      if (vType) {
        EXPECT_EQ(vType.value(), getType(value)->getDisplayName());
      }
      Py_XDECREF(repr);
    }

    Py_XDECREF(code);
    Py_XDECREF(pyMod);
    Py_XDECREF(global);

    auto& errors = mod->getErrorSink().getErrors();
    ASSERT_EQ(errors.size(), exceptions_.size());
    for (unsigned i = 0; i < errors.size(); ++i) {
      EXPECT_EQ(errors[i]->testString(), exceptions_[i]);
    }
  }

 private:
  std::string source_;
  std::vector<VarMatcher> vars_;
  std::vector<std::string> exceptions_;

  static std::shared_ptr<strictmod::StrictType> getType(
      const std::shared_ptr<strictmod::BaseStrictObject>& object) {
    if (auto type = std::dynamic_pointer_cast<strictmod::StrictType>(object)) {
      return type;
    }
    return object->getType();
  }
};
