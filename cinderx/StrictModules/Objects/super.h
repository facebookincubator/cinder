// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
namespace strictmod::objects {
// Instance of super(T, obj)
class StrictSuper : public StrictInstance {
 public:
  StrictSuper(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictType> currentClass,
      std::shared_ptr<BaseStrictObject> self,
      std::shared_ptr<StrictType> selfClass,
      bool ignoreUnknowns = false);

  const std::shared_ptr<StrictType>& getCurrentClass() {
    return currentClass_;
  }
  const std::shared_ptr<BaseStrictObject>& getObj() {
    return self_;
  }
  const std::shared_ptr<StrictType>& getObjClass() {
    return selfClass_;
  }
  bool ignoreUnknowns() {
    return ignoreUnknowns_;
  }

  virtual std::string getDisplayName() const override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> super__init__(
      std::shared_ptr<StrictSuper> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> currentClass, // T
      std::shared_ptr<BaseStrictObject> obj);

 private:
  std::shared_ptr<StrictType> currentClass_; // T
  std::shared_ptr<BaseStrictObject> self_; // obj
  std::shared_ptr<StrictType> selfClass_; // type(obj)
  bool ignoreUnknowns_;
};

class StrictSuperType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;
  virtual std::shared_ptr<BaseStrictObject> loadAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> defaultValue,
      const CallerContext& caller) override;

  virtual void storeAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual void delAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual bool isBaseType() const override;
};
} // namespace strictmod::objects
