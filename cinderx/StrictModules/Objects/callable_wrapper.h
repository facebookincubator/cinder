// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "cinderx/StrictModules/Objects/instance.h"
#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"
#include "cinderx/StrictModules/sequence_map.h"

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
  CallableWrapper(
      WrappedFType<T, Args...> func,
      std::string name,
      bool hasDefault = false)
      : func_(func), name_(name), default_(), hasDefault_(hasDefault) {}

  // currently only one default case come up, but we can easily support
  // multiple defaults
  CallableWrapper(
      WrappedFType<T, Args...> func,
      std::string name,
      std::shared_ptr<BaseStrictObject> defaultValue)
      : func_(func),
        name_(name),
        default_(std::move(defaultValue)),
        hasDefault_(true) {}

  std::shared_ptr<BaseStrictObject> operator()(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    if (!namedArgs.empty()) {
      caller.raiseTypeError(
          "named arguments in builtin call '{}' not supported", name_);
    }
    const int n = sizeof...(Args);
    if constexpr (n > 0) {
      // cannot set n less than 0 since the value is computed for all code paths
      // at compile time, and the template arg must be >= 0
      if (n == args.size() + 1 && hasDefault_) {
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
  bool hasDefault_;

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
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>>,
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
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwArgs;
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
      sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwArgs,
      std::index_sequence<Is...>) {
    return func_(
        std::static_pointer_cast<T>(std::move(obj)),
        caller,
        std::move(starArgs),
        std::move(kwArgs),
        args[Is]...);
  }
};

using SToPFunc =
    std::shared_ptr<BaseStrictObject> (*)(Ref<>, const CallerContext&);

template <typename... Args>
using PyCallable = PyObject* (*)(Args...);

template <int n>
class PythonWrappedCallableByName {
 public:
  PythonWrappedCallableByName(
      PyObject* obj,
      SToPFunc convertFunc,
      std::string name)
      : callable_(), convertFunc_(convertFunc), name_(std::move(name)) {
    callable_ = PyObject_GetAttrString(obj, name_.c_str());
    assert(callable_ != nullptr);
  }

  PythonWrappedCallableByName(const PythonWrappedCallableByName& other)
      : callable_(other.callable_),
        convertFunc_(other.convertFunc_),
        name_(other.name_) {
    Py_XINCREF(callable_);
  }

  PythonWrappedCallableByName(PythonWrappedCallableByName&& other)
      : callable_(other.callable_),
        convertFunc_(other.convertFunc_),
        name_(std::move(other.name_)) {
    other.callable_ = nullptr;
  };

  ~PythonWrappedCallableByName() {
    Py_XDECREF(callable_);
  }

  std::shared_ptr<BaseStrictObject> operator()(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    if (!namedArgs.empty()) {
      caller.raiseTypeError(
          "named arguments in builtin call '{}' not supported", name_);
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
  PyObject* callable_;
  SToPFunc convertFunc_;
  std::string name_;

  template <size_t... Is>
  std::shared_ptr<BaseStrictObject> callStatic(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const CallerContext& caller,
      std::index_sequence<Is...>) {
    Ref<> pyArgs[args.size() < 1 ? 1 : args.size()];

    for (std::size_t i = 0; i < args.size(); ++i) {
      Ref<> val = args[i]->getPyObject();
      if (val.get() == nullptr) {
        caller.error<UnsupportedException>(name_, args[i]->getDisplayName());
        return makeUnknown(caller, "{}({})", name_, formatArgs(args, {}));
      }
      pyArgs[i] = std::move(val);
    }

    Ref<> result;

    if (obj == nullptr) {
      // calling static method
      result = Ref<>::steal(PyObject_CallFunctionObjArgs(
          callable_, (pyArgs[Is].get())..., nullptr));
    } else {
      Ref<> self = obj->getPyObject();
      if (self.get() == nullptr) {
        caller.error<UnsupportedException>(name_, obj->getDisplayName());
        return makeUnknown(caller, "{}({})", name_, formatArgs(args, {}));
      }

      result = Ref<>::steal(PyObject_CallFunctionObjArgs(
          callable_, self.get(), (pyArgs[Is].get())..., nullptr));
    }

    if (result == nullptr) {
      PyObject *type, *value, *traceback;
      PyErr_Fetch(&type, &value, &traceback);
      auto refType = Ref<>::steal(type);
      auto refValue = Ref<>::steal(value);
      auto refTb = Ref<>::steal(traceback);
      std::string errName;
      if (type) {
        errName = PyExceptionClass_Name(type);
      } else {
        errName = "unknown error";
      }
      auto errType = getExceptionFromString(errName, TypeErrorType());
      caller.raiseExceptionStr(
          errType,
          "calling {}({}) resulted in {}",
          name_,
          formatArgs(args, {}),
          errName);
    }
    std::shared_ptr<BaseStrictObject> resultObj =
        convertFunc_(std::move(result), caller);
    return resultObj;
  }
};

class PythonWrappedCallableDefaultByName {
 public:
  PythonWrappedCallableDefaultByName(
      PyObject* obj,
      SToPFunc convertFunc,
      std::string name,
      std::size_t defaultSize,
      std::size_t numArgs)
      : callable_(),
        convertFunc_(convertFunc),
        name_(std::move(name)),
        defaultSize_(defaultSize),
        n_(numArgs) {
    callable_ = PyObject_GetAttrString(obj, name_.c_str());
    assert(callable_ != nullptr);
  }

  PythonWrappedCallableDefaultByName(
      const PythonWrappedCallableDefaultByName& other)
      : callable_(other.callable_),
        convertFunc_(other.convertFunc_),
        name_(other.name_),
        defaultSize_(other.defaultSize_),
        n_(other.n_) {
    Py_XINCREF(callable_);
  }

  PythonWrappedCallableDefaultByName(PythonWrappedCallableDefaultByName&& other)
      : callable_(other.callable_),
        convertFunc_(other.convertFunc_),
        name_(std::move(other.name_)),
        defaultSize_(other.defaultSize_),
        n_(other.n_) {
    other.callable_ = nullptr;
  };

  ~PythonWrappedCallableDefaultByName() {
    Py_XDECREF(callable_);
  }

  std::shared_ptr<BaseStrictObject> operator()(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    if (args.size() + defaultSize_ < n_ || args.size() > n_) {
      caller.raiseTypeError(
          "{}() takes {} to {} positional arguments but {} were given",
          name_,
          n_ - defaultSize_,
          n_,
          args.size());
    }
    if (namedArgs.empty()) {
      return callHelper(std::move(obj), args, caller);
    } else {
      return callHelperKw(std::move(obj), args, namedArgs, caller);
    }
  }

 private:
  PyObject* callable_;
  SToPFunc convertFunc_;
  std::string name_;
  std::size_t defaultSize_;
  std::size_t n_; // number of parameters counting defaults

  std::shared_ptr<BaseStrictObject> callHelper(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const CallerContext& caller) {
    Ref<> argTuple = makeArgTuple(std::move(obj), args, args.size(), caller);
    if (argTuple == nullptr) {
      return makeUnknown(caller, "{}({})", name_, formatArgs(args, {}));
    }

    PyObject* result = PyObject_CallObject(callable_, argTuple.get());
    if (result == nullptr) {
      PyErr_Clear();
      caller.raiseTypeError(
          "calling {}({}) resulted in TypeError", name_, formatArgs(args, {}));
    }
    std::shared_ptr<BaseStrictObject> resultObj =
        convertFunc_(Ref<>::steal(result), caller);
    return resultObj;
  }

  std::shared_ptr<BaseStrictObject> callHelperKw(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    assert(args.size() >= namedArgs.size());
    std::size_t posSize = args.size() - namedArgs.size();

    // process arguments passed by caller
    Ref<> argTuple = makeArgTuple(std::move(obj), args, posSize, caller);
    if (argTuple == nullptr) {
      return makeUnknown(caller, "{}({})", name_, formatArgs(args, {}));
    }

    // kwargs
    Ref<> kwArgDict = Ref<>::steal(PyDict_New());
    for (std::size_t i = 0; i < namedArgs.size(); ++i) {
      Ref<> value = args[posSize + i]->getPyObject();
      PyDict_SetItemString(kwArgDict.get(), namedArgs[i].c_str(), value.get());
    }

    PyObject* result =
        PyObject_Call(callable_, argTuple.get(), kwArgDict.get());
    if (result == nullptr) {
      PyErr_Clear();
      caller.raiseTypeError(
          "calling {}({}) resulted in TypeError", name_, formatArgs(args, {}));
    }
    std::shared_ptr<BaseStrictObject> resultObj =
        convertFunc_(Ref<>::steal(result), caller);
    return resultObj;
  }

  Ref<> makeArgTuple(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      std::size_t size,
      const CallerContext& caller) {
    Ref<> argTuple = Ref<>::steal(PyTuple_New(size + 1));
    // process arguments passed by caller
    for (std::size_t i = 0; i < size; ++i) {
      Ref<> val = args[i]->getPyObject();
      if (val.get() == nullptr) {
        caller.error<UnsupportedException>(name_, args[i]->getDisplayName());
        return Ref<>(nullptr);
      }
      PyTuple_SET_ITEM(argTuple.get(), i + 1, val.release());
    }

    // first argument is self
    Ref<> self = obj->getPyObject();
    if (self.get() == nullptr) {
      caller.error<UnsupportedException>(name_, obj->getDisplayName());
      return Ref<>(nullptr);
    }
    PyTuple_SET_ITEM(argTuple.get(), 0, self.release());
    return argTuple;
  }
};

template <typename T, std::string T::*mp>
std::shared_ptr<BaseStrictObject> stringMemberGetFunc(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  return caller.makeStr(static_cast<T*>(inst.get())->*mp);
}

template <typename T, std::string T::*mp>
void stringMemberSetFunc(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto strValue = std::dynamic_pointer_cast<StrictString>(value);
  if (!strValue) {
    caller.raiseTypeError(
        "string member of {} object can only be set to string",
        inst->getTypeRef().getName());
  }
  static_cast<T*>(inst.get())->*mp = strValue->getValue();
}

template <typename T, std::string T::*mp>
void stringMemberDelFunc(
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext& caller) {
  caller.raiseTypeError(
      "string member of {} object can only be set to string",
      inst->getTypeRef().getName());
}

template <typename T, std::optional<std::string> T::*mp>
std::shared_ptr<BaseStrictObject> stringOptionalMemberGetFunc(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto optStr = static_cast<T*>(inst.get())->*mp;
  if (optStr) {
    return caller.makeStr(*optStr);
  }
  return NoneObject();
}

template <typename T, std::optional<std::string> T::*mp>
void stringOptionalMemberSetFunc(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  if (value == nullptr || value == NoneObject()) {
    static_cast<T*>(inst.get())->*mp = std::nullopt;
    return;
  }
  auto strValue = std::dynamic_pointer_cast<StrictString>(value);
  if (!strValue) {
    caller.raiseTypeError(
        "string member of {} object can only be set to string or None, but got "
        "{}",
        inst->getTypeRef().getName(),
        value->getDisplayName());
  }
  static_cast<T*>(inst.get())->*mp = strValue->getValue();
}

template <typename T, std::optional<std::string> T::*mp>
void stringOptionalMemberDelFunc(
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext&) {
  static_cast<T*>(inst.get())->*mp = std::nullopt;
}

template <typename T, std::string T::*mp>
void StrictType::addStringMemberDescriptor(const std::string& name) {
  auto descr = std::make_shared<StrictGetSetDescriptor>(
      creator_,
      name,
      stringMemberGetFunc<T, mp>,
      stringMemberSetFunc<T, mp>,
      stringMemberDelFunc<T, mp>);
  setAttr(name, std::move(descr));
}

template <typename T, std::optional<std::string> T::*mp>
void StrictType::addStringOptionalMemberDescriptor(const std::string& name) {
  auto descr = std::make_shared<StrictGetSetDescriptor>(
      creator_,
      name,
      stringOptionalMemberGetFunc<T, mp>,
      stringOptionalMemberSetFunc<T, mp>,
      stringOptionalMemberDelFunc<T, mp>);
  setAttr(name, std::move(descr));
}

} // namespace strictmod::objects
