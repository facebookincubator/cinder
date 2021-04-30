// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/dict_object.h"

#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"
#include "StrictModules/exceptions.h"

#include <unordered_map>

namespace strictmod::objects {

// StrictDict
StrictDict::StrictDict(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    DictDataT data,
    std::string displayName)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)),
      displayName_(displayName) {}

StrictDict::StrictDict(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    DictDataT data,
    std::string displayName)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)),
      displayName_(displayName) {}

std::string StrictDict::getDisplayName() const {
  if (displayName_.empty()) {
    std::vector<std::string> items;
    items.reserve(data_.size());
    for (auto& item : data_) {
      items.push_back(fmt::format("{}: {}", item.first, item.second));
    }
    return fmt::format("{{{}}}", fmt::join(std::move(items), ", "));
  } else {
    // overwritten displayName
    return displayName_;
  }
}

Ref<> StrictDict::getPyObject() const {
  Ref<> pyObj = Ref<>::steal(PyDict_New());
  if (pyObj == nullptr) {
    return nullptr;
  }
  for (auto& it : data_) {
    Ref<> key = it.first->getPyObject();
    if (key == nullptr) {
      return nullptr;
    }
    Ref<> value = it.second->getPyObject();
    if (value == nullptr) {
      return nullptr;
    }
    if (PyDict_SetItem(pyObj.get(), key.get(), value.get()) < 0) {
      PyErr_Clear();
      return nullptr;
    }
  }
  return pyObj;
}

// wrapped method
std::shared_ptr<BaseStrictObject> StrictDict::dict__len__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return caller.makeInt(self->data_.size());
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__getitem__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key) {
  auto got = self->data_.find(key);
  if (got == self->data_.end()) {
    if (key->getType() == UnknownType()) {
      caller.error<UnknownValueAttributeException>(
          key->getDisplayName(), "__hash__");
      return makeUnknown(
          caller, "{}[{}]", self->getDisplayName(), key->getDisplayName());
    }
    // check __missing__
    auto missingFunc =
        iLoadAttrOnType(std::move(self), "__missing__", nullptr, caller);
    if (missingFunc) {
      return iCall(missingFunc, {key}, kEmptyArgNames, caller);
    } else {
      caller.raiseExceptionStr(KeyErrorType(), "{}", key);
    }
  }
  return got->second;
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__setitem__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> value) {
  if (key->getType() == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return NoneObject();
  }
  checkExternalModification(self, caller);
  // TODO: explicitly prohibit unhashable keys and raise typeError
  self->data_[key] = value;
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__delitem__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key) {
  if (key->getType() == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return NoneObject();
  }
  checkExternalModification(self, caller);
  // TODO: explicitly prohibit unhashable keys and raise typeError
  self->data_.erase(key);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__contains__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key) {
  if (key->getType() == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return StrictFalse();
  }
  if (self->data_.find(std::move(key)) == self->data_.end()) {
    return StrictFalse();
  }
  return StrictTrue();
}

std::shared_ptr<BaseStrictObject> StrictDict::dictGet(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  if (defaultValue == nullptr) {
    defaultValue = NoneObject();
  }
  if (key->getType() == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return defaultValue;
  }
  auto got = self->data_.find(std::move(key));
  if (got == self->data_.end()) {
    return defaultValue;
  }
  return got->second;
}

std::shared_ptr<BaseStrictObject> StrictDict::dictSetDefault(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> value) {
  if (key->getType() == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return value;
  }
  auto got = self->data_.find(key);
  if (got == self->data_.end()) {
    self->data_[key] = value;
    return value;
  }
  return got->second;
}

std::shared_ptr<BaseStrictObject> StrictDict::dictCopy(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return std::make_shared<StrictDict>(
      self->type_, caller.caller, self->data_, self->displayName_);
}

std::shared_ptr<BaseStrictObject> StrictDict::dictPop(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  checkExternalModification(self, caller);
  auto got = self->data_.find(key);
  if (got == self->data_.end()) {
    if (!defaultValue) {
      caller.raiseException(KeyErrorType());
    } else {
      return defaultValue;
    }
  }
  auto result = std::shared_ptr(got->second);
  self->data_.erase(got);
  return result;
}

std::shared_ptr<BaseStrictObject> StrictDict::dictKeys(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return std::make_shared<StrictDictView>(
      DictViewType(), caller.caller, std::move(self), StrictDictView::kKey);
}

std::shared_ptr<BaseStrictObject> StrictDict::dictValues(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return std::make_shared<StrictDictView>(
      DictViewType(), caller.caller, std::move(self), StrictDictView::kValue);
}

std::shared_ptr<BaseStrictObject> StrictDict::dictItems(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return std::make_shared<StrictDictView>(
      DictViewType(), caller.caller, std::move(self), StrictDictView::kItem);
}

std::shared_ptr<StrictIteratorBase> StrictDictType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  auto vec = StrictDictType::getElementsVec(std::move(obj), caller);
  auto list =
      std::make_shared<StrictList>(ListType(), caller.caller, std::move(vec));
  return std::make_shared<StrictSequenceIterator>(
      SequenceIteratorType(), caller.caller, std::move(list));
}

std::vector<std::shared_ptr<BaseStrictObject>> StrictDictType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext&) {
  auto dict = assertStaticCast<StrictDict>(std::move(obj));
  const DictDataT& data = dict->getData();
  std::vector<std::shared_ptr<BaseStrictObject>> vec;
  vec.reserve(data.size());
  for (auto& e : data) {
    vec.push_back(e.first);
  }
  return vec;
}

void StrictDictType::addMethods() {
  addMethod(kDunderLen, StrictDict::dict__len__);
  addMethod(kDunderGetItem, StrictDict::dict__getitem__);
  addMethod(kDunderSetItem, StrictDict::dict__setitem__);
  addMethod(kDunderDelItem, StrictDict::dict__delitem__);
  addMethod(kDunderContains, StrictDict::dict__contains__);

  addMethodDefault("get", StrictDict::dictGet, nullptr);
  addMethod("setdefault", StrictDict::dictSetDefault);
  addMethod("copy", StrictDict::dictCopy);
  addMethodDefault("pop", StrictDict::dictPop, nullptr);
  addMethod("keys", StrictDict::dictKeys);
  addMethod("values", StrictDict::dictValues);
  addMethod("items", StrictDict::dictItems);
}

// StrictDictView
std::string kViewNames[] = {"dict_keys", "dict_values", "dict_items"};

StrictDictView::StrictDictView(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictDict> viewedObj,
    ViewKind kind)
    : StrictInstance(std::move(type), std::move(creator)),
      viewedObj_(std::move(viewedObj)),
      kind_(kind) {}

StrictDictView::StrictDictView(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictDict> viewedObj,
    ViewKind kind)
    : StrictInstance(std::move(type), std::move(creator)),
      viewedObj_(std::move(viewedObj)),
      kind_(kind) {}

std::string StrictDictView::getDisplayName() const {
  return fmt::format(
      "{}({})",
      kViewNames[kind_],
      std::static_pointer_cast<BaseStrictObject>(viewedObj_));
}

// wrapped method
std::shared_ptr<BaseStrictObject> StrictDictView::dictview__len__(
    std::shared_ptr<StrictDictView> self,
    const CallerContext& caller) {
  return StrictDict::dict__len__(self->viewedObj_, caller);
}

std::vector<std::shared_ptr<BaseStrictObject>> dictViewGetElementsHelper(
    const std::shared_ptr<StrictDictView>& self,
    const CallerContext& caller) {
  const DictDataT& data = self->getViewed()->getData();
  std::vector<std::shared_ptr<BaseStrictObject>> result;
  result.reserve(data.size());
  switch (self->getViewKind()) {
    case StrictDictView::kKey: {
      for (auto& e : data) {
        result.push_back(e.first);
      }
      break;
    }
    case StrictDictView::kValue: {
      for (auto& e : data) {
        result.push_back(e.second);
      }
      break;
    }
    case StrictDictView::kItem: {
      for (auto& e : data) {
        result.push_back(caller.makePair(e.first, e.second));
      }
      break;
    }
  }
  return result;
}

std::shared_ptr<BaseStrictObject> StrictDictView::dictview__iter__(
    std::shared_ptr<StrictDictView> self,
    const CallerContext& caller) {
  auto list = std::make_shared<StrictList>(
      ListType(),
      caller.caller,
      dictViewGetElementsHelper(std::move(self), caller));
  return std::make_shared<StrictSequenceIterator>(
      SequenceIteratorType(), caller.caller, std::move(list));
}

void StrictDictViewType::addMethods() {
  addMethod(kDunderLen, StrictDictView::dictview__len__);
  addMethod(kDunderIter, StrictDictView::dictview__iter__);
}

std::shared_ptr<StrictIteratorBase> StrictDictViewType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  std::shared_ptr<StrictDictView> self =
      assertStaticCast<StrictDictView>(std::move(obj));

  auto list = std::make_shared<StrictList>(
      ListType(),
      caller.caller,
      dictViewGetElementsHelper(std::move(self), caller));

  return std::make_shared<StrictSequenceIterator>(
      SequenceIteratorType(), caller.caller, std::move(list));
}

std::vector<std::shared_ptr<BaseStrictObject>>
StrictDictViewType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  std::shared_ptr<StrictDictView> self =
      assertStaticCast<StrictDictView>(std::move(obj));
  return dictViewGetElementsHelper(self, caller);
}

} // namespace strictmod::objects
