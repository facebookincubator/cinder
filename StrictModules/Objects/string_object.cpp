// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/string_object.h"

#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/helper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include <sstream>

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
    Ref<> pyValue)
    : StrictInstance(std::move(type), std::move(creator)),
      pyStr_(std::move(pyValue)),
      value_() {
  const char* v = PyUnicode_AsUTF8(pyStr_);
  if (v == nullptr) {
    PyErr_Clear();
    Ref<> enStr = Ref<>::steal(
        PyUnicode_AsEncodedString(pyStr_, "utf-8", "surrogatepass"));

    assert(enStr != nullptr);
    value_ = PyBytes_AsString(enStr);
  } else {
    value_ = v;
  }
}

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
  return Ref<>::create(pyStr_.get());
}

std::string StrictString::getDisplayName() const {
  return value_;
}

std::shared_ptr<BaseStrictObject> StrictString::copy(
    const CallerContext& caller) {
  return std::make_shared<StrictString>(type_, caller.caller, value_);
}

std::shared_ptr<BaseStrictObject> StrictString::strFromPyObj(
    Ref<> pyObj,
    const CallerContext& caller) {
  return std::make_shared<StrictString>(
      StrType(), caller.caller, std::move(pyObj));
}

std::shared_ptr<BaseStrictObject> StrictString::listFromPyStrList(
    Ref<> pyObj,
    const CallerContext& caller) {
  if (!PyList_CheckExact(pyObj.get())) {
    caller.raiseTypeError("str.split did not return a list");
  }
  std::size_t size = PyList_GET_SIZE(pyObj.get());
  std::vector<std::shared_ptr<BaseStrictObject>> data;
  data.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    auto elem = Ref<>::create(PyList_GET_ITEM(pyObj.get(), i));
    auto elemStr = std::make_shared<StrictString>(
        StrType(), caller.caller, std::move(elem));
    data.push_back(std::move(elemStr));
  }
  return std::make_shared<StrictList>(
      ListType(), caller.caller, std::move(data));
}

std::shared_ptr<BaseStrictObject> StrictString::str__new__(
    std::shared_ptr<StrictString>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> instType,
    std::shared_ptr<BaseStrictObject> object) {
  std::shared_ptr<StrictStringType> strType =
      std::dynamic_pointer_cast<StrictStringType>(instType);
  if (strType == nullptr) {
    caller.raiseExceptionStr(
        TypeErrorType(), "X is not a str type object ({})", instType);
  }
  if (object == nullptr) {
    return std::make_shared<StrictString>(
        std::move(strType), caller.caller, "");
  }
  std::string funcName = kDunderStr;
  auto func = iLoadAttrOnType(object, kDunderStr, nullptr, caller);
  if (func == nullptr) {
    funcName = kDunderRepr;
    func = iLoadAttrOnType(object, kDunderRepr, nullptr, caller);
  }
  if (func != nullptr) {
    auto result = iCall(std::move(func), kEmptyArgs, kEmptyArgNames, caller);
    auto resultStr = std::dynamic_pointer_cast<StrictString>(result);
    if (resultStr == nullptr) {
      caller.raiseTypeError(
          "{}.{} must return string, not {}",
          object->getTypeRef().getName(),
          std::move(funcName),
          result->getTypeRef().getName());
    }
    if (strType == StrType()) {
      return resultStr;
    }
    return std::make_shared<StrictString>(
        std::move(strType), caller.caller, resultStr->getValue());
  } else {
    caller.error<UnsupportedException>("str()", object->getDisplayName());
    return makeUnknown(caller, "str({})", object);
  }
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

std::shared_ptr<BaseStrictObject> StrictString::strJoin(
    std::shared_ptr<StrictString> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable) {
  std::vector<std::shared_ptr<BaseStrictObject>> elements =
      iGetElementsVec(std::move(iterable), caller);
  std::stringstream ss;
  std::size_t size = elements.size();
  const std::string& sep = self->getValue();
  for (std::size_t i = 0; i < size; ++i) {
    auto elemStr = std::dynamic_pointer_cast<StrictString>(elements[i]);
    if (elemStr == nullptr) {
      caller.raiseTypeError(
          "expect str for element {} of join, got {}",
          i,
          elements[i]->getTypeRef().getName());
    }
    ss << elemStr->getValue();
    if (i != size - 1) {
      ss << sep;
    }
  }
  return caller.makeStr(ss.str());
}

std::shared_ptr<BaseStrictObject> StrictString::strFormat(
    std::shared_ptr<BaseStrictObject> self,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext& caller) {
  caller.raiseExceptionStr(
      NotImplementedErrorType(),
      "format() method of {} object is not supported yet in strict modules",
      self->getType()->getName());
}

std::shared_ptr<BaseStrictObject> StrictString::str__str__(
    std::shared_ptr<StrictString> self,
    const CallerContext& caller) {
  return caller.makeStr(self->getValue());
}

std::shared_ptr<BaseStrictObject> StrictString::str__iter__(
    std::shared_ptr<StrictString> self,
    const CallerContext& caller) {
  const std::string& value = self->getValue();
  std::vector<std::shared_ptr<BaseStrictObject>> chars;
  chars.reserve(value.size());
  for (char c : value) {
    chars.push_back(caller.makeStr(std::string{c}));
  }
  return std::make_shared<StrictVectorIterator>(
      VectorIteratorType(), caller.caller, std::move(chars));
}

std::shared_ptr<BaseStrictObject> StrictString::str__getitem__(
    std::shared_ptr<StrictString> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> index) {
  const std::string& data = self->getValue();
  std::shared_ptr<StrictInt> intIndex =
      std::dynamic_pointer_cast<StrictInt>(index);
  if (intIndex != nullptr) {
    if (!intIndex->getValue()) {
      caller.raiseTypeError(
          "string index out of range: {}", intIndex->getDisplayName());
    }
    int idx = normalizeIndex(*intIndex->getValue(), data.size());
    if (idx >= 0 && (size_t)idx < data.size()) {
      return caller.makeStr(data.substr(idx, 1));
    } else {
      caller.raiseTypeError("string index out of range: {}", idx);
    }
  }
  std::shared_ptr<StrictSlice> sliceIndex =
      std::dynamic_pointer_cast<StrictSlice>(index);
  if (sliceIndex != nullptr) {
    int dataSize = int(data.size());
    std::string result;
    result.reserve(dataSize);
    int start, stop, step;
    std::tie(start, stop, step) =
        sliceIndex->normalizeToSequenceIndex(caller, dataSize);

    if (step > 0) {
      for (int i = std::max(0, start); i < std::min(stop, dataSize);
           i += step) {
        result.push_back(data[i]);
      }
    } else {
      for (int i = std::min(dataSize - 1, start); i > std::max(-1, stop);
           i += step) {
        result.push_back(data[i]);
      }
    }
    // sliced result is always the base type
    return caller.makeStr(std::move(result));
  }
  caller.raiseTypeError(
      "string indices must be integers or slices, not {}",
      index->getTypeRef().getName());
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
  return Ref<>::create(reinterpret_cast<PyObject*>(&PyUnicode_Type));
}

void StrictStringType::addMethods() {
  addStaticMethodDefault("__new__", StrictString::str__new__, nullptr);
  addMethod(kDunderLen, StrictString::str__len__);
  addMethod(kDunderStr, StrictString::str__str__);
  addMethod(kDunderIter, StrictString::str__iter__);
  addMethod("__eq__", StrictString::str__eq__);
  addMethod("join", StrictString::strJoin);
  addMethod(kDunderGetItem, StrictString::str__getitem__);
  PyObject* strType = reinterpret_cast<PyObject*>(&PyUnicode_Type);
  addPyWrappedMethodObj<1>("__format__", strType, StrictString::strFromPyObj);
  addPyWrappedMethodObj<>(kDunderRepr, strType, StrictString::strFromPyObj);
  addPyWrappedMethodObj<1>("__mod__", strType, StrictString::strFromPyObj);
  addPyWrappedMethodObj<>("isidentifier", strType, StrictBool::boolFromPyObj);
  addPyWrappedMethodObj<>("lower", strType, StrictString::strFromPyObj);
  addPyWrappedMethodObj<>("upper", strType, StrictString::strFromPyObj);

  addPyWrappedMethodObj<1>(
      "__ne__", strType, StrictBool::boolOrNotImplementedFromPyObj);
  addPyWrappedMethodObj<1>(
      "__ge__", strType, StrictBool::boolOrNotImplementedFromPyObj);
  addPyWrappedMethodObj<1>(
      "__gt__", strType, StrictBool::boolOrNotImplementedFromPyObj);
  addPyWrappedMethodObj<1>(
      "__le__", strType, StrictBool::boolOrNotImplementedFromPyObj);
  addPyWrappedMethodObj<1>(
      "__lt__", strType, StrictBool::boolOrNotImplementedFromPyObj);
  addPyWrappedMethodObj<1>(
      "__contains__", strType, StrictBool::boolOrNotImplementedFromPyObj);

  addPyWrappedMethodObj<1>("__add__", strType, StrictString::strFromPyObj);

  addPyWrappedMethodDefaultObj(
      "strip", strType, StrictString::strFromPyObj, 1, 1);
  addPyWrappedMethodDefaultObj(
      "replace", strType, StrictString::strFromPyObj, 1, 3);
  addPyWrappedMethodDefaultObj(
      "startswith", strType, StrictBool::boolFromPyObj, 2, 3);
  addPyWrappedMethodDefaultObj(
      "split", strType, StrictString::listFromPyStrList, 2, 2);
  addPyWrappedMethodDefaultObj(
      "encode", strType, StrictBytes::bytesFromPyObj, 2, 2);
  addMethodDescr("format", StrictString::strFormat);
}

// Bytes Object
StrictBytes::StrictBytes(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    PyObject* bytesObj)
    : StrictInstance(std::move(type), std::move(creator)),
      bytesObj_(Ref<>::create(bytesObj)) {}

StrictBytes::StrictBytes(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    Ref<> bytesObj)
    : StrictInstance(std::move(type), std::move(creator)),
      bytesObj_(std::move(bytesObj)) {}

Ref<> StrictBytes::getPyObject() const {
  return Ref<>::create(bytesObj_.get());
}

std::string StrictBytes::getDisplayName() const {
  return std::string(PyBytes_AsString(bytesObj_.get()));
}

// conversion methods
std::shared_ptr<BaseStrictObject> StrictBytes::bytesFromPyObj(
    Ref<> pyObj,
    const CallerContext& caller) {
  return std::make_shared<StrictBytes>(
      BytesType(), caller.caller, std::move(pyObj));
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictBytes::bytes__len__(
    std::shared_ptr<StrictBytes> self,
    const CallerContext& caller) {
  return caller.makeInt(PyBytes_Size(self->bytesObj_));
}

std::shared_ptr<BaseStrictObject> StrictBytes::bytes__iter__(
    std::shared_ptr<StrictBytes> self,
    const CallerContext& caller) {
  Py_ssize_t len = PyBytes_Size(self->bytesObj_.get());
  char* content = PyBytes_AsString(self->bytesObj_.get());
  std::vector<std::shared_ptr<BaseStrictObject>> contentsVec;
  contentsVec.reserve(len);
  for (int i = 0; i < len; ++i) {
    auto contentInt = caller.makeInt(content[i]);
    contentsVec.push_back(std::move(contentInt));
  }
  return std::make_shared<StrictVectorIterator>(
      VectorIteratorType(), caller.caller, std::move(contentsVec));
}

std::unique_ptr<BaseStrictObject> StrictBytesType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  Ref<> s = Ref<>::steal(PyBytes_FromString(""));
  return std::make_unique<StrictBytes>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      std::move(s));
}

std::shared_ptr<StrictType> StrictBytesType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictBytesType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

Ref<> StrictBytesType::getPyObject() const {
  return Ref<>::create(reinterpret_cast<PyObject*>(&PyBytes_Type));
}

void StrictBytesType::addMethods() {
  addMethod(kDunderLen, StrictBytes::bytes__len__);
  addMethod(kDunderIter, StrictBytes::bytes__iter__);

  PyObject* bytesType = reinterpret_cast<PyObject*>(&PyBytes_Type);

  addPyWrappedMethodObj<>("lower", bytesType, StrictBytes::bytesFromPyObj);
  addPyWrappedMethodObj<>("upper", bytesType, StrictBytes::bytesFromPyObj);
  addPyWrappedMethodObj<1>("__add__", bytesType, StrictBytes::bytesFromPyObj);
  addPyWrappedStaticMethodObj<2>(
      "maketrans", bytesType, StrictBytes::bytesFromPyObj);
}

std::vector<std::type_index> StrictBytesType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictBytesType));
  return baseVec;
}

// ByteArray
StrictByteArray::StrictByteArray(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    PyObject* bytearrayObj)
    : StrictInstance(std::move(type), std::move(creator)),
      bytearrayObj_(Ref<>::create(bytearrayObj)) {}

StrictByteArray::StrictByteArray(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    Ref<> bytearrayObj)
    : StrictInstance(std::move(type), std::move(creator)),
      bytearrayObj_(std::move(bytearrayObj)) {}

Ref<> StrictByteArray::getPyObject() const {
  return Ref<>::create(bytearrayObj_.get());
}

std::string StrictByteArray::getDisplayName() const {
  return std::string(PyByteArray_AsString(bytearrayObj_.get()));
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictByteArray::bytearray__iter__(
    std::shared_ptr<StrictByteArray> self,
    const CallerContext& caller) {
  Py_ssize_t len = PyByteArray_Size(self->bytearrayObj_.get());
  char* content = PyByteArray_AsString(self->bytearrayObj_.get());
  std::vector<std::shared_ptr<BaseStrictObject>> contentsVec;
  contentsVec.reserve(len);
  for (int i = 0; i < len; ++i) {
    auto contentInt = caller.makeInt(content[i]);
    contentsVec.push_back(std::move(contentInt));
  }
  return std::make_shared<StrictVectorIterator>(
      VectorIteratorType(), caller.caller, std::move(contentsVec));
}

std::unique_ptr<BaseStrictObject> StrictByteArrayType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  Ref<> s = Ref<>::steal(PyByteArray_FromStringAndSize("", 0));
  return std::make_unique<StrictByteArray>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      std::move(caller),
      std::move(s));
}

std::shared_ptr<StrictType> StrictByteArrayType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictByteArrayType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

Ref<> StrictByteArrayType::getPyObject() const {
  return Ref<>::create(reinterpret_cast<PyObject*>(&PyByteArray_Type));
}

void StrictByteArrayType::addMethods() {
  addMethod(kDunderIter, StrictByteArray::bytearray__iter__);
}

std::vector<std::type_index> StrictByteArrayType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictByteArrayType));
  return baseVec;
}
} // namespace strictmod::objects
