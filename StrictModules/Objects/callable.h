// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_CALLABLE_H__
#define __STRICTM_CALLABLE_H__
#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/object_type.h"

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

 private:
  std::shared_ptr<BaseStrictObject> func_;
  std::shared_ptr<BaseStrictObject> inst_;
};

class StrictMethodType : public StrictObjectType {
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
};

template <typename T, typename... Args>
class CallableWrapper;

template <typename T>
void StrictType::addMethod(const std::string& name, T func) {
  auto method = std::make_shared<StrictMethodDescr>(
      creator_, CallableWrapper(func, name), nullptr, name);
  setAttr(name, method);
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
} // namespace strictmod::objects

#endif //__STRICTM_CALLABLE_H__
