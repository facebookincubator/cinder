// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_MODULE_LOADER_H__
#define __STRICTM_MODULE_LOADER_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "StrictModules/Compiler/analyzed_module.h"
#include "StrictModules/Compiler/module_info.h"
#include "StrictModules/analyzer.h"
#include "StrictModules/error_sink.h"
namespace strictmod::compiler {
enum class FileSuffixKind { kPythonFile, kStrictStubFile, kTypingStubFile };

class ModuleLoader {
 public:
  typedef std::function<bool(const std::string&, const std::string&)>
      ForceStrictFunc;
  typedef std::function<std::unique_ptr<BaseErrorSink>()> ErrorSinkFactory;

  ModuleLoader(std::vector<std::string> importPath)
      : ModuleLoader(std::move(importPath), std::nullopt, [] {
          return std::make_unique<ErrorSink>();
        }) {}

  ModuleLoader(std::vector<std::string> importPath, ForceStrictFunc forceStrict)
      : ModuleLoader(std::move(importPath), forceStrict, [] {
          return std::make_unique<ErrorSink>();
        }) {}

  ModuleLoader(
      std::vector<std::string> importPath,
      std::optional<ForceStrictFunc> forceStrict,
      ErrorSinkFactory factory)
      : importPath_(std::move(importPath)),
        modules_(),
        forceStrict_(forceStrict),
        errorSinkFactory_(factory) {
    arena_ = PyArena_New();
    if (arena_ == nullptr) {
      throw std::runtime_error(kArenaNewErrorMsg);
    }
  }

  ~ModuleLoader() {
    // free all scopes owned by analyzed modules
    for (auto& am : modules_) {
      // since passModule could be used in tests, the values
      // of modules could be nullptr
      if (am.second) {
        am.second->cleanModuleContent();
      }
    }
    PyArena_Free(arena_);
  }

  /**
  pass ownership to caller of an already analyzed module.
  Return nullptr if module is not loaded
  */
  std::unique_ptr<AnalyzedModule> passModule(const std::string& modName) {
    return std::move(modules_[modName]);
  }

  /**
  Load a module named `modName`.
  The returned pointer maybe null, which indicates that the module is
  not found.
  Note that this is different from the value of the analyzed module
  being nullptr, indicating that the analysis failed/module is not strict
  */
  AnalyzedModule* loadModule(const char* modName);
  AnalyzedModule* loadModule(const std::string& modName);

  AnalyzedModule* loadModuleFromSource(
      const std::string& source,
      const std::string& name,
      const std::string& filename,
      std::vector<std::string> searchLocations);

  std::unique_ptr<ModuleInfo> findModule(
      const std::string& modName,
      const std::vector<std::string>& searchLocations,
      FileSuffixKind suffixKind);
  std::unique_ptr<ModuleInfo> findModule(
      const std::string& modName,
      FileSuffixKind suffixKind);

  AnalyzedModule* loadSingleModule(const std::string& modName);

  bool setImportPath(std::vector<std::string> importPath);

 private:
  static const std::string kArenaNewErrorMsg;
  std::vector<std::string> importPath_;
  PyArena* arena_;
  // the loader owns all analyzed module produced during the analysis
  std::unordered_map<std::string, std::unique_ptr<AnalyzedModule>> modules_;
  std::optional<ForceStrictFunc> forceStrict_;
  ErrorSinkFactory errorSinkFactory_;

  AnalyzedModule* analyze(std::unique_ptr<ModuleInfo> modInfo);
};

} // namespace strictmod::compiler

#endif // !__STRICTM_MODULE_LOADER_H__
