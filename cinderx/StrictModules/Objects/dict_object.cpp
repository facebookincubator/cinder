// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/dict_object.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"
#include "cinderx/StrictModules/exceptions.h"
#include "cinderx/StrictModules/sequence_map.h"

namespace strictmod::objects {

// StrictDict
StrictDict::StrictDict(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    DictDataT data,
    std::string displayName)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::make_unique<DirectMapDictData>(std::move(data))),
      displayName_(displayName) {}

StrictDict::StrictDict(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    DictDataT data,
    std::string displayName)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::make_unique<DirectMapDictData>(std::move(data))),
      displayName_(displayName) {}

StrictDict::StrictDict(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::unique_ptr<DictDataInterface> data,
    std::string displayName)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)),
      displayName_(displayName) {}

StrictDict::StrictDict(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    std::unique_ptr<DictDataInterface> data,
    std::string displayName)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)),
      displayName_(displayName) {}

std::string StrictDict::getDisplayName() const {
  if (displayName_.empty()) {
    std::vector<std::string> items;
    items.reserve(data_->size());
    data_->const_iter([&items](
                          std::shared_ptr<BaseStrictObject> key,
                          std::shared_ptr<BaseStrictObject> value) {
      items.push_back(fmt::format("{}: {}", std::move(key), std::move(value)));
      return true;
    });
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
  bool success = true;
  data_->const_iter([&pyObj, &success](
                        std::shared_ptr<BaseStrictObject> k,
                        std::shared_ptr<BaseStrictObject> v) {
    Ref<> key = k->getPyObject();
    if (key == nullptr) {
      success = false;
      return false;
    }
    Ref<> value = v->getPyObject();
    if (value == nullptr) {
      success = false;
      return false;
    }
    if (PyDict_SetItem(pyObj.get(), key.get(), value.get()) < 0) {
      PyErr_Clear();
      success = false;
      return false;
    }
    return true;
  });
  if (success) {
    return pyObj;
  }
  return nullptr;
}

void StrictDict::cleanContent(const StrictModuleObject* owner) {
  if ((!creator_.expired() && owner != creator_.lock().get())) {
    return;
  }
  data_->clear();
  StrictIterable::cleanContent(owner);
}

// wrapped method
void StrictDict::dictUpdateHelper(
    std::shared_ptr<StrictDict> self,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    bool noPosArg,
    const CallerContext& caller) {
  if (!noPosArg) {
    std::shared_ptr<BaseStrictObject> posArg = args[0];
    std::shared_ptr<StrictDict> posDict =
        std::dynamic_pointer_cast<StrictDict>(posArg);
    if (posDict) {
      self->data_->insert(posDict->getData());
    } else {
      for (auto& elem : iGetElementsVec(posArg, caller)) {
        std::vector<std::shared_ptr<BaseStrictObject>> kvTuple =
            iGetElementsVec(elem, caller);
        if (kvTuple.size() != 2) {
          caller.raiseTypeError(
              "dict update argument has size {} but should be size 2",
              kvTuple.size());
        }
        self->data_->set(kvTuple[0], kvTuple[1]);
      }
    }
  }
  // process kwargs
  int offset = noPosArg ? 0 : 1;
  for (size_t i = 0; i < namedArgs.size(); ++i) {
    auto key = caller.makeStr(namedArgs[i]);
    self->data_->set(std::move(key), args[i + offset]);
  }
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__init__(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  int posArgNum = args.size() - namedArgs.size();
  if (posArgNum < 0 || posArgNum > 1) {
    caller.raiseTypeError(
        "dict.__init__() takes {} positional arguments but {} were given",
        1,
        posArgNum);
  }
  std::shared_ptr<StrictDict> self = assertStaticCast<StrictDict>(obj);
  checkExternalModification(self, caller);
  self->data_->clear();
  dictUpdateHelper(std::move(self), args, namedArgs, posArgNum == 0, caller);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictDict::dictUpdate(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  int posArgNum = args.size() - namedArgs.size();
  if (posArgNum < 0 || posArgNum > 1) {
    caller.raiseTypeError(
        "dict.update() takes {} positional arguments but {} were given",
        1,
        posArgNum);
  }
  std::shared_ptr<StrictDict> self = assertStaticCast<StrictDict>(obj);
  checkExternalModification(self, caller);
  dictUpdateHelper(std::move(self), args, namedArgs, posArgNum == 0, caller);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__len__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return caller.makeInt(self->data_->size());
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__getitem__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key) {
  auto got = self->data_->get(key);
  if (got == std::nullopt) {
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
  return got.value();
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__setitem__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> value) {
  auto keyType = key->getType();
  if (keyType == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return NoneObject();
  }
  checkExternalModification(self, caller);

  bool success = self->data_->set(std::move(key), std::move(value));
  if (!success) {
    caller.error<UnsupportedException>(
        fmt::format("{}.__setitem__", self->getDisplayName()),
        keyType->getName());
  }
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictDict::dict__delitem__(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key) {
  auto keyType = key->getType();
  if (keyType == UnknownType()) {
    caller.error<UnknownValueAttributeException>(
        key->getDisplayName(), "__hash__");
    return NoneObject();
  }
  checkExternalModification(self, caller);

  bool success = self->data_->erase(key);
  if (!success) {
    caller.error<UnsupportedException>(
        fmt::format("{}.__delitem__", self->getDisplayName()),
        keyType->getName());
  }
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
  if (self->data_->contains(std::move(key))) {
    return StrictTrue();
  }
  return StrictFalse();
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
  auto got = self->data_->get(std::move(key));
  if (got == std::nullopt) {
    return defaultValue;
  }
  return got.value();
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
  checkExternalModification(self, caller);
  auto got = self->data_->get(key);
  if (got == std::nullopt) {
    self->data_->set(std::move(key), value);
    return value;
  }
  return got.value();
}

std::shared_ptr<BaseStrictObject> StrictDict::dictCopy(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller) {
  return std::make_shared<StrictDict>(
      self->type_, caller.caller, self->data_->copy(), self->displayName_);
}

std::shared_ptr<BaseStrictObject> StrictDict::dictPop(
    std::shared_ptr<StrictDict> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  checkExternalModification(self, caller);
  auto got = self->data_->get(key);
  if (got == std::nullopt) {
    if (!defaultValue) {
      caller.raiseException(KeyErrorType());
    } else {
      return defaultValue;
    }
  }
  auto result = std::shared_ptr(got.value());
  self->data_->erase(key);
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
  const DictDataInterface& data = dict->getData();
  std::vector<std::shared_ptr<BaseStrictObject>> vec;
  vec.reserve(data.size());
  data.const_iter([&vec](
                      std::shared_ptr<BaseStrictObject> k,
                      std::shared_ptr<BaseStrictObject>) {
    vec.push_back(std::move(k));
    return true;
  });

  return vec;
}

void StrictDictType::addMethods() {
  StrictIterableType::addMethods();
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

  addMethodDescr(kDunderInit, StrictDict::dict__init__);
  addMethodDescr("update", StrictDict::dictUpdate);

  addPyWrappedMethodObj<>(
      kDunderRepr,
      reinterpret_cast<PyObject*>(&PyDict_Type),
      StrictString::strFromPyObj);

  addMethod(kDunderClassGetItem, createGenericAlias);
}

std::unique_ptr<BaseStrictObject> StrictDictType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictDict>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller));
}

std::shared_ptr<StrictType> StrictDictType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictDictType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictDictType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictDictType));
  return baseVec;
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
  const DictDataInterface& data = self->getViewed()->getData();
  std::vector<std::shared_ptr<BaseStrictObject>> result;
  result.reserve(data.size());
  switch (self->getViewKind()) {
    case StrictDictView::kKey: {
      data.const_iter([&result](
                          std::shared_ptr<BaseStrictObject> k,
                          std::shared_ptr<BaseStrictObject>) {
        result.push_back(std::move(k));
        return true;
      });
      break;
    }
    case StrictDictView::kValue: {
      data.const_iter([&result](
                          std::shared_ptr<BaseStrictObject>,
                          std::shared_ptr<BaseStrictObject> v) {
        result.push_back(std::move(v));
        return true;
      });
      break;
    }
    case StrictDictView::kItem: {
      data.const_iter([&result, &caller](
                          std::shared_ptr<BaseStrictObject> k,
                          std::shared_ptr<BaseStrictObject> v) {
        result.push_back(caller.makePair(std::move(k), std::move(v)));
        return true;
      });
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

std::shared_ptr<StrictType> StrictDictViewType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictDictViewType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictDictViewType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictDictViewType));
  return baseVec;
}

// DirectMapDictData
bool DirectMapDictData::set(
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> value) {
  data_[std::move(key)] = std::move(value);
  return true;
}

std::optional<std::shared_ptr<BaseStrictObject>> DirectMapDictData::get(
    const std::shared_ptr<BaseStrictObject>& key) {
  auto item = data_.find(key);
  if (item == data_.map_end()) {
    return std::nullopt;
  }
  return item->second.first;
}

bool DirectMapDictData::contains(
    const std::shared_ptr<BaseStrictObject>& key) const {
  return data_.find(key) != data_.map_end();
}

std::size_t DirectMapDictData::size() const {
  return data_.size();
}

bool DirectMapDictData::erase(const std::shared_ptr<BaseStrictObject>& key) {
  std::size_t erased = data_.erase(key);
  return erased > 0;
}

void DirectMapDictData::clear() {
  data_.clear();
}

void DirectMapDictData::insert(const DictDataInterface& other) {
  other.const_iter([&data = data_](
                       std::shared_ptr<BaseStrictObject> k,
                       std::shared_ptr<BaseStrictObject> v) {
    data[std::move(k)] = std::move(v);
    return true;
  });
}

std::unique_ptr<DictDataInterface> DirectMapDictData::copy() {
  return std::make_unique<DirectMapDictData>(data_);
}

/* iterate on items in the dict, and call func on each item
 * If func returns false, iteration is stopped
 */
void DirectMapDictData::iter(std::function<bool(
                                 std::shared_ptr<BaseStrictObject>,
                                 std::shared_ptr<BaseStrictObject>)> func) {
  for (auto& item : data_) {
    bool ok = func(item.first, item.second.first);
    if (!ok) {
      break;
    }
  }
}
void DirectMapDictData::const_iter(
    std::function<bool(
        std::shared_ptr<BaseStrictObject>,
        std::shared_ptr<BaseStrictObject>)> func) const {
  for (auto& item : data_) {
    bool ok = func(item.first, item.second.first);
    if (!ok) {
      break;
    }
  }
}

// InstanceDictDictData
static std::optional<std::string> keyToStr(
    const std::shared_ptr<BaseStrictObject>& key) {
  auto strKey = std::dynamic_pointer_cast<StrictString>(key);
  if (strKey) {
    return strKey->getValue();
  }
  return std::nullopt;
}

bool InstanceDictDictData::set(
    std::shared_ptr<BaseStrictObject> key,
    std::shared_ptr<BaseStrictObject> value) {
  auto keyStr = keyToStr(key);
  if (keyStr) {
    (*data_)[keyStr.value()] = std::move(value);
    return true;
  }
  return false;
}
std::optional<std::shared_ptr<BaseStrictObject>> InstanceDictDictData::get(
    const std::shared_ptr<BaseStrictObject>& key) {
  auto keyStr = keyToStr(key);
  if (keyStr) {
    auto item = data_->find(keyStr.value());
    if (item != data_->map_end()) {
      auto& result = item->second.first;
      if (result->isLazy()) {
        auto lazy = std::static_pointer_cast<StrictLazyObject>(result);
        auto evaluated = lazy->evaluate();
        item->second.first = evaluated;
      }
      return item->second.first;
    }
  }
  return std::nullopt;
}

bool InstanceDictDictData::contains(
    const std::shared_ptr<BaseStrictObject>& key) const {
  auto keyStr = keyToStr(key);
  if (keyStr) {
    return data_->find(keyStr.value()) != data_->map_end();
  }
  return false;
}

std::size_t InstanceDictDictData::size() const {
  return data_->size();
}

bool InstanceDictDictData::erase(const std::shared_ptr<BaseStrictObject>& key) {
  auto keyStr = keyToStr(key);
  if (keyStr) {
    data_->erase(keyStr.value());
    return true;
  }
  return false;
}

void InstanceDictDictData::clear() {
  data_->clear();
}

void InstanceDictDictData::insert(const DictDataInterface& other) {
  other.const_iter([&data = data_](
                       std::shared_ptr<BaseStrictObject> k,
                       std::shared_ptr<BaseStrictObject> v) {
    auto keyStr = keyToStr(k);
    if (keyStr) {
      (*data)[keyStr.value()] = std::move(v);
      return true;
    }
    return false;
  });
}

std::unique_ptr<DictDataInterface> InstanceDictDictData::copy() {
  return std::make_unique<InstanceDictDictData>(data_, creator_);
}

/* iterate on items in the dict, and call func on each item
 * If func returns false, iteration is stopped
 */
void InstanceDictDictData::iter(std::function<bool(
                                    std::shared_ptr<BaseStrictObject>,
                                    std::shared_ptr<BaseStrictObject>)> func) {
  for (auto& item : *data_) {
    auto keyObj =
        std::make_shared<StrictString>(StrType(), creator_, item.first);
    auto& valueObj = item.second.first;
    if (valueObj->isLazy()) {
      auto lazy = std::static_pointer_cast<StrictLazyObject>(valueObj);
      auto evaluated = lazy->evaluate();
      item.second.first = evaluated;
    }
    bool ok = func(std::move(keyObj), item.second.first);
    if (!ok) {
      break;
    }
  }
}

void InstanceDictDictData::const_iter(
    std::function<bool(
        std::shared_ptr<BaseStrictObject>,
        std::shared_ptr<BaseStrictObject>)> func) const {
  for (auto& item : *data_) {
    auto keyObj =
        std::make_shared<StrictString>(StrType(), creator_, item.first);
    auto& valueObj = item.second.first;
    if (valueObj->isLazy()) {
      auto lazy = std::static_pointer_cast<StrictLazyObject>(valueObj);
      auto evaluated = lazy->evaluate();
      item.second.first = evaluated;
    }
    bool ok = func(std::move(keyObj), item.second.first);
    if (!ok) {
      break;
    }
  }
}

std::shared_ptr<BaseStrictObject> StrictInstance::getDunderDict() {
  if (dictObj_ != nullptr) {
    return dictObj_;
  }

  std::unique_ptr<DictDataInterface> dict =
      std::make_unique<InstanceDictDictData>(dict_, creator_);
  dictObj_ = std::make_shared<StrictDict>(
      DictObjectType(),
      creator_,
      std::move(dict),
      fmt::format("{}.__dict__", type_->getName()));
  return dictObj_;
}

} // namespace strictmod::objects
