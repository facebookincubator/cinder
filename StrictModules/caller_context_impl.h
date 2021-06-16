// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_CALLER_CONTEXT_IMPL_CPP__
#define __STRICTM_CALLER_CONTEXT_IMPL_CPP__

#include <fmt/format.h>
#include "StrictModules/caller_context.h"

#include "StrictModules/Objects/objects.h"
#include "StrictModules/exceptions.h"

namespace strictmod {

template <typename... Args>
std::unique_ptr<StrictModuleUserException<BaseStrictObject>>
CallerContext::exception(std::shared_ptr<StrictType> excType, Args...) const {
  auto excDict = std::make_shared<objects::DictType>();
  // TODO populate dict with `args`
  (*excDict)["args"] = nullptr;
  auto excObj = std::make_shared<objects::StrictExceptionObject>(
      std::move(excType), caller, std::move(excDict));
  return std::make_unique<StrictModuleUserException<BaseStrictObject>>(
      lineno, col, filename, scopeName, std::move(excObj));
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
    std::string&& fmtStr,
    Args&&... args) const {
  std::string excMsg = fmt::format(
      std::forward<std::string>(fmtStr), std::forward<Args>(args)...);

  raiseException(
      std::move(error),
      std::make_shared<objects::StrictString>(
          objects::StrType(), caller, std::move(excMsg)));
}

template <typename... Args>
[[noreturn]] void CallerContext::raiseTypeError(
    std::string&& fmtStr,
    Args&&... args) const {
  raiseExceptionStr(
      objects::TypeErrorType(),
      std::forward<std::string>(fmtStr),
      std::forward<Args>(args)...);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeInt(long i) const {
  return std::make_shared<objects::StrictInt>(objects::IntType(), caller, i);
}

inline std::shared_ptr<BaseStrictObject> CallerContext::makeFloat(
    double i) const {
  return std::make_shared<objects::StrictFloat>(
      objects::FloatType(), caller, i);
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
  std::vector<std::shared_ptr<BaseStrictObject>> vec(2);
  vec.push_back(std::move(first));
  vec.push_back(std::move(second));
  return std::make_shared<objects::StrictTuple>(
      objects::TupleType(), caller, std::move(vec));
}

} // namespace strictmod

#endif // __STRICTM_CALLER_CONTEXT_IMPL_CPP__
