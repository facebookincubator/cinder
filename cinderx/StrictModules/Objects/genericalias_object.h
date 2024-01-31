// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
namespace strictmod::objects {
class StrictGenericAlias : public StrictInstance {
 public:
  StrictGenericAlias(
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> origin,
      std::shared_ptr<BaseStrictObject> args);

  StrictGenericAlias(
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> origin,
      std::vector<std::shared_ptr<BaseStrictObject>> args);

  const std::vector<std::shared_ptr<BaseStrictObject>> getArgs() const {
    return args_;
  }

  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> ga__getitem__(
      std::shared_ptr<StrictGenericAlias> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> idx);

  static std::shared_ptr<BaseStrictObject> ga__mro_entries__(
      std::shared_ptr<StrictGenericAlias> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> args);

  static std::shared_ptr<BaseStrictObject> ga__instancecheck__(
      std::shared_ptr<StrictGenericAlias> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

  static std::shared_ptr<BaseStrictObject> ga__subclasscheck__(
      std::shared_ptr<StrictGenericAlias> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

  static std::shared_ptr<BaseStrictObject> ga__args__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> ga__origin__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> ga__parameters__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType>,
      const CallerContext& caller);

 private:
  void makeParametersHelper(const CallerContext& caller);
  std::vector<std::shared_ptr<BaseStrictObject>> subParametersHelper(
      const CallerContext& caller,
      const std::shared_ptr<BaseStrictObject>& item);

  std::vector<std::shared_ptr<BaseStrictObject>> args_;
  std::optional<std::vector<std::shared_ptr<BaseStrictObject>>> parameters_;
  std::shared_ptr<BaseStrictObject> origin_;
  std::shared_ptr<BaseStrictObject> argsObj_;
  std::shared_ptr<BaseStrictObject> parametersObj_;
};

class StrictGenericAliasType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

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

std::shared_ptr<BaseStrictObject> createGenericAlias(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> args);

} // namespace strictmod::objects
