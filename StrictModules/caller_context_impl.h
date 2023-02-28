// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context.h"
#include "StrictModules/exceptions.h"

#include <fmt/format.h>

namespace strictmod {

template <typename... Args>
std::unique_ptr<StrictModuleUserException<BaseStrictObject>>
CallerContext::exception(std::shared_ptr<StrictType> excType, Args... args)
    const {
  auto excDict = std::make_shared<objects::DictType>();
  std::vector<std::shared_ptr<BaseStrictObject>> argsV{args...};
  (*excDict)["args"] = std::make_shared<objects::StrictTuple>(
      objects::TupleType(), caller, std::move(argsV));
  auto excObj = std::make_shared<objects::StrictExceptionObject>(
      std::move(excType), caller, std::move(excDict));
  return std::make_unique<StrictModuleUserException<BaseStrictObject>>(
      lineno, col, filename, scopeName, std::move(excObj));
}

[[noreturn]] inline void CallerContext::raiseExceptionFromObj(
    std::shared_ptr<BaseStrictObject> excObj) const {
  std::make_unique<StrictModuleUserException<BaseStrictObject>>(
      lineno, col, filename, scopeName, std::move(excObj))
      ->raise();
  Py_UNREACHABLE();
}

[[noreturn]] inline void CallerContext::raiseCurrentPyException() const {
  PyObject *type, *value, *traceback;
  PyErr_Fetch(&type, &value, &traceback);
  PyErr_Clear();
  auto refType = Ref<>::steal(type);
  auto refValue = Ref<>::steal(value);
  auto refTb = Ref<>::steal(traceback);
  std::string errName, msg;
  PyObject* msgv = nullptr;
  if (type) {
    errName = PyExceptionClass_Name(type);
  } else {
    errName = "unknown error";
  }
  if (value) {
    msgv = PyObject_Str(value);
  }
  if (msgv) {
    msg = PyUnicode_AsUTF8(msgv);
    Py_DECREF(msgv);
  } else {
    msg = errName;
  }
  auto errType = getExceptionFromString(errName, objects::ExceptionType());
  raiseExceptionStr(errType, "{}", msg);
}

template <typename... Args>
[[noreturn]] void CallerContext::raiseException(
    std::shared_ptr<StrictType> excType,
    Args&&... args) const {
  exception(std::move(excType), std::forward<Args>(args)...)->raise();
  Py_UNREACHABLE();
}

template <typename... Args>
[[noreturn]] void CallerContext::raiseExceptionStr(
    std::shared_ptr<StrictType> error,
    fmt::format_string<Args...> fmtStr,
    Args&&... args) const {
  std::string excMsg = fmt::format(fmtStr, std::forward<Args>(args)...);

  raiseException(
      std::move(error),
      std::make_shared<objects::StrictString>(
          objects::StrType(), caller, std::move(excMsg)));
}

template <typename... Args>
[[noreturn]] void CallerContext::raiseTypeError(
    fmt::format_string<Args...> fmtStr,
    Args&&... args) const {
  raiseExceptionStr(
      objects::TypeErrorType(), fmtStr, std::forward<Args>(args)...);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeInt(
    long long i) const {
  return std::make_shared<objects::StrictInt>(objects::IntType(), caller, i);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeInt(Ref<> i) const {
  return std::make_shared<objects::StrictInt>(objects::IntType(), caller, i);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeFloat(
    double i) const {
  return std::make_shared<objects::StrictFloat>(
      objects::FloatType(), caller, i);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeFloat(
    Ref<> f) const {
  return std::make_shared<objects::StrictFloat>(
      objects::FloatType(), caller, f);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeBool(bool b) const {
  return b ? objects::StrictTrue() : objects::StrictFalse();
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeStr(
    std::string s) const {
  return std::make_shared<objects::StrictString>(
      objects::StrType(), caller, std::move(s));
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makePair(
    std::shared_ptr<BaseStrictObject> first,
    std::shared_ptr<BaseStrictObject> second) const {
  std::vector<std::shared_ptr<BaseStrictObject>> vec;
  vec.reserve(2);
  vec.push_back(std::move(first));
  vec.push_back(std::move(second));
  return std::make_shared<objects::StrictTuple>(
      objects::TupleType(), caller, std::move(vec));
}

} // namespace strictmod
