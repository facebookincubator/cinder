// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "strict_module_checker_interface.h"

#include <string>
#include <utility>
#include <vector>

#include "StrictModules/Compiler/abstract_module_loader.h"

void ErrorInfo_Clean(ErrorInfo* info) {
  Py_XDECREF(info->filename);
  Py_XDECREF(info->msg);
}

StrictModuleChecker* StrictModuleChecker_New() {
  auto checker = new strictmod::compiler::ModuleLoader({}, {});
  return reinterpret_cast<StrictModuleChecker*>(checker);
}

int StrictModuleChecker_SetImportPaths(
    StrictModuleChecker* checker,
    const char* import_paths[],
    int length) {
  std::vector<std::string> importPaths;
  importPaths.reserve(length);
  for (int _i = 0; _i < length; _i++) {
    importPaths.emplace_back(import_paths[_i]);
  }
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->setImportPath(importPaths);
  return success ? 0 : -1;
}

int StrictModuleChecker_SetStubImportPath(
    StrictModuleChecker* checker,
    const char* stub_import_path) {
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->setStubImportPath(std::string(stub_import_path));
  return success ? 0 : -1;
}

int StrictModuleChecker_SetAllowListPrefix(
    StrictModuleChecker* checker,
    const char* allowList[],
    int length) {
  std::vector<std::string> allowListVec;
  allowListVec.reserve(length);
  for (int _i = 0; _i < length; _i++) {
    allowListVec.emplace_back(allowList[_i]);
  }
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->setAllowListPrefix(allowListVec);
  return success ? 0 : -1;
}

int StrictModuleChecker_SetAllowListExact(
    StrictModuleChecker* checker,
    const char* allowList[],
    int length) {
  std::vector<std::string> allowListVec;
  allowListVec.reserve(length);
  for (int _i = 0; _i < length; _i++) {
    allowListVec.emplace_back(allowList[_i]);
  }
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->setAllowListExact(allowListVec);
  return success ? 0 : -1;
}

int StrictModuleChecker_LoadStrictModuleBuiltins(StrictModuleChecker* checker) {
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->loadStrictModuleModule();
  return success ? 0 : -1;
}

void StrictModuleChecker_Free(StrictModuleChecker* checker) {
  delete reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
}

StrictAnalyzedModule* StrictModuleChecker_Check(
    StrictModuleChecker* checker,
    PyObject* module_name,
    int* out_error_count,
    int* is_strict_out) {
  if (!PyUnicode_Check(module_name)) {
    return NULL;
  }
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  const char* modName = PyUnicode_AsUTF8(module_name);
  auto analyzedModule = loader->loadModule(modName);
  *out_error_count = analyzedModule == nullptr
      ? 0
      : analyzedModule->getErrorSink().getErrorCount();
  bool is_strict =
      analyzedModule != nullptr && analyzedModule->getModuleValue() != nullptr;
  *is_strict_out = is_strict;
  return reinterpret_cast<StrictAnalyzedModule*>(analyzedModule);
}

int StrictModuleChecker_GetErrors(
    StrictAnalyzedModule* mod,
    ErrorInfo errors_out[],
    size_t length) {
  strictmod::compiler::AnalyzedModule* loader =
      reinterpret_cast<strictmod::compiler::AnalyzedModule*>(mod);
  const auto& errors = loader->getErrorSink().getErrors();
  if (errors.size() != length) {
    return -1;
  }
  for (size_t i = 0; i < length; ++i) {
    const auto& err = errors[i];
    std::string msg = err->displayString(false);
    const std::string& filename = err->getFilename();

    PyObject* py_msg = PyUnicode_FromString(msg.c_str());
    if (!py_msg) {
      return -1;
    }
    PyObject* py_file = PyUnicode_FromString(filename.c_str());
    if (!py_file) {
      Py_XDECREF(py_msg);
      return -1;
    }
    errors_out[i] = {py_msg, py_file, err->getLineno(), err->getCol()};
  }
  return 0;
}

int StrictModuleChecker_SetForceStrict(
    StrictModuleChecker* checker,
    PyObject* force_strict) {
  if (!PyBool_Check(force_strict)) {
    return -1;
  }
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool forceStrictBool = force_strict == Py_True;
  loader->setForceStrict(forceStrictBool);
  return 0;
}

int StrictModuleChecker_GetAnalyzedModuleCount(StrictModuleChecker* checker) {
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  return loader->getAnalyzedModuleCount();
}
