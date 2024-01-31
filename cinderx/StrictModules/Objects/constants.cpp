// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/constants.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context_impl.h"

namespace strictmod::objects {
// NoneObject_
Ref<> NoneObject_::getPyObject() const {
  return Ref<>::create(Py_None);
}
std::string NoneObject_::getDisplayName() const {
  return "None";
}
std::shared_ptr<BaseStrictObject> NoneObject_::copy(const CallerContext&) {
  return shared_from_this();
}

bool NoneObject_::eq(const BaseStrictObject& other) const {
  return typeid(other) == typeid(NoneObject_);
}

bool NoneObject_::isHashable() const {
  return true;
}

size_t NoneObject_::hash() const {
  // some random constant
  return 255347;
}

std::shared_ptr<BaseStrictObject> NoneObject_::None__bool__(
    std::shared_ptr<NoneObject_>,
    const CallerContext&) {
  return StrictFalse();
}

// NoneType_
Ref<> NoneType_::getPyObject() const {
  return Ref<>::create(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)));
}
std::string NoneType_::getDisplayName() const {
  return "NoneType";
}
std::shared_ptr<BaseStrictObject> NoneType_::getTruthValue(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  return caller.makeBool(false);
}

void NoneType_::addMethods() {
  addMethod(kDunderBool, NoneObject_::None__bool__);
  addPyWrappedMethodObj<>(
      kDunderRepr,
      reinterpret_cast<PyObject*>(&_PyNone_Type),
      StrictString::strFromPyObj);
}

// NotImplementedObject
Ref<> NotImplementedObject::getPyObject() const {
  return Ref<>::create(Py_NotImplemented);
}
std::string NotImplementedObject::getDisplayName() const {
  return "NotImplemented()";
}
std::shared_ptr<BaseStrictObject> NotImplementedObject::copy(
    const CallerContext&) {
  return shared_from_this();
}

// StrictEllipsisObject
Ref<> StrictEllipsisObject::getPyObject() const {
  return Ref<>::create(Py_Ellipsis);
}
std::string StrictEllipsisObject::getDisplayName() const {
  return "...";
}
std::shared_ptr<BaseStrictObject> StrictEllipsisObject::copy(
    const CallerContext&) {
  return shared_from_this();
}

bool StrictEllipsisObject::eq(const BaseStrictObject& other) const {
  return typeid(other) == typeid(StrictEllipsisObject);
}

bool StrictEllipsisObject::isHashable() const {
  return true;
}

size_t StrictEllipsisObject::hash() const {
  // some random constant
  return 798211021;
}

std::shared_ptr<BaseStrictObject> StrictEllipsisObject::Ellipsis__repr__(
    std::shared_ptr<StrictEllipsisObject>,
    const CallerContext& caller) {
  return caller.makeStr("Ellipsis");
}

// EllipsisType
Ref<> StrictEllipsisType::getPyObject() const {
  return Ref<>::create(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)));
}
std::string StrictEllipsisType::getDisplayName() const {
  return "<class ellipsis>";
}
std::shared_ptr<BaseStrictObject> StrictEllipsisType::getTruthValue(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  return StrictTrue();
}

void StrictEllipsisType::addMethods() {
  addMethod(kDunderRepr, StrictEllipsisObject::Ellipsis__repr__);
}
} // namespace strictmod::objects
