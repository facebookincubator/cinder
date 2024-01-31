// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/module.h"
#include "cinderx/StrictModules/caller_context.h"

#include <memory>
#include <typeindex>
#include <vector>

namespace strictmod::objects {
class StrictIteratorBase;
class StrictTypeType;
class StrictType : public StrictInstance {
 public:
  friend class StrictTypeType;
  StrictType(
      std::string name,
      std::shared_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<StrictType> metatype,
      bool immutable = true);

  StrictType(
      std::string name,
      std::weak_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool immutable);

  std::vector<std::shared_ptr<BaseStrictObject>> getBaseClasses() const {
    return baseClasses_;
  }
  void setBases(std::vector<std::shared_ptr<BaseStrictObject>> bases) {
    baseClasses_ = std::move(bases);
  }

  std::string getName() const {
    return name_;
  }

  std::string getCreatorModuleName() const {
    return creator_.lock()->getModuleName();
  }

  bool isImmutable() const {
    return immutable_;
  }

  void setIsImmutable(bool immutable) {
    immutable_ = immutable;
  }

  bool isSubType(std::shared_ptr<StrictType> base) const;
  const std::vector<std::shared_ptr<const BaseStrictObject>>& mro() const;
  std::shared_ptr<BaseStrictObject> typeLookup(
      const std::string& name,
      const CallerContext& caller);

  bool hasSubLayout(std::shared_ptr<StrictType> other) const;

  virtual std::string getDisplayName() const override;

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  virtual bool isBaseType() const;
  virtual bool isCallable(const CallerContext& caller);
  virtual bool isDataDescr(const CallerContext& caller);

  virtual void addMethods();

  virtual void cleanContent(const StrictModuleObject* owner) override;

  // recreate the same instance with updated field
  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) = 0;

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
      std::weak_ptr<StrictModuleObject> caller) = 0;

  virtual std::vector<std::type_index> getBaseTypeinfos() const = 0;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> type__call__(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> type__new__(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> typeMro(
      std::shared_ptr<StrictType> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> type__subclasscheck__(
      std::shared_ptr<StrictType> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> derived);

  static std::shared_ptr<BaseStrictObject> type__or__(
      std::shared_ptr<StrictType> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> type__ror__(
      std::shared_ptr<StrictType> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> type__bases__Getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> type__module__Getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static void type__module__Setter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> type__mro__Getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> type__qualname__Getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType>,
      const CallerContext& caller);

  // helpers to add builtin methods to types

  template <typename T>
  void addMethod(const std::string& name, T func);

  template <typename T>
  void addClassMethod(const std::string& name, T func);

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

  template <int n = 0, typename U>
  void
  addPyWrappedMethodObj(const std::string& name, PyObject* obj, U convertFunc);

  template <int n = 0, typename U>
  void addPyWrappedStaticMethodObj(
      const std::string& name,
      PyObject* obj,
      U convertFunc);

  template <typename U>
  void addPyWrappedMethodDefaultObj(
      const std::string& name,
      PyObject* obj,
      U convertFunc,
      std::size_t numDefaultArgs,
      std::size_t numArgs);

  template <typename G, typename S, typename D>
  void addGetSetDescriptor(
      const std::string& name,
      G getter,
      S setter = nullptr,
      D deleter = nullptr);

  template <typename T, std::string T::*mp>
  void addStringMemberDescriptor(const std::string& name);

  template <typename T, std::optional<std::string> T::*mp>
  void addStringOptionalMemberDescriptor(const std::string& name);

 private:
  std::string name_;
  std::vector<std::shared_ptr<BaseStrictObject>> baseClasses_;
  bool immutable_;
  mutable std::optional<std::vector<std::shared_ptr<const BaseStrictObject>>>
      mro_;
  mutable std::optional<bool> isDataDescr_;
  mutable std::shared_ptr<BaseStrictObject> basesObj_;
  mutable std::optional<bool> isCallable_;
};

template <typename T>
std::shared_ptr<StrictType> createType(
    std::string name,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool immutable) {
  return std::make_shared<T>(
      std::move(name),
      std::move(creator),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      immutable);
}

} // namespace strictmod::objects
