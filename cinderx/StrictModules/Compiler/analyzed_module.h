// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Compiler/module_info.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/error_sink.h"
#include "cinderx/StrictModules/symbol_table.h"

#include <memory>
namespace strictmod::compiler {
using strictmod::objects::StrictModuleObject;

enum class ModuleKind {
  kStrict,
  kStatic,
  kNonStrict,
};

enum class ShouldAnalyze {
  kYes,
  kNo,
};

struct PreprocessingRecord {
  mod_ty preprocessedAst;
  mod_ty originalAst;
};

class AnalyzedModule {
  using astToResultT = strictmod::objects::astToResultT;

 public:
  AnalyzedModule(
      std::unique_ptr<StrictModuleObject> module,
      ModuleKind moduleKind,
      std::shared_ptr<BaseErrorSink> error,
      std::unique_ptr<ModuleInfo> modInfo)
      : module_(std::move(module)),
        moduleKind_(moduleKind),
        errorSink_(std::move(error)),
        astToResults_(),
        modInfo_(std::move(modInfo)),
        preprocessRecord_({nullptr, nullptr}) {}
  AnalyzedModule(
      ModuleKind moduleKind,
      std::shared_ptr<BaseErrorSink> error,
      std::unique_ptr<ModuleInfo> modInfo)
      : AnalyzedModule(
            nullptr,
            moduleKind,
            std::move(error),
            std::move(modInfo)) {}
  ~AnalyzedModule() {
    cleanModuleContent();
  }

  bool isStrict() const;
  bool isStatic() const;
  bool getError() const;
  const BaseErrorSink& getErrorSink() const;
  BaseErrorSink& getErrorSink();
  std::shared_ptr<StrictModuleObject> getModuleValue();

  void setModuleValue(std::shared_ptr<StrictModuleObject> module);
  void cleanModuleContent();

  void setAstToResults(std::unique_ptr<astToResultT> map) {
    astToResults_ = std::move(map);
  }

  astToResultT* getAstToResults() {
    return astToResults_.get();
  }

  Ref<> getPyAst(PyArena* arena);

  const ModuleInfo& getModuleInfo() const {
    return *modInfo_;
  }

  int getStubKindAsInt() const {
    return modInfo_->getStubKind().getValue();
  }
  int getModKindAsInt() const;

 private:
  std::shared_ptr<StrictModuleObject> module_;
  ModuleKind moduleKind_;
  std::shared_ptr<BaseErrorSink> errorSink_;
  std::unique_ptr<astToResultT> astToResults_;
  std::unique_ptr<ModuleInfo> modInfo_;
  PreprocessingRecord preprocessRecord_;
};
} // namespace strictmod::compiler
