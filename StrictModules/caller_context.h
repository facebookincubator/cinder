// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_CALLER_CONTEXT_H__
#define __STRICTM_CALLER_CONTEXT_H__

#include <cassert>
#include <memory>
#include "StrictModules/error_sink.h"

namespace strictmod {

namespace objects {
class StrictModuleObject;
class BaseStrictObject;
class StrictType;
} // namespace objects

class StrictModuleException;

using objects::BaseStrictObject;
using objects::StrictModuleObject;
using objects::StrictType;

class CallerContext {
 public:
  std::weak_ptr<StrictModuleObject> caller;
  std::string filename;
  /* scope where call happens, such as function name */
  std::string scopeName;
  int lineno;
  int col;
  /* error sink, owned by the analyzed module, here we just
   * borrow the pointer
   */
  BaseErrorSink* errorSink;

  CallerContext(
      std::shared_ptr<StrictModuleObject> caller,
      std::string filename,
      std::string scopeName,
      int lineno,
      int col,
      BaseErrorSink* error)
      : caller(std::weak_ptr<StrictModuleObject>(caller)),
        filename(std::move(filename)),
        scopeName(std::move(scopeName)),
        lineno(lineno),
        col(col),
        errorSink(error) {
    assert(errorSink != nullptr);
  }

  CallerContext(
      std::weak_ptr<StrictModuleObject> caller,
      std::string filename,
      std::string scopeName,
      int lineno,
      int col,
      BaseErrorSink* error)
      : caller(std::move(caller)),
        filename(std::move(filename)),
        scopeName(std::move(scopeName)),
        lineno(lineno),
        col(col),
        errorSink(error) {
    assert(errorSink != nullptr);
  }

  template <typename T, typename... Args>
  void error(Args&&... args) const {
    errorSink->error<T>(
        lineno, col, filename, scopeName, std::forward<Args>(args)...);
  }

  template <typename... Args>
  std::unique_ptr<StrictModuleUserException<BaseStrictObject>> exception(
      std::shared_ptr<StrictType> excType,
      Args... args) const;

  [[noreturn]] void raiseExceptionFromObj(
      std::shared_ptr<BaseStrictObject> excObj) const;

  template <typename... Args>
  [[noreturn]] void raiseException(
      std::shared_ptr<StrictType> excType,
      Args&&... args) const;

  template <typename... Args>
  [[noreturn]] void raiseExceptionStr(
      std::shared_ptr<StrictType> error,
      std::string&& fmtStr,
      Args&&... args) const;

  template <typename... Args>
  [[noreturn]] void raiseTypeError(std::string&& fmtStr, Args&&... args) const;

  // convenience methods
  std::shared_ptr<BaseStrictObject> makeInt(long i) const;
  std::shared_ptr<BaseStrictObject> makeFloat(double f) const;
  std::shared_ptr<BaseStrictObject> makeBool(bool b) const;
  std::shared_ptr<BaseStrictObject> makeStr(std::string s) const;
  std::shared_ptr<BaseStrictObject> makePair(
      std::shared_ptr<BaseStrictObject> first,
      std::shared_ptr<BaseStrictObject> second) const;
};
} // namespace strictmod
#endif // !__STRICTM_CALLER_CONTEXT_H__
