// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/iterable_objects.h"
#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"

#include <fmt/format.h>
namespace strictmod::objects {

// -------------------------Iterable-------------------------

// wrapped methods
static inline bool strictIterableContainsHelper(
    const std::vector<std::shared_ptr<BaseStrictObject>>& data,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> item) {
  for (auto& elem : data) {
    if (iStrictObjectEq(item, elem, caller)) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<StrictType> StrictIterableType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictIterableType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictIterableType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictIterableType));
  return baseVec;
}

// -------------------------Sequence (random access)-------------------------
StrictSequence::StrictSequence(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)) {}

StrictSequence::StrictSequence(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)) {}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictSequence::sequence__contains__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> item) {
  return strictIterableContainsHelper(self->data_, caller, std::move(item))
      ? StrictTrue()
      : StrictFalse();
}

std::shared_ptr<BaseStrictObject> StrictSequence::sequence__len__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller) {
  return caller.makeInt(self->data_.size());
}

std::shared_ptr<BaseStrictObject> StrictSequence::sequence__iter__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller) {
  return std::make_shared<StrictSequenceIterator>(
      SequenceIteratorType(), caller.caller, std::move(self));
}

std::shared_ptr<BaseStrictObject> StrictSequence::sequence__eq__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  // Type are in practice singletons, so we just compare the address
  if (self->getType() != rhs->getType()) {
    return StrictFalse();
  }
  std::shared_ptr<StrictSequence> rhsSeq =
      std::dynamic_pointer_cast<StrictSequence>(rhs);
  if (rhsSeq == nullptr) {
    return StrictFalse();
  }
  if (self->data_.size() != rhsSeq->data_.size()) {
    return StrictFalse();
  }
  for (size_t i = 0; i < self->data_.size(); ++i) {
    if (!iStrictObjectEq(self->data_[i], rhsSeq->data_[i], caller)) {
      return StrictFalse();
    }
  }
  return StrictTrue();
}

std::shared_ptr<BaseStrictObject> StrictSequence::sequence__add__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  if (self->getType() != rhs->getType()) {
    return NotImplemented();
  }
  std::shared_ptr<StrictSequence> rhsSeq =
      std::dynamic_pointer_cast<StrictSequence>(rhs);
  if (rhsSeq == nullptr) {
    return NotImplemented();
  }
  std::vector<std::shared_ptr<BaseStrictObject>> newData(self->data_);
  newData.insert(newData.end(), rhsSeq->data_.begin(), rhsSeq->data_.end());
  return self->makeSequence(self->getType(), caller.caller, std::move(newData));
}

static std::shared_ptr<BaseStrictObject> sequenceMulHelper(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> other) {
  std::shared_ptr<StrictInt> multFactor =
      std::dynamic_pointer_cast<StrictInt>(other);
  if (multFactor == nullptr) {
    return NotImplemented();
  }
  std::vector<std::shared_ptr<BaseStrictObject>> result;
  auto& data = self->getData();
  long repeat = multFactor->getValue();
  result.reserve(data.size() * repeat);
  for (int i = 0; i < repeat; ++i) {
    result.insert(result.end(), data.begin(), data.end());
  }
  return self->makeSequence(self->getType(), caller.caller, std::move(result));
}

std::shared_ptr<BaseStrictObject> StrictSequence::sequence__mul__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  return sequenceMulHelper(std::move(self), caller, std::move(rhs));
}

std::shared_ptr<BaseStrictObject> StrictSequence::sequence__rmul__(
    std::shared_ptr<StrictSequence> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  return sequenceMulHelper(std::move(self), caller, std::move(lhs));
}

// TODO: __iter__, __reversed__

static inline int normalizeIndex(int index, int size) {
  return index < 0 ? index + size : index;
}

// TODO also provide __getitem__, and call it here
std::shared_ptr<BaseStrictObject> StrictSequenceType::getElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  auto seq = assertStaticCast<StrictSequence>(obj);
  auto& data = seq->getData();
  // TODO handle slice
  std::shared_ptr<StrictInt> intIndex =
      std::dynamic_pointer_cast<StrictInt>(index);
  if (intIndex != nullptr) {
    int idx = normalizeIndex(intIndex->getValue(), data.size());
    if (idx >= 0 && (size_t)idx < data.size()) {
      return data[idx];
    } else {
      caller.raiseTypeError(
          "{} index out of range: {}", seq->getTypeRef().getName(), idx);
    }
  }
  caller.raiseTypeError(
      "{} indices must be integers or slices, not {}",
      seq->getTypeRef().getName(),
      index->getTypeRef().getName());
}

std::shared_ptr<StrictIteratorBase> StrictSequenceType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  auto seq = assertStaticCast<StrictSequence>(obj);
  return std::make_shared<StrictSequenceIterator>(
      SequenceIteratorType(), caller.caller, std::move(seq));
}

std::vector<std::shared_ptr<BaseStrictObject>>
StrictSequenceType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext&) {
  auto seq = assertStaticCast<StrictSequence>(obj);
  return seq->getData();
}

std::shared_ptr<StrictType> StrictSequenceType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictSequenceType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictSequenceType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictIterableType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictSequenceType));
  return baseVec;
}

void StrictSequenceType::addMethods() {
  StrictIterableType::addMethods();
  addMethod(kDunderContains, StrictSequence::sequence__contains__);
  addMethod(kDunderLen, StrictSequence::sequence__len__);
  addMethod("__eq__", StrictSequence::sequence__eq__);
  addMethod("__add__", StrictSequence::sequence__add__);
  addMethod("__mul__", StrictSequence::sequence__mul__);
  addMethod("__rmul__", StrictSequence::sequence__rmul__);
  addMethod("__iter__", StrictSequence::sequence__iter__);
}

// -------------------------List-------------------------
std::shared_ptr<StrictSequence> StrictList::makeSequence(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data) {
  return std::make_shared<StrictList>(
      std::move(type), std::move(creator), std::move(data));
}

std::string StrictList::getDisplayName() const {
  return fmt::format("[{}]", fmt::join(data_, ","));
}

Ref<> StrictList::getPyObject() const {
  Ref<> pyObj = Ref<>::steal(PyList_New(data_.size()));

  if (pyObj == nullptr) {
    // allocation failed
    return nullptr;
  }
  for (size_t i = 0; i < data_.size(); ++i) {
    Ref<> elem = data_[i]->getPyObject();
    if (elem == nullptr) {
      return nullptr;
    }
    // elem reference is stolen into the list
    // Thus, it's no longer managed by the Ref elem
    PyList_SET_ITEM(pyObj.get(), i, elem.get());
    elem.release();
  }
  return pyObj;
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictList::listAppend(
    std::shared_ptr<StrictList> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> elem) {
  checkExternalModification(self, caller);
  self->data_.push_back(std::move(elem));
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictList::listCopy(
    std::shared_ptr<StrictList> self,
    const CallerContext& caller) {
  return std::make_shared<StrictList>(ListType(), caller.caller, self->data_);
}

std::shared_ptr<BaseStrictObject> StrictList::list__init__(
    std::shared_ptr<StrictList> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable) {
  if (iterable != nullptr) {
    self->data_ = iGetElementsVec(std::move(iterable), caller);
  }
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictList::listExtend(
    std::shared_ptr<StrictList> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable) {
  if (iterable != nullptr) {
    auto newVec = iGetElementsVec(std::move(iterable), caller);
    self->data_.insert(
        self->data_.end(),
        std::make_move_iterator(newVec.begin()),
        std::make_move_iterator(newVec.end()));
  }
  return NoneObject();
}

void StrictListType::setElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  checkExternalModification(obj, caller);
  auto list = assertStaticCast<StrictList>(obj);
  // TODO handle slice
  std::shared_ptr<StrictInt> intIndex =
      std::dynamic_pointer_cast<StrictInt>(index);
  if (intIndex != nullptr) {
    auto& data = list->getData();
    int idx = normalizeIndex(intIndex->getValue(), data.size());
    if (idx >= 0 && (size_t)idx < data.size()) {
      list->setData(idx, std::move(value));
    } else {
      caller.raiseTypeError("list assignment index out of range: {}", idx);
    }
  }
  caller.raiseTypeError(
      "list indices must be integers or slices, not {}",
      index->getTypeRef().getName());
}

std::unique_ptr<BaseStrictObject> StrictListType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictList>(ListType(), caller, kEmptyArgs);
}

Ref<> StrictListType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyList_Type));
}

std::shared_ptr<StrictType> StrictListType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictListType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictListType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictSequenceType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictListType));
  return baseVec;
}

void StrictListType::addMethods() {
  StrictSequenceType::addMethods();
  addMethod("append", StrictList::listAppend);
  addMethod("copy", StrictList::listCopy);
  addMethodDefault("__init__", StrictList::list__init__, nullptr);
  addMethod("extend", StrictList::listExtend);
}

// -------------------------Tuple-------------------------
StrictTuple::StrictTuple(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data)
    : StrictSequence(std::move(type), std::move(creator), std::move(data)),
      pyObj_(nullptr),
      displayName_() {}

StrictTuple::StrictTuple(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data)
    : StrictSequence(std::move(type), std::move(creator), std::move(data)),
      pyObj_(nullptr),
      displayName_() {}

std::shared_ptr<StrictSequence> StrictTuple::makeSequence(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data) {
  return std::make_shared<StrictTuple>(
      std::move(type), std::move(creator), std::move(data));
}

bool StrictTuple::isHashable() const {
  for (auto& e : data_) {
    if (!e->isHashable()) {
      return false;
    }
  }
  return true;
}

size_t StrictTuple::hash() const {
  size_t h = data_.size();
  // taken from boost.hash_combine
  for (auto& e : data_) {
    h ^= e->hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
  }
  return h;
}

bool StrictTuple::eq(const BaseStrictObject& other) const {
  if (&other.getTypeRef() != type_.get()) {
    return false;
  }
  const StrictTuple& otherTuple = static_cast<const StrictTuple&>(other);
  if (data_.size() != otherTuple.data_.size()) {
    return false;
  }
  for (size_t i = 0; i < data_.size(); ++i) {
    const auto& dataI = data_[i];
    const auto& otherI = otherTuple.data_[i];
    if (!dataI->eq(*otherI) && !otherI->eq(*dataI)) {
      return false;
    }
  }
  return true;
}

std::string StrictTuple::getDisplayName() const {
  if (displayName_.empty()) {
    displayName_ = fmt::format("({})", fmt::join(data_, ","));
  }
  return displayName_;
}

Ref<> StrictTuple::getPyObject() const {
  // We can cache the PyObject since tuple is immutable
  if (pyObj_ == nullptr) {
    pyObj_ = Ref<>::steal(PyTuple_New(data_.size()));
    if (pyObj_ == nullptr) {
      // allocation failed
      return nullptr;
    }
    for (size_t i = 0; i < data_.size(); ++i) {
      Ref<> elem = data_[i]->getPyObject();
      if (elem == nullptr) {
        return nullptr;
      }
      // elem reference is stolen into the tuple
      PyTuple_SET_ITEM(pyObj_.get(), i, elem);
      elem.release();
    }
  }
  return Ref<>(pyObj_.get());
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictTuple::tupleIndex(
    std::shared_ptr<StrictTuple> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> item) {
  const auto& data = self->data_;
  for (size_t i = 0; i < data.size(); ++i) {
    if (iStrictObjectEq(item, data[i], caller)) {
      return caller.makeInt(i);
    }
  }
  caller.raiseExceptionStr(ValueErrorType(), "tuple.index(x): x not in tuple");
}

std::shared_ptr<BaseStrictObject> StrictTuple::tuple__new__(
    std::shared_ptr<StrictTuple>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> instType,
    std::shared_ptr<BaseStrictObject> elements) {
  std::shared_ptr<StrictTupleType> tType =
      std::dynamic_pointer_cast<StrictTupleType>(instType);
  if (tType == nullptr) {
    caller.raiseExceptionStr(
        TypeErrorType(), "X is not a tuple type object ({})", instType);
  }
  if (elements == nullptr) {
    // empty tuple
    return std::make_shared<StrictTuple>(tType, caller.caller, kEmptyArgs);
  }
  return std::make_shared<StrictTuple>(
      tType, caller.caller, iGetElementsVec(std::move(elements), caller));
}

std::unique_ptr<BaseStrictObject> StrictTupleType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictTuple>(
      TupleType(), std::move(caller), kEmptyArgs);
}

Ref<> StrictTupleType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyTuple_Type));
}

std::shared_ptr<StrictType> StrictTupleType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictTupleType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictTupleType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictSequenceType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictTupleType));
  return baseVec;
}

void StrictTupleType::addMethods() {
  StrictSequenceType::addMethods();
  addMethod("index", StrictTuple::tupleIndex);
  addStaticMethodDefault("__new__", StrictTuple::tuple__new__, nullptr);
}

// -------------------------Set Like-------------------------
StrictSetLike::StrictSetLike(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    SetDataT data)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)) {}

StrictSetLike::StrictSetLike(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    SetDataT data)
    : StrictIterable(std::move(type), std::move(creator)),
      data_(std::move(data)) {}

void StrictSetLike::addElement(
    const CallerContext&,
    std::shared_ptr<BaseStrictObject> element) {
  data_.insert(element);
}

static bool strictSetLikeContainsHelper(
    const SetDataT& data,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject> obj) {
  auto got = data.find(std::move(obj));
  return got != data.end();
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictSetLike::set__contains__(
    std::shared_ptr<StrictSetLike> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> element) {
  if (strictSetLikeContainsHelper(
          self->getData(), caller, std::move(element))) {
    return StrictTrue();
  }
  return StrictFalse();
}

std::shared_ptr<BaseStrictObject> StrictSetLike::set__len__(
    std::shared_ptr<StrictSetLike> self,
    const CallerContext& caller) {
  return caller.makeInt(self->getData().size());
}

std::shared_ptr<BaseStrictObject> StrictSetLike::set__and__(
    std::shared_ptr<StrictSetLike> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsSetLike = std::dynamic_pointer_cast<StrictSetLike>(rhs);
  if (rhsSetLike != nullptr) {
    auto& rhsData = rhsSetLike->getData();
    SetDataT newData;
    for (auto& elem : rhsData) {
      if (strictSetLikeContainsHelper(self->data_, caller, elem)) {
        newData.insert(elem);
      }
    }
    return self->makeSetLike(self->type_, caller.caller, std::move(newData));
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictSetLike::set__or__(
    std::shared_ptr<StrictSetLike> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsSetLike = std::dynamic_pointer_cast<StrictSetLike>(rhs);
  if (rhsSetLike != nullptr) {
    auto& rhsData = rhsSetLike->getData();
    SetDataT newData(self->data_);
    for (auto& elem : rhsData) {
      if (!strictSetLikeContainsHelper(newData, caller, elem)) {
        newData.insert(elem);
      }
    }
    return self->makeSetLike(self->type_, caller.caller, std::move(newData));
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictSetLike::set__xor__(
    std::shared_ptr<StrictSetLike> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsSetLike = std::dynamic_pointer_cast<StrictSetLike>(rhs);
  if (rhsSetLike != nullptr) {
    auto& rhsData = rhsSetLike->getData();
    SetDataT newData;
    for (auto& elem : rhsData) {
      if (!strictSetLikeContainsHelper(self->data_, caller, elem)) {
        newData.insert(elem);
      }
    }
    for (auto& elem : self->data_) {
      if (!strictSetLikeContainsHelper(rhsData, caller, elem)) {
        newData.insert(elem);
      }
    }
    return self->makeSetLike(self->type_, caller.caller, std::move(newData));
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictSetLike::set__iter__(
    std::shared_ptr<StrictSetLike> self,
    const CallerContext& caller) {
  return std::make_shared<StrictSetIterator>(
      SetIteratorType(), caller.caller, std::move(self));
}
// TODO issubset, issuperset, __le__, __lt__,
// __ge__, __gt__

void StrictSetLikeType::addMethods() {
  addMethod(kDunderContains, StrictSetLike::set__contains__);
  addMethod("__len__", StrictSetLike::set__len__);
  addMethod("__and__", StrictSetLike::set__and__);
  addMethod("__or__", StrictSetLike::set__or__);
  addMethod("__xor__", StrictSetLike::set__xor__);
  addMethod(kDunderIter, StrictSetLike::set__iter__);
}

std::shared_ptr<StrictType> StrictSetLikeType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictSetLikeType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictSetLikeType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictSetLikeType));
  return baseVec;
}

std::shared_ptr<StrictIteratorBase> StrictSetLikeType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  auto set = assertStaticCast<StrictSetLike>(obj);
  return std::make_shared<StrictSetIterator>(
      SetIteratorType(), caller.caller, std::move(set));
}

std::vector<std::shared_ptr<BaseStrictObject>>
StrictSetLikeType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext&) {
  auto seq = assertStaticCast<StrictSetLike>(obj);
  auto& data = seq->getData();
  return std::vector<std::shared_ptr<BaseStrictObject>>(
      data.begin(), data.end());
}

// -------------------------Set-------------------------
std::string StrictSet::getDisplayName() const {
  if (data_.empty()) {
    return "set()";
  }
  return fmt::format("{{{}}}", fmt::join(data_, ","));
}

Ref<> StrictSet::getPyObject() const {
  // this give empty set
  Ref<> pyObj = Ref<>::steal(PySet_New(nullptr));
  if (pyObj == nullptr) {
    // allocation failed
    return nullptr;
  }
  for (auto& v : data_) {
    Ref<> elem = v->getPyObject();
    if (elem == nullptr) {
      return nullptr;
    }
    // set keeps its own reference to elem
    if (PySet_Add(pyObj, elem) < 0) {
      PyErr_Clear();
      return nullptr;
    }
  }
  return pyObj;
}

std::shared_ptr<StrictSetLike> StrictSet::makeSetLike(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    SetDataT data) {
  return std::make_shared<StrictSet>(
      std::move(type), std::move(creator), std::move(data));
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictSet::setAdd(
    std::shared_ptr<StrictSet> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> item) {
  checkExternalModification(self, caller);
  if (!strictSetLikeContainsHelper(self->data_, caller, item)) {
    self->data_.insert(std::move(item));
  }
  return NoneObject();
}
// TODO  __init__, update,

std::unique_ptr<BaseStrictObject> StrictSetType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictSet>(SetType(), caller);
}

std::shared_ptr<StrictType> StrictSetType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictSetType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictSetType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictSetLikeType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictSetType));
  return baseVec;
}

void StrictSetType::addMethods() {
  StrictSetLikeType::addMethods();
  addMethod("add", StrictSet::setAdd);
}

// -------------------------FrozenSet-------------------------
StrictFrozenSet::StrictFrozenSet(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    SetDataT data)
    : StrictSetLike(std::move(type), std::move(creator), std::move(data)),
      pyObj_(nullptr),
      displayName_() {}

StrictFrozenSet::StrictFrozenSet(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    SetDataT data)
    : StrictSetLike(std::move(type), std::move(creator), std::move(data)),
      pyObj_(nullptr),
      displayName_() {}

std::string StrictFrozenSet::getDisplayName() const {
  if (displayName_.empty()) {
    if (data_.empty()) {
      displayName_ = "frozenset()";
    }
    displayName_ = fmt::format("frozenset({{{}}})", fmt::join(data_, ","));
  }
  return displayName_;
}

Ref<> StrictFrozenSet::getPyObject() const {
  if (pyObj_ == nullptr) {
    pyObj_ = Ref<>::steal(PyFrozenSet_New(nullptr));
    if (pyObj_ == nullptr) {
      return nullptr;
    }
    for (auto& v : data_) {
      Ref<> elem = v->getPyObject();
      if (elem == nullptr) {
        return nullptr;
      }
      // set keeps its own reference to elem
      if (PySet_Add(pyObj_, elem) < 0) {
        PyErr_Clear();
        return nullptr;
      }
    }
  }
  return Ref<>(pyObj_.get());
}

std::shared_ptr<StrictSetLike> StrictFrozenSet::makeSetLike(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    SetDataT data) {
  return std::make_shared<StrictFrozenSet>(
      std::move(type), std::move(creator), std::move(data));
}

// wrapped methods
// TODO __new__,

std::unique_ptr<BaseStrictObject> StrictFrozenSetType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictFrozenSet>(FrozensetType(), caller);
}

std::shared_ptr<StrictType> StrictFrozenSetType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictFrozenSetType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictFrozenSetType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictSetLikeType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictFrozenSetType));
  return baseVec;
}

void StrictFrozenSetType::addMethods() {
  StrictSetLikeType::addMethods();
}

} // namespace strictmod::objects
