// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Compiler/analyzed_module.h"

#include "StrictModules/ast_preprocessor.h"
#include "StrictModules/pystrictmodule.h"

namespace strictmod::compiler {
bool AnalyzedModule::isStrict() const {
  return moduleKind_ != ModuleKind::kNonStrict;
}

bool AnalyzedModule::isStatic() const {
  return moduleKind_ == ModuleKind::kStatic;
}

bool AnalyzedModule::getError() const {
  return errorSink_->hasError();
}

const BaseErrorSink& AnalyzedModule::getErrorSink() const {
  return *errorSink_;
}

BaseErrorSink& AnalyzedModule::getErrorSink() {
  return *errorSink_;
}

std::shared_ptr<StrictModuleObject> AnalyzedModule::getModuleValue() {
  return std::shared_ptr(module_);
}

void AnalyzedModule::setModuleValue(
    std::shared_ptr<StrictModuleObject> module) {
  module_ = std::move(module);
}

void AnalyzedModule::cleanModuleContent() {
  if (module_) {
    module_->cleanContent(module_.get());
  }
}

int AnalyzedModule::getModKindAsInt() const {
  switch (moduleKind_) {
    case ModuleKind::kStrict:
      return Ci_STRICT_MODULE_KIND;
    case ModuleKind::kStatic:
      return Ci_STATIC_MODULE_KIND;
    case ModuleKind::kNonStrict:
      return Ci_NONSTRICT_MODULE_KIND;
  }
  Py_UNREACHABLE();
}

static mod_ty copyAST(mod_ty ast, PyArena* arena) {
  return PyAST_obj2mod(Ref<>::steal(PyAST_mod2obj(ast)), arena, 0);
}

Ref<> AnalyzedModule::getPyAst(bool preprocess, PyArena* arena) {
  if (preprocess) {
    mod_ty target = preprocessRecord_.preprocessedAst;
    if (!target) {
      // preprocessed AST has to be the original parsed one
      // in the module info since we need the original addresses
      // as dict keys in the preprocessor
      mod_ty original = modInfo_->getAst();
      if (!original) {
        return nullptr;
      }
      target = original;
      preprocessRecord_.preprocessedAst = original;
      if (!preprocessRecord_.originalAst) {
        // before changing the original ast, create a copy for
        // unprocessed AST
        mod_ty copy = copyAST(original, arena);
        preprocessRecord_.originalAst = copy;
      }
      if (astToResults_) {
        // for modules not labeled strict, there is nothing to process
        auto processor = Preprocessor(target, astToResults_.get(), arena);
        processor.preprocess();
      }
    }
    Ref<> result = Ref<>::steal(PyAST_mod2obj(target));
    return result;
  }

  mod_ty target = preprocessRecord_.originalAst;
  if (!target) {
    mod_ty original = modInfo_->getAst();
    if (!original) {
      return nullptr;
    }
    mod_ty copy = copyAST(original, arena);
    target = copy;
    preprocessRecord_.originalAst = copy;
  }
  Ref<> result = Ref<>::steal(PyAST_mod2obj(target));
  return result;
}
} // namespace strictmod::compiler
