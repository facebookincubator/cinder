// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/type.h"

namespace strictmod::objects {

class StrictObjectType : public StrictType {
 public:
  using StrictType::StrictType;

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> setDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> delDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> loadAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> defaultValue,
      const CallerContext& caller) override;

  virtual void storeAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual void delAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> binOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> right,
      operator_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> reverseBinOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> left,
      operator_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> unaryOp(
      std::shared_ptr<BaseStrictObject> obj,
      unaryop_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> binCmpOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> right,
      cmpop_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<StrictIteratorBase> getElementsIter(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::vector<std::shared_ptr<BaseStrictObject>> getElementsVec(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> getElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      const CallerContext& caller) override;

  virtual void setElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual void delElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& argNames,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual void addMethods() override;

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
};

// wrapped methods for object
std::shared_ptr<BaseStrictObject> object__init__(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> object__new__(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> object__eq__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> other);

std::shared_ptr<BaseStrictObject> object__ne__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> other);

std::shared_ptr<BaseStrictObject> object__othercmp__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> other);

std::shared_ptr<BaseStrictObject> object__format__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> formatSpec);

std::shared_ptr<BaseStrictObject> object__repr__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> object__hash__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> object__init_subclass__(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext&);

// operations on __dict__
std::shared_ptr<BaseStrictObject> getDunderDictAllowed(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> getDunderDictDisallowed(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller);

void setDunderDict(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller);
} // namespace strictmod::objects
