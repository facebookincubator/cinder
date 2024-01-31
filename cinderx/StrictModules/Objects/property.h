// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/Objects/string_object.h"
namespace strictmod::objects {
class StrictProperty : public StrictInstance {
 public:
  friend class StrictPropertyType;
  StrictProperty(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> fget,
      std::shared_ptr<BaseStrictObject> fset,
      std::shared_ptr<BaseStrictObject> fdel);

  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

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
  std::optional<std::string> doc_; // the __doc__ field
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

  virtual bool isDataDescr(const CallerContext& caller) override;
};

// GetSetDescriptor, i.e. builtin property

using TDescrGetFunc = std::shared_ptr<BaseStrictObject> (*)(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<StrictType>,
    const CallerContext&);

using TDescrSetFunc = void (*)(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&);

using TDescrDelFunc =
    void (*)(std::shared_ptr<BaseStrictObject>, const CallerContext&);

class StrictGetSetDescriptor : public StrictInstance {
 public:
  StrictGetSetDescriptor(
      std::weak_ptr<StrictModuleObject> creator,
      std::string name,
      TDescrGetFunc fget,
      TDescrSetFunc fset = nullptr,
      TDescrDelFunc fdel = nullptr);

  TDescrGetFunc getFget() const {
    return fget_;
  }
  TDescrSetFunc getFset() const {
    return fset_;
  }
  TDescrDelFunc getFdel() const {
    return fdel_;
  }
  const std::string& getName() const {
    return name_;
  }
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped funcitons
  static std::shared_ptr<BaseStrictObject> getsetdescr__get__(
      std::shared_ptr<StrictGetSetDescriptor> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> type);

  static std::shared_ptr<BaseStrictObject> getsetdescr__set__(
      std::shared_ptr<StrictGetSetDescriptor> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value);

  static std::shared_ptr<BaseStrictObject> getsetdescr__delete__(
      std::shared_ptr<StrictGetSetDescriptor> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst);

 private:
  std::string name_;
  TDescrGetFunc fget_;
  TDescrSetFunc fset_;
  TDescrDelFunc fdel_;
};

class StrictGetSetDescriptorType : public StrictObjectType {
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

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual bool isDataDescr(const CallerContext& caller) override;
};

template <typename G, typename S, typename D>
void StrictType::addGetSetDescriptor(
    const std::string& name,
    G getter,
    S setter,
    D deleter) {
  auto descr = std::make_shared<StrictGetSetDescriptor>(
      creator_, name, getter, setter, deleter);
  setAttr(name, std::move(descr));
}
} // namespace strictmod::objects
