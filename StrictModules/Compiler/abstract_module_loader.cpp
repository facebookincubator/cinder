// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Compiler/abstract_module_loader.h"

#include "StrictModules/Compiler/stub.h"

#include "StrictModules/Objects/objects.h"

#include "StrictModules/analyzer.h"
#include "StrictModules/parser_util.h"
#include "StrictModules/symbol_table.h"

#include <cstring>
#include <filesystem>

namespace strictmod::compiler {

using strictmod::objects::ModuleType;
using strictmod::objects::StrictModuleObject;

static const char* kFileSuffixNames[] = {".py", ".pys", ".pyi"};

const char* getFileSuffixKindName(FileSuffixKind kind) {
  return kFileSuffixNames[static_cast<int>(kind)];
}

const std::string ModuleLoader::kArenaNewErrorMsg =
    "failed to allocate memory in PyArena";

ModuleKind getModuleKindFromStmts(const asdl_seq* seq) {
  Py_ssize_t n = asdl_seq_LEN(seq);
  bool seenDocStr = false;
  for (int _i = 0; _i < n; _i++) {
    stmt_ty stmt = reinterpret_cast<stmt_ty>(asdl_seq_GET(seq, _i));
    switch (stmt->kind) {
      case Import_kind: {
        auto importNames = stmt->v.Import.names;
        if (asdl_seq_LEN(importNames) == 1) {
          alias_ty alias =
              reinterpret_cast<alias_ty>(asdl_seq_GET(importNames, 0));
          if (alias->asname) {
            return ModuleKind::kNonStrict;
          }
          const char* aliasNameStr = PyUnicode_AsUTF8(alias->name);
          const char* strictFlag = "__strict__";
          const char* staticFlag = "__static__";
          if (strncmp(aliasNameStr, staticFlag, strlen(staticFlag)) == 0) {
            return ModuleKind::kStatic;
          } else if (
              strncmp(aliasNameStr, strictFlag, strlen(strictFlag)) == 0) {
            return ModuleKind::kStrict;
          } else {
            return ModuleKind::kNonStrict;
          }
        }
      }
      case Expr_kind: {
        if (!seenDocStr) {
          auto expr = stmt->v.Expr.value;
          if (expr->kind == Constant_kind &&
              PyUnicode_Check(expr->v.Constant.value)) {
            seenDocStr = true;
            continue;
          }
        }
        return ModuleKind::kNonStrict;
      }
      case ImportFrom_kind: {
        auto importFromStmt = stmt->v.ImportFrom;
        if (importFromStmt.module == nullptr) {
          return ModuleKind::kNonStrict;
        }
        const char* modName = PyUnicode_AsUTF8(importFromStmt.module);
        const char* futureFlag = "__future__";
        // skip future imports
        if (strncmp(modName, futureFlag, strlen(futureFlag)) == 0) {
          continue;
        }
        // encountered an import, not strict
        return ModuleKind::kNonStrict;
      }
      default:
        return ModuleKind::kNonStrict;
    }
  }
  return ModuleKind::kNonStrict;
}

ModuleKind getModuleKind(const mod_ty ast) {
  switch (ast->kind) {
    case Module_kind:
      return getModuleKindFromStmts(ast->v.Module.body);
    case Interactive_kind:
    case Expression_kind:
    case FunctionType_kind:
    case Suite_kind:
    default:
      return ModuleKind::kNonStrict;
  }
  return ModuleKind::kNonStrict;
}

AnalyzedModule* ModuleLoader::loadModule(const char* modName) {
  return loadModule(std::string(modName));
}

AnalyzedModule* ModuleLoader::loadModule(const std::string& modName) {
  auto exist = modules_.find(modName);
  if (exist != modules_.end()) {
    return exist->second.get();
  }
  char delimiter = '.';
  auto end = modName.find(delimiter);
  while (end != std::string::npos) {
    auto mod = loadSingleModule(modName.substr(0, end));
    if (!mod) {
      return nullptr;
    }
    end = modName.find(delimiter, end + 1);
  }
  return loadSingleModule(modName);
}

void ModuleLoader::deleteModule(const std::string& modName) {
  // For shutdown cleanup reason, we cannot just delete this module.
  // Instead we move it to the deletedModules_ set
  auto exist = modules_.find(modName);
  if (exist != modules_.end()) {
    deletedModules_.emplace(std::move(exist->second));
    modules_.erase(exist);
  }
}

std::shared_ptr<StrictModuleObject> ModuleLoader::loadModuleValue(
    const char* modName) {
  return loadModuleValue(std::string(modName));
}
std::shared_ptr<StrictModuleObject> ModuleLoader::loadModuleValue(
    const std::string& modName) {
  AnalyzedModule* mod = loadModule(modName);
  if (mod) {
    return mod->getModuleValue();
  }
  return nullptr;
}

void ModuleLoader::recordLazyModule(const std::string& modName) {
  lazy_modules_.emplace(modName);
}

std::shared_ptr<StrictModuleObject> ModuleLoader::tryGetModuleValue(
    const std::string& modName) {
  auto exist = modules_.find(modName);
  if (exist != modules_.end() && exist->second) {
    return exist->second->getModuleValue();
  } else {
    auto it = lazy_modules_.find(modName);
    if (it != lazy_modules_.end()) {
      lazy_modules_.erase(it);
      return loadModuleValue(modName);
    }
  }
  return nullptr;
}

AnalyzedModule* ModuleLoader::loadSingleModule(const std::string& modName) {
  auto exist = modules_.find(modName);
  if (exist != modules_.end()) {
    return exist->second.get();
  }
  // look for pys (strict module specific) stub
  auto stubModInfo =
      findModule(modName, stubImportPath_, FileSuffixKind::kStrictStubFile);
  bool stubIsNamespacePackage = false;
  if (stubModInfo) {
    stubModInfo = getStubModuleInfo(std::move(stubModInfo), this);
    if (!stubModInfo) {
      return nullptr;
    }
    stubIsNamespacePackage =
        std::filesystem::is_directory(stubModInfo->getFilename());
  }

  // look for py source code
  auto modInfo = findModule(modName, FileSuffixKind::kPythonFile);
  if (stubModInfo && (!stubIsNamespacePackage || !modInfo)) {
    return analyze(std::move(stubModInfo));
  }
  if (modInfo) {
    return analyze(std::move(modInfo));
  }

  // look for typing stub
  auto typingModInfo = findModule(modName, FileSuffixKind::kTypingStubFile);
  if (typingModInfo) {
    typingModInfo->setFutureAnnotations(true);
    if (typingModInfo->getStubKind().isForcedStrict()) {
      return analyze(std::move(typingModInfo));
    }
    if (typingModInfo->getStubKind().isTyping() &&
        getModuleKind(typingModInfo->getAst()) == ModuleKind::kStatic) {
      // TODO: remove annotations
      return analyze(std::move(typingModInfo));
    }
  }
  return nullptr;
}

bool ModuleLoader::setImportPath(std::vector<std::string> importPath) {
  importPath_ = std::move(importPath);
  return true;
}

bool ModuleLoader::setStubImportPath(std::string importPath) {
  stubImportPath_ = {std::move(importPath)};
  return true;
}

bool ModuleLoader::setStubImportPath(std::vector<std::string> importPath) {
  stubImportPath_ = std::move(importPath);
  return true;
}

void ModuleLoader::setForceStrict(bool force) {
  forceStrict_ = [force](const std::string&, const std::string&) {
    return force;
  };
}

bool ModuleLoader::clearAllowList() {
  allowList_.clear();
  return true;
}

bool ModuleLoader::setAllowListPrefix(std::vector<std::string> allowList) {
  for (const std::string& mod : allowList) {
    allowList_.emplace_back(mod, AllowListKind::kPrefix);
  }
  return true;
}

bool ModuleLoader::setAllowListExact(std::vector<std::string> allowList) {
  for (const std::string& mod : allowList) {
    allowList_.emplace_back(mod, AllowListKind::kExact);
  }
  return true;
}

AnalyzedModule* ModuleLoader::loadModuleFromSource(
    const std::string& source,
    const std::string& name,
    const std::string& filename,
    std::vector<std::string> searchLocations) {
  return loadModuleFromSource(
      source.c_str(), name, filename, std::move(searchLocations));
}

AnalyzedModule* ModuleLoader::loadModuleFromSource(
    const char* source,
    const std::string& name,
    const std::string& filename,
    std::vector<std::string> searchLocations) {
  auto readResult = readFromSource(source, filename.c_str(), arena_);
  if (readResult) {
    AstAndSymbols& result = readResult.value();
    bool allowlisted = isAllowListed(name);
    auto stubKind = StubKind::getStubKind(filename, allowlisted);
    auto modinfo = std::make_unique<ModuleInfo>(
        std::move(name),
        std::move(filename),
        result.ast,
        result.futureAnnotations,
        std::move(result.symbols),
        stubKind,
        std::move(searchLocations));
    return analyze(std::move(modinfo));
  }
  return nullptr;
}

bool ModuleLoader::isForcedStrict(
    const std::string& modName,
    const std::string& fileName) {
  auto stubKind = StubKind::getStubKind(fileName, isAllowListed(modName));
  if (stubKind.isForcedStrict()) {
    return true;
  }
  return forceStrict_ && (*forceStrict_)(modName, fileName);
}

std::unique_ptr<ModuleInfo> ModuleLoader::findModule(
    const std::string& modName,
    const std::vector<std::string>& searchLocations,
    FileSuffixKind suffixKind) {
  // replace module separator '.' with file path separator
  size_t pos = 0;
  std::string modPathStr(modName);
  while ((pos = modPathStr.find('.', pos)) != std::string::npos) {
    modPathStr.replace(pos, 1, 1, std::filesystem::path::preferred_separator);
    pos += 1;
  }

  const char* suffix = getFileSuffixKindName(suffixKind);
  for (const std::string& importPath : searchLocations) {
    // case 1: .py file
    std::filesystem::path pyModPath =
        std::filesystem::path(importPath) / modPathStr;
    pyModPath += suffix;
    const char* modPathCstr = pyModPath.c_str();
    std::string filename = pyModPath.string();
    std::optional<AstAndSymbols> readResult;

    if (isForcedStrict(modName, filename)) {
      readResult = readFromFile(modPathCstr, arena_, {});
    } else {
      readResult = readFromFile(modPathCstr, arena_, kStrictFlags);
    }

    if (readResult) {
      AstAndSymbols& result = readResult.value();
      bool allowlisted = isAllowListed(modName);
      return std::make_unique<ModuleInfo>(
          std::move(modName),
          filename,
          result.ast,
          result.futureAnnotations,
          std::move(result.symbols),
          StubKind::getStubKind(filename, allowlisted));
    }

    // case 2: __init__.py file
    std::filesystem::path initModPath =
        std::filesystem::path(importPath) / modPathStr / "__init__";
    initModPath += suffix;
    const char* initPathCstr = initModPath.c_str();
    filename = initModPath.string();

    if (isForcedStrict(modName, filename)) {
      readResult = readFromFile(initPathCstr, arena_, {});
    } else {
      readResult = readFromFile(initPathCstr, arena_, kStrictFlags);
    }

    if (readResult) {
      AstAndSymbols& result = readResult.value();
      bool allowlisted = isAllowListed(modName);
      return std::make_unique<ModuleInfo>(
          std::move(modName),
          filename,
          result.ast,
          result.futureAnnotations,
          std::move(result.symbols),
          StubKind::getStubKind(filename, allowlisted),
          std::vector<std::string>{importPath});
    }

    // case 3: namespace package (path is a directory)
    std::filesystem::path nmPackagePath =
        std::filesystem::path(importPath) / modPathStr;
    if (std::filesystem::is_directory(nmPackagePath)) {
      readResult = readFromSource("", nmPackagePath.c_str(), arena_);
      if (readResult) {
        AstAndSymbols& result = readResult.value();
        std::string filename = nmPackagePath.string();
        bool allowlisted = isAllowListed(modName);
        return std::make_unique<ModuleInfo>(
            std::move(modName),
            filename,
            result.ast,
            result.futureAnnotations,
            std::move(result.symbols),
            StubKind::getStubKind(filename, allowlisted),
            std::vector<std::string>{importPath});
      }
    }
  }
  return nullptr;
}

std::unique_ptr<ModuleInfo> ModuleLoader::findModule(
    const std::string& modName,
    FileSuffixKind suffixKind) {
  return findModule(modName, importPath_, suffixKind);
}

std::unique_ptr<ModuleInfo> ModuleLoader::findModuleFromSource(
    const std::string& source,
    const std::string& modName,
    const std::string& filename) {
  auto readResult = readFromSource(source.c_str(), filename.c_str(), arena_);
  if (readResult) {
    AstAndSymbols& result = readResult.value();
    assert(result.ast != nullptr);
    assert(result.symbols != nullptr);
    auto modinfo = std::make_unique<ModuleInfo>(
        modName,
        filename,
        result.ast,
        result.futureAnnotations,
        std::move(result.symbols),
        StubKind::getStubKind(filename, false));
    return modinfo;
  }
  return nullptr;
}

AnalyzedModule* ModuleLoader::analyze(std::unique_ptr<ModuleInfo> modInfo) {
  const mod_ty ast = modInfo->getAst();

  // Following python semantics, publish the module before ast visits
  auto errorSink = errorSinkFactory_();
  BaseErrorSink* errorSinkBorrowed = errorSink.get();
  ModuleKind kind;
  if (ast != nullptr) {
    kind = getModuleKind(ast);
  } else {
    kind = ModuleKind::kNonStrict;
  }

  AnalyzedModule* analyzedModule =
      new AnalyzedModule(kind, std::move(errorSink), std::move(modInfo));
  const ModuleInfo& moduleInfo = analyzedModule->getModuleInfo();
  const std::string& name = moduleInfo.getModName();
  const std::string& filename = moduleInfo.getFilename();
  modules_[name] = std::unique_ptr<AnalyzedModule>(analyzedModule);
  lazy_modules_.erase(name);
  if (analyzedModule->isStrict() || isForcedStrict(name, filename)) {
    assert(ast != nullptr);
    // Run ast visits
    auto globalScope = std::shared_ptr(objects::getBuiltinsDict());
    // create module object. Analysis result will be the __dict__ of this object
    auto mod =
        StrictModuleObject::makeStrictModule(ModuleType(), name, globalScope);

    // set __name__ and __path__
    mod->setAttr(
        "__name__",
        std::make_shared<objects::StrictString>(objects::StrType(), mod, name));
    const auto& subModuleLocs = moduleInfo.getSubmoduleSearchLocations();
    if (!subModuleLocs.empty()) {
      std::vector<std::shared_ptr<objects::BaseStrictObject>> pathVec;
      pathVec.reserve(subModuleLocs.size());
      for (const std::string& s : subModuleLocs) {
        auto strObj =
            std::make_shared<objects::StrictString>(objects::StrType(), mod, s);
        pathVec.push_back(std::move(strObj));
      }
      mod->setAttr(
          "__path__",
          std::make_shared<objects::StrictList>(
              objects::ListType(), mod, std::move(pathVec)));
    }

    analyzedModule->setModuleValue(mod);

    // do analysis
    Analyzer analyzer(
        ast,
        this,
        Symtable(moduleInfo.getSymtable()),
        globalScope,
        errorSinkBorrowed,
        filename,
        name,
        "<module>",
        mod,
        moduleInfo.getFutureAnnotations());

    analyzer.analyze();
    analyzedModule->setAstToResults(analyzer.passAstToResultsMap());
  }

  return analyzedModule;
}

bool ModuleLoader::isAllowListed(const std::string& modName) {
  for (const auto& allowed : allowList_) {
    if (allowed.first == modName) {
      return true;
    }
    switch (allowed.second) {
      case AllowListKind::kPrefix: {
        std::size_t startPos = modName.rfind(allowed.first, 0);
        // modName startswith an allowlisted mod name prefix,
        // and the next character after the prefix is '.'.
        // This differentiates allowed.mod.name.the.rest versus
        // allowed.mod.nameAndOthers
        if (startPos == 0 && modName[allowed.first.size()] == '.') {
          return true;
        }
      }
      case AllowListKind::kExact: {
        continue;
      }
    }
  }
  return false;
}

int ModuleLoader::getAnalyzedModuleCount() const {
  int count = 0;
  for (auto& m : modules_) {
    if (m.second != nullptr && m.second->getModuleValue() != nullptr) {
      count++;
    }
  }
  return count;
}

bool ModuleLoader::loadStrictModuleModule() {
  const std::string& name = objects::strictModName;
  auto it = modules_.find(name);
  if (it == modules_.end()) {
    auto strictModKind = ModuleKind::kStrict;
    auto stubKind = StubKind(0);
    auto moduleInfo = std::make_unique<ModuleInfo>(
        name, "", nullptr, false, nullptr, stubKind);
    auto analyzedModule = std::make_unique<AnalyzedModule>(
        strictModKind, errorSinkFactory_(), std::move(moduleInfo));
    auto strictModModule = objects::createStrictModulesModule();
    strictModModule->setAttr(
        "__name__",
        std::make_shared<objects::StrictString>(
            objects::StrType(), strictModModule, name));
    analyzedModule->setModuleValue(std::move(strictModModule));
    modules_[name] = std::move(analyzedModule);
    return true;
  }
  return false;
}

bool ModuleLoader::isModuleLoaded(const std::string& modName) {
  return modules_.find(modName) != modules_.end();
}
} // namespace strictmod::compiler
