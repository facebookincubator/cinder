// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Compiler/analyzed_module.h"

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

} // namespace strictmod::compiler
