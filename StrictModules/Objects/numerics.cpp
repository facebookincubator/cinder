#include "StrictModules/Objects/numerics.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include "StrictModules/Objects/callable.h"
#include "StrictModules/Objects/callable_wrapper.h"

namespace strictmod::objects {
// StrictInt
StrictInt::StrictInt(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    long value)
    : StrictNumeric(std::move(type), std::move(creator)),
      value_(value),
      pyValue_(nullptr),
      displayName_() {}

StrictInt::StrictInt(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    long value)
    : StrictInt(std::move(type), std::weak_ptr(creator), value) {}

StrictInt::StrictInt(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    PyObject* pyValue // constructor will incref on the object
    )
    : StrictNumeric(std::move(type), std::move(creator)),
      value_(PyLong_AsLong(pyValue)),
      pyValue_(Ref<>(pyValue)),
      displayName_() {}

bool StrictInt::isHashable() const {
  return true;
}

size_t StrictInt::hash() const {
  return value_;
}

bool StrictInt::eq(const BaseStrictObject& other) const {
  try {
    const StrictNumeric& num = dynamic_cast<const StrictNumeric&>(other);
    return num.getReal() == value_ && num.getImaginary() == 0;
  } catch (std::bad_cast&) {
    return false;
  }
}

double StrictInt::getReal() const {
  return value_;
}
double StrictInt::getImaginary() const {
  return 0;
}

Ref<> StrictInt::getPyObject() const {
  if (pyValue_ == nullptr) {
    pyValue_ = Ref<>::steal(PyLong_FromLong(value_));
  }
  return Ref<>(pyValue_.get());
}

std::string StrictInt::getDisplayName() const {
  if (displayName_.empty()) {
    displayName_ = std::to_string(value_);
  }
  return displayName_;
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictInt::int__bool__(
    std::shared_ptr<StrictInt> self,
    const CallerContext&) {
  return self->value_ == 0 ? StrictFalse() : StrictTrue();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__eq__(
    std::shared_ptr<StrictInt> self,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject> rhs) {
  // TODO make this work with all numeric types
  auto rhsNum = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsNum) {
    return rhsNum->getValue() == self->value_ ? StrictTrue() : StrictFalse();
  }
  return NotImplemented();
}

// StrictIntType
std::unique_ptr<BaseStrictObject> StrictIntType::constructInstance(
    std::shared_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictInt>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      0);
}

Ref<> StrictIntType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyLong_Type));
}

std::shared_ptr<BaseStrictObject> StrictIntType::getTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  if (obj->getType() == IntType()) {
    return assertStaticCast<StrictInt>(obj)->getValue() == 0 ? StrictFalse()
                                                             : StrictTrue();
  }
  return StrictObjectType::getTruthValue(std::move(obj), caller);
}

void StrictIntType::addMethods() {
  addMethod(kDunderBool, StrictInt::int__bool__);
  addMethod("__eq__", StrictInt::int__eq__);
}

// StrictBool
Ref<> StrictBool::getPyObject() const {
  if (pyValue_ == nullptr) {
    pyValue_ = Ref<>::steal(PyBool_FromLong(value_));
  }
  return Ref<>(pyValue_.get());
}

std::string StrictBool::getDisplayName() const {
  if (displayName_.empty()) {
    displayName_ = value_ == 0 ? "false" : "true";
  }
  return displayName_;
}

// StrictBoolType
Ref<> StrictBoolType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyBool_Type));
}

std::unique_ptr<BaseStrictObject> StrictBoolType::constructInstance(
    std::shared_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictBool>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      0);
}

bool StrictBoolType::isBaseType() const {
  return false;
}

std::shared_ptr<BaseStrictObject> StrictBoolType::getTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext&) {
  // assert since bool has no subtypes
  assert(obj->getType() == BoolType());
  return obj;
}

void StrictBoolType::addMethods() {
  StrictIntType::addMethods();
}

} // namespace strictmod::objects
