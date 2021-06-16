// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/string_object.h"

#include "StrictModules/Objects/objects.h"

#include "StrictModules/Objects/callable_wrapper.h"

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
  try {
    const StrictString& otherStr = dynamic_cast<const StrictString&>(other);
    return value_ == otherStr.value_;
  } catch (const std::bad_cast&) {
    return false;
  }
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

std::shared_ptr<BaseStrictObject> StrictString::str__eq__(
    std::shared_ptr<StrictString> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> other) {
  auto otherStr = std::dynamic_pointer_cast<StrictString>(other);
  if (!otherStr) {
    return StrictFalse();
  }
  return caller.makeBool(self->value_ == otherStr->value_);
}

std::shared_ptr<BaseStrictObject> StrictString::str__format__(
    std::shared_ptr<StrictString> self,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject>) {
  return self;
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
  addMethod("__eq__", StrictString::str__eq__);
  addMethod("__format__", StrictString::str__format__);
}
} // namespace strictmod::objects
