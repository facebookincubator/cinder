// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "strict_module_checker_interface.h"

#include <string>
#include <vector>

#include "StrictModules/Compiler/abstract_module_loader.h"

StrictModuleChecker* StrictModuleChecker_New() {
  auto checker = new strictmod::compiler::ModuleLoader({}, {});
  return reinterpret_cast<StrictModuleChecker*>(checker);
}

int StrictModuleChecker_SetImportPaths(
    StrictModuleChecker* checker,
    const char* import_paths[],
    int length) {
  std::vector<std::string> importPaths(length);
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

void StrictModuleChecker_Free(StrictModuleChecker* checker) {
  delete reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
}

int StrictModuleChecker_Check(
    StrictModuleChecker* checker,
    PyObject* module_name) {
  if (!PyUnicode_Check(module_name)) {
    return -1;
  }
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  const char* modName = PyUnicode_AsUTF8(module_name);
  auto analyzedModule = loader->loadModule(modName);
  return analyzedModule ? analyzedModule->getError() : 1;
}
