// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/module.h"

namespace strictmod::objects {
StrictModuleObject::StrictModuleObject(
    std::shared_ptr<StrictType> type,
    std::string name,
    std::shared_ptr<DictType> dict)
    : StrictInstance(type, nullptr, std::move(dict)), name_(std::move(name)) {}

std::string StrictModuleObject::getDisplayName() const {
  return "<strict module " + name_ + ">";
}

std::shared_ptr<StrictModuleObject> StrictModuleObject::makeStrictModule(
    std::shared_ptr<StrictType> type,
    std::string name,
    std::shared_ptr<DictType> dict) {
  auto mod = std::make_shared<StrictModuleObject>(
      std::move(type), std::move(name), std::move(dict));
  mod->setCreator(mod);
  return mod;
}
} // namespace strictmod::objects
