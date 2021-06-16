// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTMOD_CONSTANTS_H__
#define __STRICTMOD_CONSTANTS_H__

#include "StrictModules/Objects/object_type.h"

namespace strictmod::objects {
class NoneObject_ : public StrictInstance {
  using StrictInstance::StrictInstance;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
};

class NoneType_ : public StrictObjectType {
  using StrictObjectType::StrictObjectType;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;
};

class NotImplementedObject : public StrictInstance {
  using StrictInstance::StrictInstance;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
};

} // namespace strictmod::objects

#endif // __STRICTMOD_CONSTANTS_H__
