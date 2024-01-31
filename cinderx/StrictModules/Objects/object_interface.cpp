// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/object_interface.h"

#include "cinderx/StrictModules/Compiler/abstract_module_loader.h"
#include "cinderx/StrictModules/Objects/base_object.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/Objects/type.h"

namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> iGetDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.getDescr(
      std::move(obj), std::move(inst), std::move(type), caller);
}

void iSetDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  obj_type.setDescr(std::move(obj), std::move(inst), std::move(value), caller);
}

void iDelDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  obj_type.delDescr(std::move(obj), std::move(inst), caller);
}

std::shared_ptr<BaseStrictObject> iLoadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.loadAttr(
      std::move(obj), key, std::move(defaultValue), caller);
}

std::shared_ptr<BaseStrictObject> iLoadAttrOnType(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  if (obj->isUnknown()) {
    caller.error<UnknownValueAttributeException>(obj->getDisplayName(), key);
  }
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr) {
    return iGetDescr(descr, obj, objType, caller);
  }
  return defaultValue;
}

void iStoreAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  obj_type.storeAttr(std::move(obj), key, std::move(value), caller);
}

void iDelAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.delAttr(std::move(obj), key, caller);
}

std::shared_ptr<BaseStrictObject> iBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.binOp(std::move(obj), std::move(right), op, caller);
}

std::shared_ptr<BaseStrictObject> iReverseBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> left,
    operator_ty op,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.reverseBinOp(std::move(obj), std::move(left), op, caller);
}

std::shared_ptr<BaseStrictObject> iDoBinOp(
    std::shared_ptr<BaseStrictObject> left,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller) {
  bool triedRight = false;
  auto lType = left->getType();
  auto rType = right->getType();
  if (lType != rType && rType->isSubType(lType)) {
    // do reverse op first
    auto result = iReverseBinOp(right, left, op, caller);
    if (result) {
      return result;
    }
    triedRight = true;
  }

  auto result = iBinOp(left, right, op, caller);
  if (result == nullptr and !triedRight) {
    result = iReverseBinOp(right, left, op, caller);
  }
  if (result == nullptr) {
    caller.raiseTypeError(
        "unsupported operand types for {}: {} and {}",
        kBinOpDisplays[op],
        lType->getName(),
        rType->getName());
  }
  return result;
}

std::shared_ptr<BaseStrictObject> iUnaryOp(
    std::shared_ptr<BaseStrictObject> obj,
    unaryop_ty op,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.unaryOp(std::move(obj), op, caller);
}

std::shared_ptr<BaseStrictObject> iBinCmpOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    cmpop_ty op,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.binCmpOp(std::move(obj), std::move(right), op, caller);
}

std::shared_ptr<StrictIteratorBase> iGetElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.getElementsIter(std::move(obj), caller);
}

std::vector<std::shared_ptr<BaseStrictObject>> iGetElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.getElementsVec(std::move(obj), caller);
}

std::shared_ptr<BaseStrictObject> iGetElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.getElement(std::move(obj), std::move(index), caller);
}

void iSetElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  obj_type.setElement(
      std::move(obj), std::move(index), std::move(value), caller);
}

bool iContainsElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  auto result = iBinCmpOp(std::move(index), std::move(obj), In, caller);
  return iGetTruthValue(std::move(result), caller) == StrictTrue();
}

void iDelElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.delElement(std::move(obj), std::move(index), caller);
}

std::shared_ptr<BaseStrictObject> iCall(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.call(std::move(obj), args, argNames, caller);
}

std::shared_ptr<BaseStrictObject> iGetTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  StrictType& obj_type = obj->getTypeRef();
  return obj_type.getTruthValue(std::move(obj), caller);
}

bool iStrictObjectEq(
    std::shared_ptr<BaseStrictObject> lhs,
    std::shared_ptr<BaseStrictObject> rhs,
    const CallerContext& caller) {
  if (lhs == rhs) {
    return true;
  }
  auto result = iBinCmpOp(std::move(lhs), std::move(rhs), Eq, caller);
  return iGetTruthValue(std::move(result), caller) == StrictTrue();
}

std::shared_ptr<BaseStrictObject> iImportFrom(
    std::shared_ptr<BaseStrictObject> fromMod,
    const std::string& name,
    const CallerContext& context,
    ModuleLoader* loader) {
  auto value = iLoadAttr(fromMod, name, nullptr, context);
  if (value != nullptr) {
    return value;
  }
  auto dunderPath = iLoadAttr(fromMod, "__path__", nullptr, context);
  if (dunderPath == nullptr) {
    // not a package
    return nullptr;
  }
  auto modName = iLoadAttr(fromMod, "__name__", nullptr, context);
  auto modNameStr = std::dynamic_pointer_cast<StrictString>(modName);
  if (modNameStr == nullptr) {
    return nullptr;
  }
  return loader->loadModuleValue(modNameStr->getValue() + "." + name);
}
} // namespace strictmod::objects
