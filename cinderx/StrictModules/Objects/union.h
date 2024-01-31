// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
namespace strictmod::objects {
class StrictUnion : public StrictInstance {
 public:
  StrictUnion(
      std::weak_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> args);

  const std::vector<std::shared_ptr<BaseStrictObject>> getArgs() const {
    return args_;
  }

  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> union__instancecheck__(
      std::shared_ptr<StrictUnion> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

  static std::shared_ptr<BaseStrictObject> union__subclasscheck__(
      std::shared_ptr<StrictUnion> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

  static std::shared_ptr<BaseStrictObject> union__or__(
      std::shared_ptr<StrictUnion> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

  static std::shared_ptr<BaseStrictObject> union__ror__(
      std::shared_ptr<StrictUnion> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

  static std::shared_ptr<BaseStrictObject> union__args__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

 private:
  std::vector<std::shared_ptr<BaseStrictObject>> args_;
  std::shared_ptr<BaseStrictObject> argsObj_;
};

// create a union type from left and right (i.e. left | right)
std::shared_ptr<BaseStrictObject> unionOrHelper(
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> left,
    std::shared_ptr<BaseStrictObject> right);

class StrictUnionType : public StrictObjectType {
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
} // namespace strictmod::objects
