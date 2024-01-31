// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/unknown.h"

#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/exceptions.h"

#include <stdexcept>
namespace strictmod::objects {

// UnknownObject
UnknownObject::UnknownObject(
    std::string name,
    std::shared_ptr<StrictModuleObject> creator)
    : UnknownObject(std::move(name), std::weak_ptr(creator)) {}

UnknownObject::UnknownObject(
    std::string name,
    std::weak_ptr<StrictModuleObject> creator)
    : BaseStrictObject(UnknownType(), std::move(creator)),
      name_(std::move(name)) {}

std::string UnknownObject::getDisplayName() const {
  return name_;
}

std::shared_ptr<BaseStrictObject> UnknownObject::copy(const CallerContext&) {
  return shared_from_this();
}

// UnknownObjectType
std::unique_ptr<BaseStrictObject> UnknownObjectType::constructInstance(
    std::weak_ptr<StrictModuleObject>) {
  throw std::runtime_error("should not call constructInstance on unknown");
}

std::shared_ptr<StrictType> UnknownObjectType::recreate(
    std::string,
    std::weak_ptr<StrictModuleObject>,
    std::vector<std::shared_ptr<BaseStrictObject>>,
    std::shared_ptr<DictType>,
    std::shared_ptr<StrictType>,
    bool) {
  throw std::runtime_error("should not call recreate on unknown");
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  // Don't run the descriptor protocol on unknowns, we'll report the error
  // on the following load_attr which makes more sense than an error on
  // accessing "__get__"
  return obj;
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::setDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("Not implemented: set_descr");
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::delDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("Not implemented: del_descr");
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::loadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  std::string displayName = obj->getDisplayName();

  caller.error<UnknownValueAttributeException>(displayName, key);
  if (defaultValue) {
    return defaultValue;
  }
  return makeUnknown(caller, "{}.{}", std::move(displayName), key);
}

void UnknownObjectType::storeAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  caller.error<UnknownValueAttributeException>(obj->getDisplayName(), key);
}

void UnknownObjectType::delAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    const CallerContext& caller) {
  caller.error<UnknownValueAttributeException>(obj->getDisplayName(), key);
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::binOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller) {
  caller.error<UnknownValueBinaryOpException>(
      obj->getDisplayName(), kBinOpDisplays[op], right->getDisplayName());
  return makeUnknown(caller, "{} {} {}", obj, right, kBinOpDisplays[op]);
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::reverseBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> left,
    operator_ty op,
    const CallerContext& caller) {
  caller.error<UnknownValueBinaryOpException>(
      obj->getDisplayName(), kBinOpDisplays[op], left->getDisplayName());
  return makeUnknown(caller, "{} {} {}", left, obj, kBinOpDisplays[op]);
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::unaryOp(
    std::shared_ptr<BaseStrictObject> obj,
    unaryop_ty op,
    const CallerContext& caller) {
  std::string opName = kUnaryOpDisplays[op];
  std::string displayName = obj->getDisplayName();
  assert(opName != "");
  caller.error<UnknownValueUnaryOpException>(opName, displayName);
  return makeUnknown(caller, "{}{}", std::move(opName), std::move(displayName));
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::binCmpOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    cmpop_ty op,
    const CallerContext& caller) {
  // is and is not, object identity
  if (op == Is || op == IsNot) {
    return (obj == right) ^ (op == Is) ? StrictFalse() : StrictTrue();
  }
  std::string displayName = obj->getDisplayName();
  std::string rDisplayName = right->getDisplayName();
  std::string opName = kCmpOpDisplays[op];
  caller.error<UnknownValueBinaryOpException>(
      displayName, opName, rDisplayName);

  return makeUnknown(
      caller,
      "{}{}{}",
      std::move(displayName),
      std::move(opName),
      std::move(rDisplayName));
}

std::shared_ptr<StrictIteratorBase> UnknownObjectType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  caller.error<UnknownValueNotIterableException>(obj->getDisplayName());
  std::vector<std::shared_ptr<BaseStrictObject>> unknownVec;
  unknownVec.emplace_back(
      makeUnknown(caller, "{}[...]", obj->getDisplayName()));

  auto unknownTuple = std::make_shared<StrictTuple>(
      TupleType(), caller.caller, std::move(unknownVec));
  return std::make_shared<StrictSequenceIterator>(
      SequenceIteratorType(), caller.caller, std::move(unknownTuple));
}

std::vector<std::shared_ptr<BaseStrictObject>>
UnknownObjectType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  caller.error<UnknownValueNotIterableException>(obj->getDisplayName());
  auto unknown = makeUnknown(caller, "{}[...]", obj->getDisplayName());
  return {unknown};
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::getElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  std::string displayName = obj->getDisplayName();
  std::string idxDisplayName = index->getDisplayName();
  caller.error<UnknownValueIndexException>(displayName, idxDisplayName);
  return makeUnknown(
      caller, "{}[{}]", std::move(displayName), std::move(idxDisplayName));
}

void UnknownObjectType::setElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  caller.error<UnknownValueIndexException>(
      obj->getDisplayName(), index->getDisplayName());
}

void UnknownObjectType::delElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller) {
  caller.error<UnknownValueIndexException>(
      obj->getDisplayName(), index->getDisplayName());
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::call(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller) {
  std::string displayName = obj->getDisplayName();
  caller.error<UnknownValueCallException>(displayName);
  return makeUnknown(caller, "{}({})", displayName, formatArgs(args, argNames));
}

std::shared_ptr<BaseStrictObject> UnknownObjectType::getTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  std::string displayName = obj->getDisplayName();
  caller.error<UnknownValueBoolException>(displayName);
  return makeUnknown(caller, "bool({})", std::move(displayName));
}

std::vector<std::type_index> UnknownObjectType::getBaseTypeinfos() const {
  throw std::runtime_error("should not call getBaseTypeinfos on unknown");
}
} // namespace strictmod::objects
