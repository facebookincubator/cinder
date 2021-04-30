#ifndef __STRICTM_DICT_OBJ_H__
#define __STRICTM_DICT_OBJ_H__

#include "StrictModules/Objects/iterable_objects.h"

namespace strictmod::objects {
typedef std::unordered_map<
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    StrictObjectHasher,
    StrictObjectEqual>
    DictDataT;

class StrictDict : public StrictIterable {
 public:
  StrictDict(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      DictDataT data = DictDataT(),
      std::string displayName = "");

  StrictDict(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      DictDataT data = DictDataT(),
      std::string displayName = "");

  const DictDataT& getData() const {
    return data_;
  }

  virtual std::string getDisplayName() const override;
  virtual Ref<> getPyObject() const override;

  // wrapped method
  static std::shared_ptr<BaseStrictObject> dict__len__(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> dict__getitem__(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key);

  static std::shared_ptr<BaseStrictObject> dict__setitem__(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> value);

  static std::shared_ptr<BaseStrictObject> dict__delitem__(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key);

  static std::shared_ptr<BaseStrictObject> dict__contains__(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key);

  static std::shared_ptr<BaseStrictObject> dictGet(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> defaultValue = nullptr);

  static std::shared_ptr<BaseStrictObject> dictSetDefault(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> value);

  static std::shared_ptr<BaseStrictObject> dictCopy(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> dictPop(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> defaultValue = nullptr);

  static std::shared_ptr<BaseStrictObject> dictKeys(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> dictValues(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> dictItems(
      std::shared_ptr<StrictDict> self,
      const CallerContext& caller);

  // TODO __init__, update (require kwargs support)

 private:
  DictDataT data_;
  std::string displayName_;
};

class StrictDictType : public StrictIterableType {
 public:
  using StrictIterableType::StrictIterableType;

  virtual void addMethods() override;

  virtual std::shared_ptr<StrictIteratorBase> getElementsIter(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::vector<std::shared_ptr<BaseStrictObject>> getElementsVec(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;
};

class StrictDictView : public StrictInstance {
 public:
  enum ViewKind { kKey = 0, kValue = 1, kItem = 2 };
  StrictDictView(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictDict> viewedObj,
      ViewKind kind);

  StrictDictView(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictDict> viewedObj,
      ViewKind kind);

  const std::shared_ptr<const StrictDict> getViewed() const {
    return viewedObj_;
  }

  ViewKind getViewKind() const {
    return kind_;
  }

  virtual std::string getDisplayName() const override;

  // wrapped method
  static std::shared_ptr<BaseStrictObject> dictview__len__(
      std::shared_ptr<StrictDictView> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> dictview__iter__(
      std::shared_ptr<StrictDictView> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<StrictDict> viewedObj_;
  ViewKind kind_;
};

class StrictDictViewType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual void addMethods() override;

  virtual std::shared_ptr<StrictIteratorBase> getElementsIter(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::vector<std::shared_ptr<BaseStrictObject>> getElementsVec(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;
};
} // namespace strictmod::objects
#endif //#ifndef __STRICTM_DICT_OBJ_H__
