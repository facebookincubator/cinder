// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_ANALYZED_MODULE_H__
#define __STRICTM_ANALYZED_MODULE_H__

#include <memory>
#include "StrictModules/Objects/objects.h"
#include "StrictModules/error_sink.h"
namespace strictmod::compiler {
using strictmod::objects::StrictModuleObject;

enum class ModuleKind { kStrict, kStatic, kNonStrict };

class AnalyzedModule {
  using astToResultT = strictmod::objects::astToResultT;

 public:
  AnalyzedModule(
      std::unique_ptr<StrictModuleObject> module,
      ModuleKind kind,
      std::shared_ptr<BaseErrorSink> error,
      mod_ty ast)
      : module_(std::move(module)),
        moduleKind_(kind),
        errorSink_(std::move(error)),
        ast_(ast),
        astToResults_() {}
  AnalyzedModule(
      ModuleKind kind,
      std::shared_ptr<BaseErrorSink> error,
      mod_ty ast)
      : AnalyzedModule(nullptr, kind, std::move(error), ast) {}
  ~AnalyzedModule() {
    cleanModuleContent();
  }

  bool isStrict() const;
  bool isStatic() const;
  bool getError() const;
  const BaseErrorSink& getErrorSink() const;
  std::shared_ptr<StrictModuleObject> getModuleValue();

  void setModuleValue(std::shared_ptr<StrictModuleObject> module);
  void cleanModuleContent();

  void setAstToResults(std::unique_ptr<astToResultT> map) {
    astToResults_ = std::move(map);
  }

  /** notice that this produces a fresh python AST
   */
  Ref<> getPyAst(bool preprocess, PyArena* arena);

  mod_ty getAST();

 private:
  std::shared_ptr<StrictModuleObject> module_;
  ModuleKind moduleKind_;
  std::shared_ptr<BaseErrorSink> errorSink_;
  mod_ty ast_;
  std::unique_ptr<astToResultT> astToResults_;
};
} // namespace strictmod::compiler

#endif // __STRICTM_ANALYZED_MODULE_H__
