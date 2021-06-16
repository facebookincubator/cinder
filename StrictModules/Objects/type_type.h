// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_TYPE_TYPE_H__
#define __STRICTM_TYPE_TYPE_H__

#include "StrictModules/Objects/object_type.h"

namespace strictmod::objects {
class StrictTypeType : public StrictObjectType {
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

#endif //__STRICTM_TYPE_TYPE_H__
