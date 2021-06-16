// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/string_object.h"

#include "StrictModules/Objects/objects.h"

#include "StrictModules/Objects/callable_wrapper.h"

#include <iostream>

namespace strictmod::objects {
// StrictType
StrictString::StrictString(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    std::string value)
    : StrictString(
          std::move(type),
          std::weak_ptr(std::move(creator)),
          std::move(value)) {}

StrictString::StrictString(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::string value)
    : StrictInstance(std::move(type), std::move(creator)),
      pyStr_(nullptr),
      value_(value) {}

StrictString::StrictString(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    PyObject* pyValue)
    : StrictInstance(std::move(type), std::move(creator)),
      pyStr_(Ref<>(pyValue)),
      value_(PyUnicode_AsUTF8(pyValue)) {}

bool StrictString::isHashable() const {
  return true;
}

size_t StrictString::hash() const {
  return std::hash<std::string>{}(value_);
}

bool StrictString::eq(const BaseStrictObject& other) const {
  if (type_.get() != &other.getTypeRef()) {
    return false;
  }
  const StrictString& otherStr = static_cast<const StrictString&>(other);
  return value_ == otherStr.value_;
}

Ref<> StrictString::getPyObject() const {
  if (pyStr_ == nullptr) {
    pyStr_ = Ref<>::steal(PyUnicode_FromString(value_.c_str()));
  }
  return Ref<>(pyStr_.get());
}

std::string StrictString::getDisplayName() const {
  return value_;
}

std::shared_ptr<BaseStrictObject> StrictString::str__len__(
    std::shared_ptr<StrictString> self,
    const CallerContext& caller) {
  return caller.makeInt(self->value_.size());
}

// StrictStringType
std::unique_ptr<BaseStrictObject> StrictStringType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictString>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      "");
}

std::shared_ptr<StrictType> StrictStringType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictStringType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictStringType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictStringType));
  return baseVec;
}

Ref<> StrictStringType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyUnicode_Type));
}

void StrictStringType::addMethods() {
  addMethod(kDunderLen, StrictString::str__len__);
}
} // namespace strictmod::objects
