// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "cinderx/StrictModules/Objects/instance.h"
#include "cinderx/StrictModules/Objects/object_type.h"

namespace strictmod::objects {
// Type of callable function f(self, args, kwargs)
typedef std::function<std::shared_ptr<BaseStrictObject>(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext&)>
    InstCallType;

//-----------method descriptor ("builtin" methods)---------
class StrictMethodDescr : public StrictInstance {
 public:
  StrictMethodDescr(
      std::weak_ptr<StrictModuleObject> creator,
      InstCallType func,
      std::shared_ptr<StrictType> declType,
      std::string name);

  InstCallType getFunc() {
    return func_;
  }
  std::shared_ptr<StrictType> getDeclaredType() {
    return declType_;
  }
  std::string getFuncName() {
    return funcName_;
  }
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

 private:
  InstCallType func_;
  std::shared_ptr<StrictType> declType_;
  std::string funcName_;
};

class StrictMethodDescrType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;
  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& argNames,
      const CallerContext& caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual bool isCallable(const CallerContext& caller) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

// --------------------Builtin functions--------------------
class StrictBuiltinFunctionOrMethod : public StrictInstance {
 public:
  StrictBuiltinFunctionOrMethod(
      std::weak_ptr<StrictModuleObject> creator,
      InstCallType func,
      std::shared_ptr<BaseStrictObject> inst,
      std::string name);

  InstCallType getFunc() {
    return func_;
  }
  std::shared_ptr<BaseStrictObject> getInst() {
    return inst_;
  }

  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

 private:
  InstCallType func_;
  std::shared_ptr<BaseStrictObject> inst_;
  std::string name_;
  std::string displayName_;
};

class StrictBuiltinFunctionOrMethodType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& names,
      const CallerContext& caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual bool isCallable(const CallerContext& caller) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

// --------------instance (user) Method-------------------
class StrictMethod : public StrictInstance {
 public:
  StrictMethod(
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> func,
      std::shared_ptr<BaseStrictObject> inst);

  std::shared_ptr<BaseStrictObject> getFunc() const {
    return func_;
  }
  std::shared_ptr<BaseStrictObject> getInst() const {
    return inst_;
  }
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

 private:
  std::shared_ptr<BaseStrictObject> func_;
  std::shared_ptr<BaseStrictObject> inst_;
};

class StrictMethodType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<BaseStrictObject> loadAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> defaultValue,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& names,
      const CallerContext& caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual bool isCallable(const CallerContext& caller) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

// -----------------class (user) Method-------------------
class StrictClassMethod : public StrictInstance {
 public:
  StrictClassMethod(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> func);

  std::shared_ptr<BaseStrictObject> getFunc() const {
    return func_;
  }
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped method
  static std::shared_ptr<BaseStrictObject> classmethod__init__(
      std::shared_ptr<StrictClassMethod> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> func);

  static std::shared_ptr<BaseStrictObject> classmethod__get__(
      std::shared_ptr<StrictClassMethod> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> ctx);

 private:
  std::shared_ptr<BaseStrictObject> func_;
};

class StrictClassMethodType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;

  virtual void addMethods() override;
};

// -----------------static (user) Method-------------------
class StrictStaticMethod : public StrictInstance {
 public:
  StrictStaticMethod(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> func);

  std::shared_ptr<BaseStrictObject> getFunc() const {
    return func_;
  }
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped method
  static std::shared_ptr<BaseStrictObject> staticmethod__init__(
      std::shared_ptr<StrictStaticMethod> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> func);

  static std::shared_ptr<BaseStrictObject> staticmethod__get__(
      std::shared_ptr<StrictStaticMethod> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> ctx);

 private:
  std::shared_ptr<BaseStrictObject> func_;
};

class StrictStaticMethodType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;

  virtual void addMethods() override;
};

// Helpers to add method to types
template <typename T, typename... Args>
class CallableWrapper;

template <typename T, typename... Args>
class StarCallableWrapper;

template <int n>
class PythonWrappedCallableByName;

class PythonWrappedCallableDefaultByName;

template <typename T>
void StrictType::addMethod(const std::string& name, T func) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_, CallableWrapper(func, name), nullptr, name);
  setAttr(name, method);
}

extern std::shared_ptr<StrictType> ClassMethodType();

template <typename T>
void StrictType::addClassMethod(const std::string& name, T func) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_, CallableWrapper(func, name), nullptr, name);
  setAttr(
      name,
      std::make_shared<StrictClassMethod>(
          ClassMethodType(), creator_, std::move(method)));
}

template <typename T>
void StrictType::addStaticMethod(const std::string& name, T func) {
  auto method = std::make_shared<StrictBuiltinFunctionOrMethod>(
      creator_, CallableWrapper(func, name), nullptr, name);
  setAttr(name, method);
}

template <typename T>
void StrictType::addMethodDefault(
    const std::string& name,
    T func,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_,
      CallableWrapper(func, name, std::move(defaultValue)),
      nullptr,
      name);
  setAttr(name, method);
}

template <typename T>
void StrictType::addStaticMethodDefault(
    const std::string& name,
    T func,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  auto method = std::make_shared<StrictBuiltinFunctionOrMethod>(
      creator_,
      CallableWrapper(func, name, std::move(defaultValue)),
      nullptr,
      name);
  setAttr(name, method);
}

template <typename T>
void StrictType::addMethodKwargs(const std::string& name, T func) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_, StarCallableWrapper(func, name), nullptr, name);
  setAttr(name, method);
}

template <typename T>
void StrictType::addStaticMethodKwargs(const std::string& name, T func) {
  auto method = std::make_shared<StrictBuiltinFunctionOrMethod>(
      creator_, StarCallableWrapper(func, name), nullptr, name);
  setAttr(name, method);
}

template <typename T>
void StrictType::addMethodDescr(const std::string& name, T func) {
  auto method =
      std::make_shared<StrictMethodDescr>(creator_, func, nullptr, name);
  setAttr(name, method);
}

template <typename T>
void StrictType::addBuiltinFunctionOrMethod(const std::string& name, T func) {
  auto method = std::make_shared<StrictBuiltinFunctionOrMethod>(
      creator_, func, nullptr, name);
  setAttr(name, method);
}

template <int n, typename U>
void StrictType::addPyWrappedMethodObj(
    const std::string& name,
    PyObject* obj,
    U convertFunc) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_,
      InstCallType(PythonWrappedCallableByName<n>(obj, convertFunc, name)),
      nullptr,
      name);
  setAttr(name, method);
}

template <int n, typename U>
void StrictType::addPyWrappedStaticMethodObj(
    const std::string& name,
    PyObject* obj,
    U convertFunc) {
  auto method = std::make_shared<StrictBuiltinFunctionOrMethod>(
      creator_,
      InstCallType(PythonWrappedCallableByName<n>(obj, convertFunc, name)),
      nullptr,
      name);
  setAttr(name, method);
}

template <typename U>
void StrictType::addPyWrappedMethodDefaultObj(
    const std::string& name,
    PyObject* obj,
    U convertFunc,
    std::size_t numDefaultArgs,
    std::size_t numArgs) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_,
      InstCallType(PythonWrappedCallableDefaultByName(
          obj, convertFunc, name, numDefaultArgs, numArgs)),
      nullptr,
      name);
  setAttr(name, method);
}
} // namespace strictmod::objects
