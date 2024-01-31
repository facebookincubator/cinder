// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/sequence_map.h"

namespace strictmod::objects {
class StrictExceptionObject : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;

  virtual std::string getDisplayName() const override;

  static std::shared_ptr<BaseStrictObject> exception__new__(
      std::shared_ptr<StrictExceptionObject>,
      const CallerContext& caller,
      std::vector<std::shared_ptr<BaseStrictObject>> args,
      sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs);

 private:
  mutable std::string displayName_;
};

class StrictExceptionType : public StrictObjectType {
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
