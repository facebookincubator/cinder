// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_CALLABLE_WRAPPER_H__
#define __STRICTM_CALLABLE_WRAPPER_H__
#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/object_type.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"

#include <unordered_map>

namespace strictmod::objects {

// function pointer of a wrapper function
template <typename T, typename... Args>
using WrappedFType = std::shared_ptr<BaseStrictObject> (*)(
    std::shared_ptr<T>,
    const CallerContext&,
    Args...);

template <typename T, typename... Args>
class CallableWrapper {
  static_assert(
      std::is_base_of<BaseStrictObject, T>::value,
      "instance type of wrapper function must be strict object");

 public:
  CallableWrapper(WrappedFType<T, Args...> func, std::string name)
      : func_(func), name_(name), default_() {}

  // currently only one default case come up, but we can easily support
  // multiple defaults
  CallableWrapper(
      WrappedFType<T, Args...> func,
      std::string name,
      std::shared_ptr<BaseStrictObject> defaultValue)
      : func_(func), name_(name), default_(std::move(defaultValue)) {}

  std::shared_ptr<BaseStrictObject> operator()(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    if (!namedArgs.empty()) {
      throw std::runtime_error("named arguments in builtin call not supported");
    }
    const int n = sizeof...(Args);
    if constexpr (n > 0) {
      // cannot set n less than 0 since the value is computed for all code paths
      // at compile time, and the template arg must be >= 0
      if (n == args.size() + 1) {
        return callStaticWithDefault(
            std::move(obj), args, caller, std::make_index_sequence<n - 1>());
      }
    }

    if (n != args.size()) {
      caller.raiseTypeError(
          "{}() takes {} positional arguments but {} were given",
          name_,
          n,
          args.size());
    }
    return callStatic(
        std::move(obj), args, caller, std::make_index_sequence<n>());
  }

 private:
  WrappedFType<T, Args...> func_;
  std::string name_;
  std::shared_ptr<BaseStrictObject> default_;

  template <size_t... Is>
  std::shared_ptr<BaseStrictObject> callStatic(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const CallerContext& caller,
      std::index_sequence<Is...>) {
    return func_(
        std::static_pointer_cast<T>(std::move(obj)), caller, args[Is]...);
  }

  template <size_t... Is>
  std::shared_ptr<BaseStrictObject> callStaticWithDefault(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const CallerContext& caller,
      std::index_sequence<Is...>) {
    return func_(
        std::static_pointer_cast<T>(std::move(obj)),
        caller,
        args[Is]...,
        default_);
  }
};

// function pointer of a wrapper function with star args
// The positional arguments are "positional only", and keywords
// are reserved specifically for **kwargs
template <typename T, typename... Args>
using WrappedFStarType = std::shared_ptr<BaseStrictObject> (*)(
    std::shared_ptr<T>,
    const CallerContext&,
    std::vector<std::shared_ptr<BaseStrictObject>>,
    std::unordered_map<std::string, std::shared_ptr<BaseStrictObject>>,
    Args...);

template <typename T, typename... Args>
class StarCallableWrapper {
  static_assert(
      std::is_base_of<BaseStrictObject, T>::value,
      "instance type of wrapper function must be strict object");

 public:
  StarCallableWrapper(WrappedFStarType<T, Args...> func, std::string name)
      : func_(func), name_(name) {}

  std::shared_ptr<BaseStrictObject> operator()(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    const int n = sizeof...(Args);
    int unnamedSize = args.size() - namedArgs.size();
    if (unnamedSize < n) {
      caller.raiseTypeError(
          "{}() takes {} positional arguments but {} were given",
          name_,
          n,
          unnamedSize);
    }
    std::vector<std::shared_ptr<BaseStrictObject>> starArgs;
    starArgs.reserve(unnamedSize - n);
    for (int i = n; i < unnamedSize; ++i) {
      // *args
      starArgs.push_back(args[i]);
    }
    std::unordered_map<std::string, std::shared_ptr<BaseStrictObject>> kwArgs;
    kwArgs.reserve(namedArgs.size());
    for (size_t i = 0; i < namedArgs.size(); ++i) {
      kwArgs[namedArgs[i]] = args[i + unnamedSize];
    }

    return callStatic(
        std::move(obj),
        args,
        caller,
        std::move(starArgs),
        std::move(kwArgs),
        std::make_index_sequence<n>());
  }

 private:
  WrappedFStarType<T, Args...> func_;
  std::string name_;

  template <size_t... Is>
  std::shared_ptr<BaseStrictObject> callStatic(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const CallerContext& caller,
      std::vector<std::shared_ptr<BaseStrictObject>> starArgs,
      std::unordered_map<std::string, std::shared_ptr<BaseStrictObject>> kwArgs,
      std::index_sequence<Is...>) {
    return func_(
        std::static_pointer_cast<T>(std::move(obj)),
        caller,
        std::move(starArgs),
        std::move(kwArgs),
        args[Is]...);
  }
};

} // namespace strictmod::objects

#endif // __STRICTM_CALLABLE_WRAPPER_H__
