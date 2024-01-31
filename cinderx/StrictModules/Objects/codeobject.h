// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/numerics.h"
#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/Objects/string_object.h"

namespace strictmod::objects {
class StrictCodeObject : public StrictInstance {
 public:
  StrictCodeObject(
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictString> name,
      std::shared_ptr<StrictInt> argCount,
      std::shared_ptr<StrictInt> posOnlyArgCount,
      std::shared_ptr<StrictInt> kwOnlyArgCount,
      std::shared_ptr<StrictInt> flags,
      std::shared_ptr<BaseStrictObject> varNames);

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> codeArgCountGetter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> codePosOnlyArgCountGetter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> codeNameGetter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> codeFlagsGetter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> codeVarnamesGetter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> codeKwOnlyArgCountGetter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

 private:
  std::shared_ptr<StrictString> name_;
  std::shared_ptr<StrictInt> argCount_;
  std::shared_ptr<StrictInt> posOnlyArgCount_;
  std::shared_ptr<StrictInt> kwOnlyArgCount_;
  std::shared_ptr<StrictInt> flags_;
  std::shared_ptr<BaseStrictObject> varNames_;
};

class StrictCodeObjectType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};
} // namespace strictmod::objects
