// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/object_type.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context_impl.h"

namespace strictmod::objects {

std::unique_ptr<BaseStrictObject> StrictObjectType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
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
  if (descr && descr->getTypeRef().isDataDescr(caller)) {
    return iGetDescr(descr, obj, objType, caller);
  }
  // obj dict case
  auto value = assertStaticCast<StrictInstance>(obj)->getAttr(key);
  if (value) {
    return value;
  }
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
  if (descr && descr->getTypeRef().isDataDescr(caller)) {
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
  if (descr && descr->getTypeRef().isDataDescr(caller)) {
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
  if (rType->isSubType(lType) && rType != lType) {
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

  if (obj->isUnknown() || right->isUnknown()) {
    // error already raised when looking up rich compare functions
    return makeUnknown(caller, "{} {} {}", obj, kCmpOpDisplays[op], right);
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
      // special case type[<idx>] which always work
      if (typ == TypeType()) {
        return std::make_shared<StrictGenericAlias>(caller.caller, typ, index);
      }
      getItem = typ->typeLookup(kDunderClassGetItem, caller);
      if (getItem) {
        return iCall(getItem, {obj, index}, kEmptyArgNames, caller);
      }
    }
    caller.raiseTypeError(
        "'{}' object {} is not subscriptable",
        obj->getTypeRef().getDisplayName(),
        obj->getDisplayName());
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
    auto lenValue = lenInt->getValue();
    return lenValue == 0 ? StrictFalse() : StrictTrue();
  }
  // by default, bool(obj) should return True
  return StrictTrue();
}

std::shared_ptr<StrictType> StrictObjectType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictObjectType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictObjectType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec;
  baseVec.emplace_back(typeid(StrictObjectType));
  return baseVec;
}

static std::shared_ptr<BaseStrictObject> getDunderClass(
    std::shared_ptr<BaseStrictObject> value,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  return value->getType();
}

std::shared_ptr<BaseStrictObject> getDunderDictAllowed(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto instance = assertStaticCast<StrictInstance>(std::move(inst));
  return instance->getDunderDict();
}

std::shared_ptr<BaseStrictObject> getDunderDictDisallowed(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  return makeUnknown(caller, "{}.__dict__", inst);
}

void setDunderDict(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto instance = assertStaticCast<StrictInstance>(std::move(inst));
  auto dict = std::dynamic_pointer_cast<StrictDict>(value);
  if (dict == nullptr) {
    caller.raiseTypeError(
        "__dict__ must be set to a dict, not {} object",
        value->getTypeRef().getName());
  }
  checkExternalModification(instance, caller);
  const auto& dictData = dict->getData();
  auto newInstDictPtr = std::make_shared<DictType>();
  DictType& newInstDict = *newInstDictPtr;
  dictData.const_iter([&newInstDict, &caller](
                          std::shared_ptr<BaseStrictObject> k,
                          std::shared_ptr<BaseStrictObject> v) {
    auto kStr = std::dynamic_pointer_cast<StrictString>(k);
    if (kStr == nullptr) {
      caller.raiseTypeError("__dict__ expect string keys, got {}", k);
    }
    newInstDict[kStr->getValue()] = std::move(v);
    return true;
  });
  instance->setDict(std::move(newInstDictPtr));
}

void StrictObjectType::addMethods() {
  addMethodDescr(kDunderInit, object__init__);
  addBuiltinFunctionOrMethod("__new__", object__new__);
  addMethod("__eq__", object__eq__);
  addMethod("__ne__", object__ne__);
  addMethod("__ge__", object__othercmp__);
  addMethod("__gt__", object__othercmp__);
  addMethod("__le__", object__othercmp__);
  addMethod("__lt__", object__othercmp__);
  addMethod("__format__", object__format__);
  addMethod(kDunderRepr, object__repr__);
  addMethod("__hash__", object__hash__);
  addBuiltinFunctionOrMethod("__init_subclass__", object__init_subclass__);
  addGetSetDescriptor(kDunderClass, getDunderClass, nullptr, nullptr);
  addGetSetDescriptor(kDunderDict, getDunderDictDisallowed, nullptr, nullptr);
  addStringOptionalMemberDescriptor<StrictInstance, &StrictInstance::doc_>(
      "__doc__");
}

std::shared_ptr<BaseStrictObject> object__init__(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  if (obj->getType() == ObjectType() && !(args.empty() && namedArgs.empty())) {
    caller.raiseTypeError("object.__init__() takes not arguments");
  }
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> object__new__(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>&,
    const CallerContext& caller) {
  if (args.empty()) {
    caller.raiseTypeError("object.__new__(): not enough arguments");
  }
  auto arg1 = args[0];
  auto instType = std::dynamic_pointer_cast<StrictType>(arg1);
  if (instType == nullptr) {
    caller.raiseTypeError("{} is not a type object", arg1);
  }
  return instType->constructInstance(caller.caller);
}

std::shared_ptr<BaseStrictObject> object__eq__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject> other) {
  if (obj == other) {
    return StrictTrue();
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> object__ne__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> other) {
  auto eqFunc = iLoadAttrOnType(std::move(obj), "__eq__", nullptr, caller);
  if (eqFunc != nullptr) {
    auto eqRes = iCall(eqFunc, {std::move(other)}, kEmptyArgNames, caller);
    if (eqRes == NotImplemented()) {
      return eqRes;
    }
    auto resTruth = iGetTruthValue(std::move(eqRes), caller);
    if (resTruth->getType() == UnknownType()) {
      return resTruth;
    }
    return caller.makeBool(resTruth != StrictTrue());
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> object__othercmp__(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject>) {
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> object__format__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> formatSpec) {
  auto formatSpecStr = std::dynamic_pointer_cast<StrictString>(formatSpec);
  if (formatSpecStr == nullptr) {
    caller.raiseTypeError(
        "format spec must be str, not {}", formatSpec->getTypeRef().getName());
  }
  if (formatSpecStr->getValue() != "") {
    caller.raiseTypeError(
        "unsupported format spec {} passed to {}.__format__",
        formatSpecStr->getValue(),
        obj->getTypeRef().getName());
  }
  return iCall(StrType(), {std::move(obj)}, kEmptyArgNames, caller);
}

std::shared_ptr<BaseStrictObject> object__repr__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  return caller.makeStr(
      fmt::format("<{} object>", obj->getTypeRef().getName()));
}

std::shared_ptr<BaseStrictObject> object__hash__(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  return makeUnknown(caller, "{}.__hash__()", obj);
}

std::shared_ptr<BaseStrictObject> object__init_subclass__(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext&) {
  return NoneObject();
}
} // namespace strictmod::objects
