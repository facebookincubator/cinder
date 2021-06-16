// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_PROPERTY_H__
#define __STRICTM_PROPERTY_H__

#include "StrictModules/Objects/object_type.h"
namespace strictmod::objects {
class StrictProperty : public StrictInstance {
 public:
  StrictProperty(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> fget,
      std::shared_ptr<BaseStrictObject> fset,
      std::shared_ptr<BaseStrictObject> fdel);

  virtual std::string getDisplayName() const override;
  // wrapped functions
  static std::shared_ptr<BaseStrictObject> property__init__(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> propertyGetter(
      std::shared_ptr<StrictProperty> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> arg);

  static std::shared_ptr<BaseStrictObject> propertySetter(
      std::shared_ptr<StrictProperty> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> arg);

  static std::shared_ptr<BaseStrictObject> propertyDeleter(
      std::shared_ptr<StrictProperty> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> arg);

  static std::shared_ptr<BaseStrictObject> property__get__(
      std::shared_ptr<StrictProperty> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> type);

  static std::shared_ptr<BaseStrictObject> property__set__(
      std::shared_ptr<StrictProperty> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value);

  static std::shared_ptr<BaseStrictObject> property__delete__(
      std::shared_ptr<StrictProperty> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

 private:
  std::shared_ptr<BaseStrictObject> fget_;
  std::shared_ptr<BaseStrictObject> fset_;
  std::shared_ptr<BaseStrictObject> fdel_;
};

class StrictPropertyType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> setDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> delDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
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
};
} // namespace strictmod::objects
#endif // __STRICTM_PROPERTY_H__
