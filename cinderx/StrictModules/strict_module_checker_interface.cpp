// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "strict_module_checker_interface.h"

#include "cinderx/StrictModules/Compiler/abstract_module_loader.h"
#include "cinderx/StrictModules/Compiler/analyzed_module.h"
#include "cinderx/StrictModules/pycore_dependencies.h"

#include <string>
#include <utility>
#include <vector>

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

int StrictModuleChecker_SetAllowListRegex(
    StrictModuleChecker* checker,
    const char* allowList[],
    int length) {
  std::vector<std::string> allowListVec;
  allowListVec.reserve(length);
  for (int _i = 0; _i < length; _i++) {
    allowListVec.emplace_back(allowList[_i]);
  }
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->setAllowListRegex(allowListVec);
  return success ? 0 : -1;
}

int StrictModuleChecker_LoadStrictModuleBuiltins(StrictModuleChecker* checker) {
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->loadStrictModuleModule();
  return success ? 0 : -1;
}

int StrictModuleChecker_EnableVerboseLogging(StrictModuleChecker* checker) {
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->enableVerboseLogging();
  return success ? 0 : -1;
}

int StrictModuleChecker_DisableAnalysis(StrictModuleChecker* checker) {
  auto loader = reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  bool success = loader->disableAnalysis();
  return success ? 0 : -1;
}

void StrictModuleChecker_Free(StrictModuleChecker* checker) {
  delete reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
}

static bool getAnalyzedResult(
    strictmod::compiler::AnalyzedModule* analyzedModule,
    int* out_error_count) {
  *out_error_count = analyzedModule == nullptr
      ? 0
      : analyzedModule->getErrorSink().getErrorCount();
  bool is_strict =
      analyzedModule != nullptr && analyzedModule->getModuleValue() != nullptr;
  return is_strict;
}

StrictAnalyzedModule* StrictModuleChecker_Check(
    StrictModuleChecker* checker,
    PyObject* module_name,
    int* out_error_count,
    int* is_strict_out) {
  if (!PyUnicode_Check(module_name)) {
    return nullptr;
  }
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  const char* modName = PyUnicode_AsUTF8(module_name);
  loader->log("Checking module: %s", modName);
  auto analyzedModule = loader->loadModule(modName);
  *is_strict_out = getAnalyzedResult(analyzedModule, out_error_count);
  return reinterpret_cast<StrictAnalyzedModule*>(analyzedModule);
}

StrictAnalyzedModule* StrictModuleChecker_CheckSource(
    StrictModuleChecker* checker,
    const char* source,
    PyObject* module_name,
    PyObject* file_name,
    const char* submodule_search_locations[],
    int search_locations_size,
    int* out_error_count,
    int* is_strict_out) {
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  const char* modName = PyUnicode_AsUTF8(module_name);
  const char* fileName = PyUnicode_AsUTF8(file_name);
  std::vector<std::string> searchLocations;
  searchLocations.reserve(search_locations_size);
  for (int i = 0; i < search_locations_size; i++) {
    searchLocations.emplace_back(submodule_search_locations[i]);
  }

  auto analyzedModule =
      loader->loadModuleFromSource(source, modName, fileName, searchLocations);
  *is_strict_out = getAnalyzedResult(analyzedModule, out_error_count);
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

int StrictModuleChecker_SetForceStrictByName(
    StrictModuleChecker* checker,
    const char* forced_module_name) {
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  std::string modName(forced_module_name);
  loader->setForceStrictFunc(
      [forced_module_name](const std::string& modName, const std::string&) {
        return modName == forced_module_name;
      });
  return 0;
}

int StrictModuleChecker_GetAnalyzedModuleCount(StrictModuleChecker* checker) {
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  return loader->getAnalyzedModuleCount();
}

int StrictModuleChecker_DeleteModule(
    StrictModuleChecker* checker,
    const char* module_name) {
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  std::string name(module_name);
  loader->deleteModule(name);
  return 0;
}

PyArena* StrictModuleChecker_GetArena(StrictModuleChecker* checker) {
  strictmod::compiler::ModuleLoader* loader =
      reinterpret_cast<strictmod::compiler::ModuleLoader*>(checker);
  return loader->getArena();
}

PyObject* StrictAnalyzedModule_GetAST(
    StrictAnalyzedModule* mod,
    PyArena* arena) {
  strictmod::compiler::AnalyzedModule* module =
      reinterpret_cast<strictmod::compiler::AnalyzedModule*>(mod);
  Ref<> result = module->getPyAst(arena);
  return result.release();
}

PyObject* StrictAnalyzedModule_GetSymtable(StrictAnalyzedModule* mod) {
  strictmod::compiler::AnalyzedModule* module =
      reinterpret_cast<strictmod::compiler::AnalyzedModule*>(mod);
  auto symtable = module->getModuleInfo().getSymtable();
  if (symtable == nullptr) {
    return nullptr;
  }
  PyObject* symtableTop = (PyObject*)symtable->st_top;
  Py_INCREF(symtableTop);
  return symtableTop;
}

// retrieve filename
PyObject* StrictAnalyzedModule_GetFilename(StrictAnalyzedModule* mod) {
  strictmod::compiler::AnalyzedModule* module =
      reinterpret_cast<strictmod::compiler::AnalyzedModule*>(mod);
  const std::string& fileStr = module->getModuleInfo().getFilename();
  PyObject* filename = PyUnicode_FromString(fileStr.c_str());
  return filename;
}

// retrieve modulekind as int
int StrictAnalyzedModule_GetModuleKind(StrictAnalyzedModule* mod) {
  strictmod::compiler::AnalyzedModule* module =
      reinterpret_cast<strictmod::compiler::AnalyzedModule*>(mod);
  return module->getModKindAsInt();
}

// retrieve stubkind as int
int StrictAnalyzedModule_GetStubKind(StrictAnalyzedModule* mod) {
  strictmod::compiler::AnalyzedModule* module =
      reinterpret_cast<strictmod::compiler::AnalyzedModule*>(mod);
  return module->getStubKindAsInt();
}
