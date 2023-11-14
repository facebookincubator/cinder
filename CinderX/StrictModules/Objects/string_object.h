// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/object_type.h"
namespace strictmod::objects {

class StrictString : public StrictInstance {
 public:
  StrictString(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      std::string value);

  StrictString(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::string value);

  StrictString(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      Ref<> pyValue);

  const std::string& getValue() const {
    return value_;
  }

  virtual bool isHashable() const override;
  virtual size_t hash() const override;
  virtual bool eq(const BaseStrictObject& other) const override;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  static std::shared_ptr<BaseStrictObject> strFromPyObj(
      Ref<> pyObj,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> listFromPyStrList(
      Ref<> pyObj,
      const CallerContext& caller);

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> str__new__(
      std::shared_ptr<StrictString>,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> instType,
      std::shared_ptr<BaseStrictObject> object);

  static std::shared_ptr<BaseStrictObject> str__len__(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> str__str__(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> str__iter__(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> str__eq__(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> other);

  static std::shared_ptr<BaseStrictObject> strJoin(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> iterable);

  static std::shared_ptr<BaseStrictObject> strFormat(
      std::shared_ptr<BaseStrictObject> self,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> str__getitem__(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> index);

 private:
  mutable Ref<> pyStr_;
  std::string value_;
};

class StrictStringType : public StrictObjectType {
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

  virtual Ref<> getPyObject() const override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictBytes : public StrictInstance {
 public:
  StrictBytes(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      PyObject* bytesObj);

  StrictBytes(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      Ref<> bytesObj);

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  static std::shared_ptr<BaseStrictObject> bytesFromPyObj(
      Ref<> pyObj,
      const CallerContext& caller);

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> bytes__len__(
      std::shared_ptr<StrictBytes> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> bytes__iter__(
      std::shared_ptr<StrictBytes> self,
      const CallerContext& caller);

 private:
  Ref<> bytesObj_;
};

class StrictBytesType : public StrictObjectType {
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

  virtual Ref<> getPyObject() const override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

// ByteArray
class StrictByteArray : public StrictInstance {
 public:
  StrictByteArray(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      PyObject* bytearrayObj);

  StrictByteArray(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      Ref<> bytearrayObj);

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> bytearray__iter__(
      std::shared_ptr<StrictByteArray> self,
      const CallerContext& caller);

 private:
  Ref<> bytearrayObj_;
};

class StrictByteArrayType : public StrictObjectType {
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

  virtual Ref<> getPyObject() const override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

} // namespace strictmod::objects
