// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/base_object.h"

namespace strictmod::objects {

// Stores references to the args from the function object
class FuncSignature {
 public:
  FuncSignature(
      const std::string& funcName,
      const std::vector<std::string>& posonlyArgs,
      const std::vector<std::string>& posArgs,
      const std::vector<std::string>& kwonlyArgs,
      const std::optional<std::string>& varArg,
      const std::optional<std::string>& kwVarArg,
      const std::vector<std::shared_ptr<BaseStrictObject>>& posDefaults,
      const std::vector<std::shared_ptr<BaseStrictObject>>& kwDefaults);

  std::unique_ptr<DictType> bind(
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& names,
      const CallerContext& caller);

 private:
  const std::string& funcName_;
  const std::vector<std::string>& posonlyArgs_;
  const std::vector<std::string>& posArgs_;
  const std::vector<std::string>& kwonlyArgs_;
  const std::optional<std::string>& varArg_;
  const std::optional<std::string>& kwVarArg_;
  const std::vector<std::shared_ptr<BaseStrictObject>>& posDefaults_;
  const std::vector<std::shared_ptr<BaseStrictObject>>&
      kwDefaults_; // same size as kwonlyArgs_
};
} // namespace strictmod::objects
