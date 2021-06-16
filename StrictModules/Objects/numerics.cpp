// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/numerics.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include "StrictModules/Objects/callable.h"
#include "StrictModules/Objects/callable_wrapper.h"

#include <cmath>

namespace strictmod::objects {

bool StrictNumeric::eq(const BaseStrictObject& other) const {
  try {
    const StrictNumeric& num = dynamic_cast<const StrictNumeric&>(other);
    return num.getReal() == getReal() && num.getImaginary() == getImaginary();
  } catch (std::bad_cast) {
    return false;
  }
}

bool StrictNumeric::isHashable() const {
  return true;
}

size_t StrictNumeric::hash() const {
  return size_t(getReal());
}

static void checkDivisionByZero(
    const std::shared_ptr<StrictNumeric>& num,
    const CallerContext& caller) {
  if (num->getReal() == 0) {
    caller.raiseExceptionStr(DivisionByZeroType(), "division by zero");
  }
}

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

std::shared_ptr<BaseStrictObject> StrictInt::int__str__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller) {
  return caller.makeStr(std::to_string(self->value_));
}

std::shared_ptr<BaseStrictObject> StrictInt::int__abs__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller) {
  return caller.makeInt(abs(self->value_));
}

std::shared_ptr<BaseStrictObject> StrictInt::int__round__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject>) {
  return caller.makeInt(self->value_);
}

std::shared_ptr<BaseStrictObject> StrictInt::int__new__(
    std::shared_ptr<StrictInt>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> instType,
    std::shared_ptr<BaseStrictObject> value) {
  auto type = std::dynamic_pointer_cast<StrictType>(std::move(instType));
  if (!type->isSubType(IntType())) {
    caller.raiseTypeError("{} is not a subtype of int", type->getName());
  }
  if (value == nullptr) {
    return std::make_shared<StrictInt>(std::move(type), caller.caller, 0);
  }
  // value is numeric
  auto num = std::dynamic_pointer_cast<StrictNumeric>(value);
  if (num && num->getImaginary() == 0) {
    return std::make_shared<StrictInt>(
        std::move(type), caller.caller, long(num->getReal()));
  }
  // value is string
  auto str = std::dynamic_pointer_cast<StrictString>(value);
  if (str) {
    try {
      long i = std::stol(str->getValue());
      return std::make_shared<StrictInt>(std::move(type), caller.caller, i);
    } catch (std::invalid_argument) {
      caller.raiseExceptionStr(
          ValueErrorType(), "'{}' cannot be converted to int", str->getValue());
    } catch (std::out_of_range) {
      caller.raiseExceptionStr(
          ValueErrorType(), "'{}' cannot be converted to int", str->getValue());
    }
  }
  // check __int__
  auto intFunc = iLoadAttrOnType(value, "__int__", nullptr, caller);
  if (intFunc) {
    return iCall(std::move(intFunc), kEmptyArgs, kEmptyArgNames, caller);
  }
  // error
  caller.error<UnsupportedException>("int", value->getTypeRef().getName());
  return makeUnknown(caller, "int({})", std::move(value));
}

std::shared_ptr<BaseStrictObject> fromPyNumberHelper(
    const CallerContext& caller,
    const Ref<>& number) {
  if (PyLong_CheckExact(number.get())) {
    return std::make_shared<StrictInt>(IntType(), caller.caller, number.get());
  } else if (PyFloat_CheckExact(number.get())) {
    return std::make_shared<StrictFloat>(
        FloatType(), caller.caller, number.get());
  }
  return nullptr;
}

std::shared_ptr<BaseStrictObject> __pow__Helper(
    std::shared_ptr<StrictNumeric> self,
    const CallerContext& caller,
    std::shared_ptr<StrictNumeric> num,
    std::shared_ptr<BaseStrictObject> mod) {
  Ref<> modObj = Ref<>(Py_None);
  if (mod != nullptr && mod != NoneObject()) {
    auto modNum = std::dynamic_pointer_cast<StrictNumeric>(mod);
    if (modNum == nullptr) {
      caller.raiseTypeError(
          "unsupported operand type for pow(): '{}', '{}', '{}'",
          self->getTypeRef().getName(),
          num->getTypeRef().getName(),
          mod->getTypeRef().getName());
    }
    modObj = modNum->getPyObject();
  }
  Ref<> selfObj = self->getPyObject();
  Ref<> rhsObj = num->getPyObject();
  Ref<> result = Ref<>::steal(PyNumber_Power(selfObj, rhsObj, Py_None));

  std::shared_ptr<BaseStrictObject> unknown;
  if (mod == nullptr) {
    unknown = makeUnknown(
        caller, "pow({}, {})", self->getDisplayName(), num->getDisplayName());
  } else {
    unknown = makeUnknown(
        caller,
        "pow({}, {}, {})",
        self->getDisplayName(),
        num->getDisplayName(),
        mod->getDisplayName());
  }

  if (result == nullptr) {
    if (PyErr_Occurred()) {
      PyErr_Clear();
      caller.raiseExceptionStr(ValueErrorType(), "Error calling pow");
    }
    return unknown;
  }
  auto strictResult = fromPyNumberHelper(caller, result);
  if (strictResult == nullptr) {
    return unknown;
  }
  return strictResult;
}

std::shared_ptr<BaseStrictObject> StrictInt::int__pow__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs,
    std::shared_ptr<BaseStrictObject> mod) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  if (num == nullptr) {
    return NotImplemented();
  }
  return __pow__Helper(std::move(self), caller, std::move(num), std::move(mod));
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rpow__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs,
    std::shared_ptr<BaseStrictObject> mod) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  if (num == nullptr) {
    return NotImplemented();
  }
  return __pow__Helper(std::move(num), caller, std::move(self), std::move(mod));
}

std::shared_ptr<BaseStrictObject> __divmod__Helper(
    std::shared_ptr<StrictNumeric> self,
    const CallerContext& caller,
    std::shared_ptr<StrictNumeric> num) {
  checkDivisionByZero(num, caller);
  Ref<> selfObj = self->getPyObject();
  Ref<> numObj = num->getPyObject();
  Ref<> result = Ref<>::steal(PyNumber_Divmod(selfObj, numObj));
  if (!PyTuple_Check(result.get()) || PyTuple_GET_SIZE(result.get()) != 2) {
    caller.raiseTypeError(
        "divmod({}, {}) did not return tuple of size 2",
        self->getDisplayName(),
        num->getDisplayName());
  }
  Ref<> fst = Ref<>(PyTuple_GET_ITEM(result.get(), 0));
  Ref<> snd = Ref<>(PyTuple_GET_ITEM(result.get(), 1));
  auto fstObj = fromPyNumberHelper(caller, fst);
  auto sndObj = fromPyNumberHelper(caller, snd);
  assert(fstObj != nullptr && sndObj != nullptr);
  return caller.makePair(std::move(fstObj), std::move(sndObj));
}

std::shared_ptr<BaseStrictObject> StrictInt::int__divmod__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  return __divmod__Helper(std::move(self), caller, std::move(num));
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rdivmod__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  return __divmod__Helper(std::move(num), caller, std::move(self));
}

std::shared_ptr<BaseStrictObject> StrictInt::int__add__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ + rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__and__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ & rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__floordiv__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  checkDivisionByZero(rhsInt, caller);
  if (rhsInt) {
    return caller.makeInt(self->value_ / rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__lshift__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ << rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__mod__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ % rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__mul__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ * rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__or__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ | rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rshift__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ >> rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__sub__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ - rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__truediv__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  checkDivisionByZero(rhsInt, caller);
  if (rhsInt) {
    return caller.makeFloat(double(self->value_) / rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__xor__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeInt(self->value_ ^ rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__radd__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ + self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rand__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ & self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rfloordiv__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  checkDivisionByZero(lhsInt, caller);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ / self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rlshift__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ << self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rmod__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ % self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rmul__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ * self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__ror__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ | self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rrshift__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ >> self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rsub__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ - self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rtruediv__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  checkDivisionByZero(lhsInt, caller);
  if (lhsInt) {
    return caller.makeFloat(double(lhsInt->value_) / self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__rxor__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto lhsInt = std::dynamic_pointer_cast<StrictInt>(lhs);
  if (lhsInt) {
    return caller.makeInt(lhsInt->value_ ^ self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__pos__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller) {
  return caller.makeInt(self->value_);
}

std::shared_ptr<BaseStrictObject> StrictInt::int__neg__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller) {
  return caller.makeInt(-self->value_);
}

std::shared_ptr<BaseStrictObject> StrictInt::int__invert__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller) {
  return caller.makeInt(~self->value_);
}

std::shared_ptr<BaseStrictObject> StrictInt::int__eq__(
    std::shared_ptr<StrictInt> self,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsNum = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsNum) {
    return rhsNum->getValue() == self->value_ ? StrictTrue() : StrictFalse();
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__ne__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeBool(self->value_ != rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__lt__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeBool(self->value_ < rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__le__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeBool(self->value_ <= rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__gt__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeBool(self->value_ > rhsInt->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictInt::int__ge__(
    std::shared_ptr<StrictInt> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto rhsInt = std::dynamic_pointer_cast<StrictInt>(rhs);
  if (rhsInt) {
    return caller.makeBool(self->value_ >= rhsInt->value_);
  }
  return NotImplemented();
}

// StrictIntType
std::unique_ptr<BaseStrictObject> StrictIntType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictInt>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      0);
}

std::shared_ptr<StrictType> StrictIntType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictIntType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictIntType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictIntType));
  return baseVec;
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
  addMethod("__add__", StrictInt::int__add__);
  addMethod("__and__", StrictInt::int__and__);
  addMethod("__floordiv__", StrictInt::int__floordiv__);
  addMethod("__lshift__", StrictInt::int__lshift__);
  addMethod("__mod__", StrictInt::int__mod__);
  addMethod("__mul__", StrictInt::int__mul__);
  addMethod("__or__", StrictInt::int__or__);
  addMethod("__rshift__", StrictInt::int__rshift__);
  addMethod("__sub__", StrictInt::int__sub__);
  addMethod("__truediv__", StrictInt::int__truediv__);
  addMethod("__xor__", StrictInt::int__xor__);

  addMethod("__radd__", StrictInt::int__radd__);
  addMethod("__rand__", StrictInt::int__rand__);
  addMethod("__rfloordiv__", StrictInt::int__rfloordiv__);
  addMethod("__rlshift__", StrictInt::int__rlshift__);
  addMethod("__rmod__", StrictInt::int__rmod__);
  addMethod("__rmul__", StrictInt::int__rmul__);
  addMethod("__ror__", StrictInt::int__ror__);
  addMethod("__rrshift__", StrictInt::int__rrshift__);
  addMethod("__rsub__", StrictInt::int__rsub__);
  addMethod("__rtruediv__", StrictInt::int__rtruediv__);
  addMethod("__rxor__", StrictInt::int__rxor__);

  addMethod("__pos__", StrictInt::int__pos__);
  addMethod("__neg__", StrictInt::int__neg__);
  addMethod("__invert__", StrictInt::int__invert__);

  addMethod("__eq__", StrictInt::int__eq__);
  addMethod("__ne__", StrictInt::int__ne__);
  addMethod("__lt__", StrictInt::int__lt__);
  addMethod("__le__", StrictInt::int__le__);
  addMethod("__gt__", StrictInt::int__gt__);
  addMethod("__ge__", StrictInt::int__ge__);

  addMethod(kDunderBool, StrictInt::int__bool__);
  addMethod(kDunderStr, StrictInt::int__str__);
  addMethod("__abs__", StrictInt::int__abs__);
  addMethodDefault("__round__", StrictInt::int__round__, nullptr);
  addStaticMethodDefault("__new__", StrictInt::int__new__, nullptr);
  addMethodDefault("__pow__", StrictInt::int__pow__, nullptr);
  addMethodDefault("__rpow__", StrictInt::int__rpow__, nullptr);
  addMethod("__divmod__", StrictInt::int__divmod__);
  addMethod("__rdivmod__", StrictInt::int__rdivmod__);

  PyObject* intType = reinterpret_cast<PyObject*>(&PyLong_Type);
  addPyWrappedMethodObj<>(kDunderRepr, intType, StrictString::strFromPyObj);
  addPyWrappedMethodObj<1>("__format__", intType, StrictString::strFromPyObj);
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

std::shared_ptr<BaseStrictObject> StrictBool::boolFromPyObj(
    Ref<> pyObj,
    const CallerContext& caller) {
  return std::make_shared<StrictBool>(BoolType(), caller.caller, pyObj.get());
}

std::shared_ptr<BaseStrictObject> StrictBool::boolOrNotImplementedFromPyObj(
    Ref<> pyObj,
    const CallerContext& caller) {
  if (pyObj.get() == Py_NotImplemented) {
    return NotImplemented();
  }
  return boolFromPyObj(std::move(pyObj), caller);
}

// StrictBoolType
Ref<> StrictBoolType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyBool_Type));
}

std::unique_ptr<BaseStrictObject> StrictBoolType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictBool>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      0);
}

std::shared_ptr<StrictType> StrictBoolType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictBoolType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictBoolType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictIntType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictBoolType));
  return baseVec;
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

  addPyWrappedMethodObj<>(
      kDunderRepr,
      reinterpret_cast<PyObject*>(&PyBool_Type),
      StrictString::strFromPyObj);
}

// Float
StrictFloat::StrictFloat(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    double value)
    : StrictNumeric(std::move(type), std::move(creator)),
      value_(value),
      pyValue_(nullptr),
      displayName_() {}

StrictFloat::StrictFloat(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    double value)
    : StrictFloat(std::move(type), std::weak_ptr(std::move(creator)), value) {}

StrictFloat::StrictFloat(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    PyObject* pyValue // constructor will incref on the object
    )
    : StrictNumeric(std::move(type), std::move(creator)),
      value_(PyFloat_AsDouble(pyValue)),
      pyValue_(Ref<>(pyValue)),
      displayName_() {}

double StrictFloat::getReal() const {
  return value_;
}
double StrictFloat::getImaginary() const {
  return 0;
}

Ref<> StrictFloat::getPyObject() const {
  if (pyValue_ == nullptr) {
    pyValue_ = Ref<>::steal(PyFloat_FromDouble(value_));
  }
  return Ref<>(pyValue_.get());
}

std::string StrictFloat::getDisplayName() const {
  if (displayName_.empty()) {
    displayName_ = std::to_string(value_);
  }
  return displayName_;
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictFloat::float__bool__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller) {
  return caller.makeBool(self->value_ != 0);
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__str__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller) {
  return caller.makeStr(std::to_string(self->value_));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__abs__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller) {
  return caller.makeFloat(abs(self->value_));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__round__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> ndigit) {
  // no C API for round, so we have to use the float method
  Ref<> selfObj = self->getPyObject();
  _Py_IDENTIFIER(__round__);
  Ref<> round =
      Ref<>::steal(_PyObject_LookupSpecial(selfObj.get(), &PyId___round__));
  if (round == nullptr) {
    if (PyErr_Occurred()) {
      PyErr_Clear();
    }
    caller.raiseTypeError(
        "type {} doesn't define __round__",
        self->getTypeRef().getDisplayName());
  }

  Ref<> result;
  if (ndigit == nullptr || ndigit == NoneObject()) {
    result.reset(_PyObject_CallNoArg(round.get()));
  } else {
    auto ndigitNum = std::dynamic_pointer_cast<StrictInt>(ndigit);
    if (ndigitNum == nullptr) {
      caller.raiseTypeError("{} is not an integer", ndigit->getDisplayName());
    }
    Ref<> ndigitObj = ndigitNum->getPyObject();
    result.reset(
        PyObject_CallFunctionObjArgs(round.get(), ndigitObj.get(), NULL));
  }
  auto strictResult = fromPyNumberHelper(caller, result);
  if (strictResult == nullptr) {
    return makeUnknown(
        caller,
        "round({}, {})",
        self->getDisplayName(),
        ndigit ? ndigit : NoneObject());
  }
  return strictResult;
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__new__(
    std::shared_ptr<StrictFloat>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> instType,
    std::shared_ptr<BaseStrictObject> value) {
  auto type = std::dynamic_pointer_cast<StrictType>(std::move(instType));
  if (!type->isSubType(FloatType())) {
    caller.raiseTypeError("{} is not a subtype of float", type->getName());
  }
  if (value == nullptr) {
    return std::make_shared<StrictFloat>(std::move(type), caller.caller, 0.0);
  }
  // value is numeric
  auto num = std::dynamic_pointer_cast<StrictNumeric>(value);
  if (num && num->getImaginary() == 0) {
    return std::make_shared<StrictFloat>(
        std::move(type), caller.caller, num->getReal());
  }
  // value is string
  auto str = std::dynamic_pointer_cast<StrictString>(value);
  if (str) {
    try {
      double i = std::stod(str->getValue());
      return std::make_shared<StrictFloat>(std::move(type), caller.caller, i);
    } catch (std::invalid_argument) {
      caller.raiseExceptionStr(
          ValueErrorType(),
          "'{}' cannot be converted to float",
          str->getValue());
    } catch (std::out_of_range) {
      caller.raiseExceptionStr(
          ValueErrorType(),
          "'{}' cannot be converted to float",
          str->getValue());
    }
  }
  // check __float__
  auto floatFunc = iLoadAttrOnType(value, "__float__", nullptr, caller);
  if (floatFunc) {
    return iCall(std::move(floatFunc), kEmptyArgs, kEmptyArgNames, caller);
  }
  // error
  caller.error<UnsupportedException>("float", value->getTypeRef().getName());
  return makeUnknown(caller, "float({})", std::move(value));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__pow__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs,
    std::shared_ptr<BaseStrictObject> mod) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  if (num == nullptr) {
    return NotImplemented();
  }
  return __pow__Helper(std::move(self), caller, std::move(num), std::move(mod));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rpow__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs,
    std::shared_ptr<BaseStrictObject> mod) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  if (num == nullptr) {
    return NotImplemented();
  }
  return __pow__Helper(std::move(num), caller, std::move(self), std::move(mod));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__divmod__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  return __divmod__Helper(std::move(self), caller, std::move(num));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rdivmod__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  return __divmod__Helper(std::move(num), caller, std::move(self));
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__add__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(self->value_ + num->getReal());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__floordiv__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  checkDivisionByZero(num, caller);
  if (num && num->getImaginary() == 0) {
    return caller.makeInt(self->value_ / num->getReal());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__mod__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  if (num && num->getImaginary() == 0) {
    Ref<> selfFloat = self->getPyObject();
    Ref<> numFloat = Ref<>::steal(PyFloat_FromDouble(num->getReal()));
    Ref<> result =
        Ref<>::steal(PyNumber_Remainder(selfFloat.get(), numFloat.get()));

    return std::make_shared<StrictFloat>(
        FloatType(), caller.caller, result.get());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__mul__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(self->value_ * num->getReal());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__sub__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(self->value_ - num->getReal());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__truediv__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(rhs);
  checkDivisionByZero(num, caller);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(self->value_ / num->getReal());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__radd__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(num->getReal() + self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rfloordiv__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  checkDivisionByZero(num, caller);
  if (num && num->getImaginary() == 0) {
    return caller.makeInt(num->getReal() / self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rmod__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  if (num && num->getImaginary() == 0) {
    Ref<> selfFloat = self->getPyObject();
    Ref<> numFloat = Ref<>::steal(PyFloat_FromDouble(num->getReal()));
    Ref<> result =
        Ref<>::steal(PyNumber_Remainder(numFloat.get(), selfFloat.get()));

    return std::make_shared<StrictFloat>(
        FloatType(), caller.caller, result.get());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rmul__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(num->getReal() * self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rsub__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(num->getReal() - self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__rtruediv__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  auto num = std::dynamic_pointer_cast<StrictNumeric>(lhs);
  checkDivisionByZero(num, caller);
  if (num && num->getImaginary() == 0) {
    return caller.makeFloat(num->getReal() / self->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__pos__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller) {
  return caller.makeFloat(self->value_);
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__neg__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller) {
  return caller.makeFloat(-self->value_);
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__eq__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictFloat>(rhs);
  if (num) {
    return caller.makeBool(self->value_ == num->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__ne__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictFloat>(rhs);
  if (num) {
    return caller.makeBool(self->value_ != num->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__lt__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictFloat>(rhs);
  if (num) {
    return caller.makeBool(self->value_ < num->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__le__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictFloat>(rhs);
  if (num) {
    return caller.makeBool(self->value_ <= num->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__gt__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictFloat>(rhs);
  if (num) {
    return caller.makeBool(self->value_ > num->value_);
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictFloat::float__ge__(
    std::shared_ptr<StrictFloat> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  auto num = std::dynamic_pointer_cast<StrictFloat>(rhs);
  if (num) {
    return caller.makeBool(self->value_ >= num->value_);
  }
  return NotImplemented();
}

std::unique_ptr<BaseStrictObject> StrictFloatType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictFloat>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      0);
}

std::shared_ptr<StrictType> StrictFloatType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictFloatType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictFloatType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictFloatType));
  return baseVec;
}

Ref<> StrictFloatType::getPyObject() const {
  return Ref<>(reinterpret_cast<PyObject*>(&PyFloat_Type));
}

void StrictFloatType::addMethods() {
  addMethod("__add__", StrictFloat::float__add__);
  addMethod("__floordiv__", StrictFloat::float__floordiv__);
  addMethod("__mod__", StrictFloat::float__mod__);
  addMethod("__mul__", StrictFloat::float__mul__);
  addMethod("__sub__", StrictFloat::float__sub__);
  addMethod("__truediv__", StrictFloat::float__truediv__);

  addMethod("__radd__", StrictFloat::float__radd__);
  addMethod("__rfloordiv__", StrictFloat::float__rfloordiv__);
  addMethod("__rmod__", StrictFloat::float__rmod__);
  addMethod("__rmul__", StrictFloat::float__rmul__);
  addMethod("__rsub__", StrictFloat::float__rsub__);
  addMethod("__rtruediv__", StrictFloat::float__rtruediv__);

  addMethod("__pos__", StrictFloat::float__pos__);
  addMethod("__neg__", StrictFloat::float__neg__);

  addMethod("__eq__", StrictFloat::float__eq__);
  addMethod("__ne__", StrictFloat::float__ne__);
  addMethod("__lt__", StrictFloat::float__lt__);
  addMethod("__le__", StrictFloat::float__le__);
  addMethod("__gt__", StrictFloat::float__gt__);
  addMethod("__ge__", StrictFloat::float__ge__);

  addMethod(kDunderBool, StrictFloat::float__bool__);
  addMethod(kDunderStr, StrictFloat::float__str__);
  addMethod("__abs__", StrictFloat::float__abs__);
  addMethodDefault("__round__", StrictFloat::float__round__, nullptr);
  addStaticMethodDefault("__new__", StrictFloat::float__new__, nullptr);
  addMethodDefault("__pow__", StrictFloat::float__pow__, nullptr);
  addMethodDefault("__rpow__", StrictFloat::float__rpow__, nullptr);
  addMethod("__divmod__", StrictFloat::float__divmod__);
  addMethod("__rdivmod__", StrictFloat::float__rdivmod__);

  PyObject* floatType = reinterpret_cast<PyObject*>(&PyFloat_Type);
  addPyWrappedMethodObj<>(kDunderRepr, floatType, StrictString::strFromPyObj);
  addPyWrappedMethodObj<1>("__format__", floatType, StrictString::strFromPyObj);
}
} // namespace strictmod::objects
