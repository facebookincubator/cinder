// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "cinderx/StrictModules/Objects/base_object.h"

namespace strictmod::objects {
class StrictObjectType;
class StrictInstance : public BaseStrictObject {
 public:
  friend class StrictObjectType;
  StrictInstance(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      std::shared_ptr<DictType> dict = nullptr);

  StrictInstance(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<DictType> dict = nullptr);

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  std::shared_ptr<BaseStrictObject> getAttr(const std::string& name);
  void setAttr(std::string name, std::shared_ptr<BaseStrictObject> value);

  std::unique_ptr<DictType> copyDict() {
    return std::make_unique<DictType>(*dict_);
  }

  std::shared_ptr<DictType> getDict() {
    return dict_;
  }

  // implemented in dict_object.cpp
  std::shared_ptr<BaseStrictObject> getDunderDict();

  void setDict(std::shared_ptr<DictType> dict) {
    dict_ = dict;
    dictObj_ = nullptr;
  }

  /* clear all content in __dict__ that's owned by owner. Use this during
   * shutdown */
  virtual void cleanContent(const StrictModuleObject* owner) override;

 protected:
  std::shared_ptr<DictType> dict_;
  std::shared_ptr<BaseStrictObject> dictObj_; // __dict__, backed by dict_
  bool cleaned_; // flag used during clean up to prevent cycles
  std::optional<std::string> doc_; // every object can have __doc__
};
} // namespace strictmod::objects
