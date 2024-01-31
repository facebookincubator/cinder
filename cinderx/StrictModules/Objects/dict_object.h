// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/iterable_objects.h"

namespace strictmod::objects {
typedef sequence_map<
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    StrictObjectHasher,
    StrictObjectEqual>
    DictDataT;

class DictDataInterface {
 public:
  virtual ~DictDataInterface() {}
  // set
  virtual bool set(
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> value) = 0;
  // get, return std::nullopt if key does not exist
  virtual std::optional<std::shared_ptr<BaseStrictObject>> get(
      const std::shared_ptr<BaseStrictObject>& key) = 0;
  virtual bool contains(const std::shared_ptr<BaseStrictObject>& key) const = 0;
  virtual std::size_t size() const = 0;
  virtual bool erase(const std::shared_ptr<BaseStrictObject>& key) = 0;
  virtual void clear() = 0;
  virtual void insert(const DictDataInterface& other) = 0;
  virtual std::unique_ptr<DictDataInterface> copy() = 0;

  /* iterate on items in the dict, and call func on each item
   * If func returns false, iteration is stopped
   */
  virtual void iter(std::function<bool(
                        std::shared_ptr<BaseStrictObject>,
                        std::shared_ptr<BaseStrictObject>)> func) = 0;
  virtual void const_iter(
      std::function<bool(
          std::shared_ptr<BaseStrictObject>,
          std::shared_ptr<BaseStrictObject>)> func) const = 0;
};

/* dict interface backed directly by a hashmap mapping
 * hasable strict values to strict values (DictDataT)
 */
class DirectMapDictData : public DictDataInterface {
 public:
  DirectMapDictData(DictDataT data) : data_(std::move(data)) {}
  virtual bool set(
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> value) override;
  virtual std::optional<std::shared_ptr<BaseStrictObject>> get(
      const std::shared_ptr<BaseStrictObject>& key) override;
  virtual bool contains(
      const std::shared_ptr<BaseStrictObject>& key) const override;
  virtual std::size_t size() const override;
  virtual bool erase(const std::shared_ptr<BaseStrictObject>& key) override;
  virtual void clear() override;
  virtual void insert(const DictDataInterface& other) override;
  virtual std::unique_ptr<DictDataInterface> copy() override;

  /* iterate on items in the dict, and call func on each item
   * If func returns false, iteration is stopped
   */
  virtual void iter(std::function<bool(
                        std::shared_ptr<BaseStrictObject>,
                        std::shared_ptr<BaseStrictObject>)> func) override;
  virtual void const_iter(
      std::function<bool(
          std::shared_ptr<BaseStrictObject>,
          std::shared_ptr<BaseStrictObject>)> func) const override;

 private:
  DictDataT data_;
};

/* dict interface backed by a member dict of a strict object
 * Namely, this is an interface to __dict__ of a strict object
 */
class InstanceDictDictData : public DictDataInterface {
 public:
  InstanceDictDictData(
      std::shared_ptr<DictType> data,
      std::weak_ptr<StrictModuleObject> creator)
      : data_(std::move(data)), creator_(std::move(creator)) {}

  virtual bool set(
      std::shared_ptr<BaseStrictObject> key,
      std::shared_ptr<BaseStrictObject> value) override;
  virtual std::optional<std::shared_ptr<BaseStrictObject>> get(
      const std::shared_ptr<BaseStrictObject>& key) override;
  virtual bool contains(
      const std::shared_ptr<BaseStrictObject>& key) const override;
  virtual std::size_t size() const override;
  virtual bool erase(const std::shared_ptr<BaseStrictObject>& key) override;
  virtual void clear() override;
  virtual void insert(const DictDataInterface& other) override;
  virtual std::unique_ptr<DictDataInterface> copy() override;

  /* iterate on items in the dict, and call func on each item
   * If func returns false, iteration is stopped
   */
  virtual void iter(std::function<bool(
                        std::shared_ptr<BaseStrictObject>,
                        std::shared_ptr<BaseStrictObject>)> func) override;
  virtual void const_iter(
      std::function<bool(
          std::shared_ptr<BaseStrictObject>,
          std::shared_ptr<BaseStrictObject>)> func) const override;

 private:
  std::shared_ptr<DictType> data_;
  std::weak_ptr<StrictModuleObject> creator_;
};

class StrictDict : public StrictIterable {
 public:
  StrictDict(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      DictDataT data,
      std::string displayName = "");

  StrictDict(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      DictDataT data,
      std::string displayName = "");

  StrictDict(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::unique_ptr<DictDataInterface> data =
          std::make_unique<DirectMapDictData>(DictDataT()),
      std::string displayName = "");

  StrictDict(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      std::unique_ptr<DictDataInterface> data =
          std::make_unique<DirectMapDictData>(DictDataT()),
      std::string displayName = "");

  const DictDataInterface& getData() const {
    return *data_;
  }

  void updateDict(const DictType& data, const CallerContext& caller) {
    for (auto item : data) {
      data_->set(caller.makeStr(item.first), item.second.first);
    }
  }

  virtual std::string getDisplayName() const override;
  virtual Ref<> getPyObject() const override;

  /* clear dict content before other clean up */
  virtual void cleanContent(const StrictModuleObject* owner) override;

  // wrapped method
  static std::shared_ptr<BaseStrictObject> dict__init__(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> dictUpdate(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller);

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

 private:
  std::unique_ptr<DictDataInterface> data_;
  std::string displayName_;

  static void dictUpdateHelper(
      std::shared_ptr<StrictDict> self,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      bool noPosArg,
      const CallerContext& caller);
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

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
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

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};
} // namespace strictmod::objects
