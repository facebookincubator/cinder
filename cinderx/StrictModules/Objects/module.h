// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "cinderx/StrictModules/Objects/instance.h"

#include <string>

namespace strictmod::objects {
class StrictModuleObject : public StrictInstance {
 private:
  std::string name_;

 public:
  StrictModuleObject(
      std::shared_ptr<StrictType> type,
      std::string name,
      std::shared_ptr<DictType> dict = nullptr);

  virtual std::string getDisplayName() const override;

  const std::string getModuleName() const {
    return name_;
  }

  static std::shared_ptr<StrictModuleObject> makeStrictModule(
      std::shared_ptr<StrictType> type,
      std::string name,
      std::shared_ptr<DictType> dict = nullptr);
};
} // namespace strictmod::objects
