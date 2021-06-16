// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_TYPE_H__
#define __STRICTM_TYPE_H__

#include <memory>
#include <vector>
#include "StrictModules/Objects/module.h"
#include "StrictModules/caller_context.h"

#include "Python.h"

#include "Python-ast.h"

namespace strictmod::objects {
class StrictIteratorBase;
class StrictType : public StrictInstance {
 public:
  StrictType(
      std::string name,
      std::shared_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<StrictType> metatype,
      bool immutable = true);

  std::vector<std::shared_ptr<BaseStrictObject>> getBaseClasses() const {
    return baseClasses_;
  }
  void setBases(std::vector<std::shared_ptr<BaseStrictObject>> bases) {
    baseClasses_ = std::move(bases);
  }

  std::string getName() const {
    return name_;
  }

  std::string getModuleName() const {
    return moduleName_;
  }
  void setModuleName(std::string name) {
    moduleName_ = std::move(name);
  }

  bool isImmutable() const {
    return immutable_;
  }

  bool is_subtype(std::shared_ptr<StrictType> base) const;
  std::vector<std::shared_ptr<const BaseStrictObject>> mro() const;
  std::shared_ptr<BaseStrictObject> typeLookup(
      const std::string& name,
      const CallerContext& caller);

  virtual std::string getDisplayName() const override;

  virtual bool isBaseType() const;
  virtual bool isDataDescr() const;

  virtual void addMethods();

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> setDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> delDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> loadAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> defaultValue,
      const CallerContext& caller) = 0;

  virtual void storeAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) = 0;

  virtual void delAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> binOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> right,
      operator_ty op,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> reverseBinOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> left,
      operator_ty op,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> unaryOp(
      std::shared_ptr<BaseStrictObject> obj,
      unaryop_ty op,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> binCmpOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> right,
      cmpop_ty op,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<StrictIteratorBase> getElementsIter(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) = 0;

  virtual std::vector<std::shared_ptr<BaseStrictObject>> getElementsVec(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> getElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      const CallerContext& caller) = 0;

  virtual void setElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) = 0;

  virtual void delElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& argNames,
      const CallerContext& caller) = 0;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) = 0;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::shared_ptr<StrictModuleObject> caller) = 0;

  template <typename T>
  void addMethod(const std::string& name, T func);

  template <typename T>
  void addStaticMethod(const std::string& name, T func);

  template <typename T>
  void addMethodDefault(
      const std::string& name,
      T func,
      std::shared_ptr<BaseStrictObject> defaultValue);

  template <typename T>
  void addStaticMethodDefault(
      const std::string& name,
      T func,
      std::shared_ptr<BaseStrictObject> defaultValue);

  template <typename T>
  void addMethodKwargs(const std::string& name, T func);

  template <typename T>
  void addStaticMethodKwargs(const std::string& name, T func);

  template <typename T>
  void addMethodDescr(const std::string& name, T func);

  template <typename T>
  void addBuiltinFunctionOrMethod(const std::string& name, T func);

 private:
  std::string name_;
  std::string moduleName_;
  std::vector<std::shared_ptr<BaseStrictObject>> baseClasses_;
  bool immutable_;
  mutable std::optional<std::vector<std::shared_ptr<const BaseStrictObject>>>
      mro_;
};
} // namespace strictmod::objects
#endif // !__STRICTM_TYPE_H__
