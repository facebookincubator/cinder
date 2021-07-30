// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Compiler/analyzed_module.h"
#include "StrictModules/ast_preprocessor.h"

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

Ref<> AnalyzedModule::getPyAst(bool preprocess, PyArena* arena) {
  if (ast_ == nullptr) {
    return nullptr;
  }
  if (preprocess) {
    auto preprocessor = Preprocessor(ast_, astToResults_.get(), arena);
    preprocessor.preprocess();
  }
  Ref<> result = Ref<>::steal(PyAST_mod2obj(ast_));
  return result;
}

mod_ty AnalyzedModule::getAST() {
  return ast_;
}

} // namespace strictmod::compiler
