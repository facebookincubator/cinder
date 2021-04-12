#include "StrictModules/Objects/object_interface.h"

#include "StrictModules/Objects/base_object.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/Objects/type.h"

namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> iGetDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  return obj->getTypeRef().getDescr(
      std::move(obj), std::move(inst), std::move(type), caller);
}

void iSetDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  obj->getTypeRef().setDescr(
      std::move(obj), std::move(inst), std::move(value), caller);
}

void iDelDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext& caller) {
  obj->getTypeRef().delDescr(std::move(obj), std::move(inst), caller);
}

std::shared_ptr<BaseStrictObject> iLoadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  return obj->getTypeRef().loadAttr(
      std::move(obj), key, std::move(defaultValue), caller);
}

std::shared_ptr<BaseStrictObject> iLoadAttrOnType(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
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
  obj->getTypeRef().storeAttr(std::move(obj), key, std::move(value), caller);
}

void iDelAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    const CallerContext& caller) {
  return obj->getTypeRef().delAttr(std::move(obj), key, caller);
}

std::shared_ptr<BaseStrictObject> iBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller) {
  return obj->getTypeRef().binOp(std::move(obj), std::move(right), op, caller);
}

std::shared_ptr<BaseStrictObject> iReverseBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> left,
    operator_ty op,
    const CallerContext& caller) {
  return obj->getTypeRef().reverseBinOp(
      std::move(obj), std::move(left), op, caller);
}

std::shared_ptr<BaseStrictObject> iUnaryOp(
    std::shared_ptr<BaseStrictObject> obj,
    unaryop_ty op,
    const CallerContext& caller) {
  return obj->getTypeRef().unaryOp(std::move(obj), op, caller);
}

std::shared_ptr<BaseStrictObject> iBinCmpOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    cmpop_ty op,
    const CallerContext& caller) {
  return obj->getTypeRef().binCmpOp(
      std::move(obj), std::move(right), op, caller);
}

std::shared_ptr<BaseStrictObject> iGetElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  return obj->getTypeRef().getElement(std::move(obj), std::move(index), caller);
}

void iSetElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  obj->getTypeRef().setElement(
      std::move(obj), std::move(index), std::move(value), caller);
}

void iDelElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  return obj->getTypeRef().delElement(std::move(obj), std::move(index), caller);
}

std::shared_ptr<BaseStrictObject> iCall(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller) {
  return obj->getTypeRef().call(std::move(obj), args, argNames, caller);
}

std::shared_ptr<BaseStrictObject> iGetTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  return obj->getTypeRef().getTruthValue(std::move(obj), caller);
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
} // namespace strictmod::objects
