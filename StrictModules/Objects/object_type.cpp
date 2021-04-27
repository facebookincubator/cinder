#include "StrictModules/Objects/object_type.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context_impl.h"

namespace strictmod::objects {

std::unique_ptr<BaseStrictObject> StrictObjectType::constructInstance(
    std::shared_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictInstance>(
      std::static_pointer_cast<StrictType>(shared_from_this()), caller);
}

std::shared_ptr<BaseStrictObject> StrictObjectType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  auto get = iLoadAttrOnType(obj, kDunderGet, nullptr, caller);
  std::vector<std::shared_ptr<BaseStrictObject>> posArg{
      inst ? inst : NoneObject(), type};
  if (get) {
    return iCall(get, posArg, {}, caller);
  }
  return obj;
}

std::shared_ptr<BaseStrictObject> StrictObjectType::setDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("Not implemented: set_descr");
}

std::shared_ptr<BaseStrictObject> StrictObjectType::delDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("Not implemented: del_descr");
}

std::shared_ptr<BaseStrictObject> StrictObjectType::loadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  // data descr case
  if (descr && descr->getTypeRef().isDataDescr()) {
    return iGetDescr(descr, obj, objType, caller);
  }
  // obj dict case
  auto value = assertStaticCast<StrictInstance>(obj)->getAttr(key);
  // non data descr case
  if (descr) {
    return iGetDescr(descr, obj, objType, caller);
  }
  return defaultValue;
}

void StrictObjectType::storeAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr && descr->getTypeRef().isDataDescr()) {
    iSetDescr(descr, obj, value, caller);
    return;
  }

  if (isImmutable()) {
    caller.error<ImmutableException>(key, "object", obj->getDisplayName());
    return;
  }
  // check external modification
  checkExternalModification(obj, caller);
  assertStaticCast<StrictInstance>(obj)->setAttr(key, value);
}

void StrictObjectType::delAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr && descr->getTypeRef().isDataDescr()) {
    iDelDescr(descr, obj, caller);
    return;
  }

  if (isImmutable()) {
    caller.error<ImmutableException>(key, "object", obj->getDisplayName());
    return;
  }

  checkExternalModification(obj, caller);
  assertStaticCast<StrictInstance>(obj)->setAttr(key, nullptr);
}

std::shared_ptr<BaseStrictObject> StrictObjectType::binOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller) {
  const std::string& func_name = kBinOpNames[op];
  assert(func_name != "");
  auto func = iLoadAttrOnType(obj, func_name, nullptr, caller);
  if (!func) {
    return nullptr;
  }
  auto result = iCall(func, {right}, {}, caller);
  if (result != NotImplemented()) {
    return result;
  }
  return nullptr;
}

std::shared_ptr<BaseStrictObject> StrictObjectType::reverseBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> left,
    operator_ty op,
    const CallerContext& caller) {
  const std::string& func_name = kRBinOpNames[op];
  assert(func_name != "");
  auto func = iLoadAttrOnType(obj, func_name, nullptr, caller);
  if (!func) {
    return nullptr;
  }
  auto result = iCall(func, {left}, {}, caller);
  if (result != NotImplemented()) {
    return result;
  }
  return nullptr;
}

std::shared_ptr<BaseStrictObject> StrictObjectType::unaryOp(
    std::shared_ptr<BaseStrictObject> obj,
    unaryop_ty op,
    const CallerContext& caller) {
  const std::string& func_name = kUnaryOpNames[op];
  assert(func_name != "");
  auto func = iLoadAttrOnType(obj, func_name, nullptr, caller);
  if (func) {
    return iCall(func, {}, {}, caller);
  }

  caller.raiseTypeError(
      "bad operand type for unary {}: '{}'", kUnaryOpDisplays[op], getName());
}

std::shared_ptr<BaseStrictObject> StrictObjectType::binCmpOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    cmpop_ty op,
    const CallerContext& caller) {
  // is and is not, object identity
  if (op == Is || op == IsNot) {
    return (obj == right) ^ (op == Is) ? StrictFalse() : StrictTrue();
  }
  std::vector<std::shared_ptr<BaseStrictObject>> objArg{obj};
  std::vector<std::shared_ptr<BaseStrictObject>> rightArg{right};
  // contains
  if (op == In || op == NotIn) {
    auto func = iLoadAttrOnType(right, kDunderContains, nullptr, caller);
    if (func) {
      auto result = iCall(func, objArg, kEmptyArgNames, caller);
      auto resultBool = iGetTruthValue(result, caller);
      if (resultBool == StrictFalse() || resultBool == StrictTrue()) {
        return (op == In) ^ (resultBool == StrictTrue()) ? StrictFalse()
                                                         : StrictTrue();
      } else {
        return resultBool;
      }
    }

    caller.raiseTypeError("argument of type {} is not iterable", getName());
  }
  // other symmetric compares
  auto& funcname = kCmpOpNames[op];
  auto& rfuncname = kRCmpOpNames[op];
  bool checkedReflected = false;
  auto rType = right->getType();
  auto lType = obj->getType();
  if (rType->is_subtype(lType) && rType != lType) {
    // right has a subtype of obj, use the reflected method on right
    checkedReflected = true;
    auto rRightFunc = iLoadAttrOnType(right, rfuncname, nullptr, caller);
    if (rRightFunc) {
      auto result = iCall(rRightFunc, objArg, kEmptyArgNames, caller);
      if (result != NotImplemented()) {
        return result;
      }
    }
  }
  // use rich comparison on left
  auto leftFunc = iLoadAttrOnType(obj, funcname, nullptr, caller);
  if (leftFunc) {
    auto result = iCall(leftFunc, rightArg, kEmptyArgNames, caller);
    if (result != NotImplemented()) {
      return result;
    }
  }
  // use rich comparison on right
  if (!checkedReflected) {
    auto rRightFunc = iLoadAttrOnType(right, rfuncname, nullptr, caller);
    if (rRightFunc) {
      auto result = iCall(rRightFunc, objArg, kEmptyArgNames, caller);
      if (result != NotImplemented()) {
        return result;
      }
    }
  }
  // default implementation of Eq and NotEq
  if (op == Eq || op == NotEq) {
    return (obj == right) ^ (op == Eq) ? StrictFalse() : StrictTrue();
  }

  caller.raiseTypeError(
      "'{}' is not supported between objects of type '{}' and '{}'",
      kCmpOpDisplays[op],
      lType->getDisplayName(),
      rType->getDisplayName());
}

std::shared_ptr<StrictIteratorBase> StrictObjectType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  auto iterFunc = iLoadAttrOnType(obj, kDunderIter, nullptr, caller);
  if (iterFunc == nullptr) {
    caller.raiseExceptionStr(
        TypeErrorType(),
        "{} object is not iterable",
        obj->getType()->getName());
  }
  auto iterResult =
      iCall(std::move(iterFunc), kEmptyArgs, kEmptyArgNames, caller);
  auto nextFunc = iLoadAttrOnType(iterResult, kDunderNext, nullptr, caller);
  if (nextFunc == nullptr) {
    caller.raiseExceptionStr(
        TypeErrorType(),
        "iter({}) returned non-iterator type of {}",
        obj,
        iterResult->getType()->getName());
  }
  return std::make_shared<StrictGenericObjectIterator>(
      GenericObjectIteratorType(), caller.caller, std::move(nextFunc));
}

std::vector<std::shared_ptr<BaseStrictObject>> StrictObjectType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  std::shared_ptr<StrictIteratorBase> it =
      getElementsIter(std::move(obj), caller);
  std::vector<std::shared_ptr<BaseStrictObject>> vec;
  while (true) {
    auto nextValue = it->next(caller);
    if (it->isEnd()) {
      break;
    }
    vec.push_back(std::move(nextValue));
  }
  return vec;
}

std::shared_ptr<BaseStrictObject> StrictObjectType::getElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  auto getItem = iLoadAttrOnType(obj, kDunderGetItem, nullptr, caller);
  if (!getItem) {
    std::shared_ptr<StrictType> typ =
        std::dynamic_pointer_cast<StrictType>(obj);
    if (typ) {
      getItem = typ->typeLookup(kDunderClassGetItem, caller);
      if (getItem) {
        return iCall(getItem, {obj, index}, kEmptyArgNames, caller);
      }
    } else {
      caller.raiseTypeError(
          "'{}' object is not subscriptable",
          obj->getTypeRef().getDisplayName());
    }
  }
  return iCall(getItem, {index}, kEmptyArgNames, caller);
}

void StrictObjectType::setElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto setItem = iLoadAttrOnType(obj, kDunderSetItem, nullptr, caller);
  if (!setItem) {
    caller.raiseTypeError(
        "'{}' object does not support item assignment",
        obj->getTypeRef().getDisplayName());
  }
  iCall(setItem, {index, value}, {}, caller);
}

void StrictObjectType::delElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  auto delItem = iLoadAttrOnType(obj, kDunderDelItem, nullptr, caller);
  if (!delItem) {
    caller.raiseTypeError(
        "'{}' object does not support item deletion",
        obj->getTypeRef().getDisplayName());
  }
  iCall(delItem, {index}, {}, caller);
}

std::shared_ptr<BaseStrictObject> StrictObjectType::call(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller) {
  auto dunderCall = iLoadAttrOnType(obj, kDunderCall, nullptr, caller);
  if (!dunderCall) {
    caller.raiseTypeError(
        "'{}' object is not callable", obj->getTypeRef().getDisplayName());
  }
  return iCall(dunderCall, args, argNames, caller);
}

std::shared_ptr<BaseStrictObject> StrictObjectType::getTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  auto funcBool = iLoadAttrOnType(obj, kDunderBool, nullptr, caller);
  if (funcBool) {
    auto result = iCall(funcBool, {}, {}, caller);
    if (result != StrictFalse() && result != StrictTrue()) {
      caller.raiseTypeError(
          "{}.__bool__ should return bool, but got {}",
          obj->getDisplayName(),
          result->getDisplayName());
    }
    return result;
  }
  auto funcLen = iLoadAttrOnType(obj, kDunderLen, nullptr, caller);
  if (funcLen) {
    auto len = iCall(funcLen, {}, {}, caller);
    std::shared_ptr<StrictInt> lenInt =
        std::dynamic_pointer_cast<StrictInt>(len);
    if (!lenInt) {
      caller.raiseTypeError(
          "{}.__len__ returned {} which cannot be interpreted as int",
          obj->getDisplayName(),
          len->getDisplayName());
    }
    long lenValue = lenInt->getValue();
    return lenValue > 0 ? StrictTrue() : StrictFalse();
  }
  // by default, bool(obj) should return True
  return StrictTrue();
}
} // namespace strictmod::objects
