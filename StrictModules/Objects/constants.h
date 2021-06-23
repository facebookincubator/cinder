// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTMOD_CONSTANTS_H__
#define __STRICTMOD_CONSTANTS_H__

#include "StrictModules/Objects/object_type.h"

namespace strictmod::objects {
class NoneObject_ : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;
  // wrapped method
  static std::shared_ptr<BaseStrictObject> None__bool__(
      std::shared_ptr<NoneObject_> self,
      const CallerContext& caller);

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
};

class NoneType_ : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;
  virtual void addMethods() override;
};

class NotImplementedObject : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
};

class StrictEllipsisObject : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;
  // wrapped method
  static std::shared_ptr<BaseStrictObject> Ellipsis__repr__(
      std::shared_ptr<StrictEllipsisObject> self,
      const CallerContext& caller);

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
};

class StrictEllipsisType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;
  virtual void addMethods() override;
};

} // namespace strictmod::objects

#endif // __STRICTMOD_CONSTANTS_H__
