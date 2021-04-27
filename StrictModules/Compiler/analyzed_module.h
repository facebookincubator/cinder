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
 public:
  AnalyzedModule(
      std::unique_ptr<StrictModuleObject> module,
      ModuleKind kind,
      std::unique_ptr<BaseErrorSink> error)
      : module_(std::move(module)),
        moduleKind_(kind),
        errorSink_(std::move(error)) {}
  AnalyzedModule(ModuleKind kind, std::unique_ptr<BaseErrorSink> error)
      : AnalyzedModule(nullptr, kind, std::move(error)) {}

  bool isStrict() const;
  bool isStatic() const;
  bool getError() const;
  const BaseErrorSink& getErrorSink() const;
  std::shared_ptr<StrictModuleObject> getModuleValue();

  void setModuleValue(std::shared_ptr<StrictModuleObject> module);

 private:
  std::shared_ptr<StrictModuleObject> module_;
  ModuleKind moduleKind_;
  std::unique_ptr<BaseErrorSink> errorSink_;
};
} // namespace strictmod::compiler

#endif // __STRICTM_ANALYZED_MODULE_H__
