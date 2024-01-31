// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/property.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"

#include <fmt/format.h>
namespace strictmod::objects {

StrictProperty::StrictProperty(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> fget,
    std::shared_ptr<BaseStrictObject> fset,
    std::shared_ptr<BaseStrictObject> fdel)
    : StrictInstance(std::move(type), std::move(creator)),
      fget_(std::move(fget)),
      fset_(std::move(fset)),
      fdel_(std::move(fdel)),
      doc_() {}

std::string StrictProperty::getDisplayName() const {
  return "<property object>";
}

std::shared_ptr<BaseStrictObject> StrictProperty::copy(const CallerContext&) {
  return shared_from_this();
}

// wrapped functions
std::shared_ptr<BaseStrictObject> StrictProperty::property__init__(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  checkExternalModification(obj, caller);
  // implement arg parsing here because the wrapper cannot handle
  // defaults + keyword args
  bool seenGet = false, seenSet = false, seenDel = false,
       seenDuplicated = false;
  std::shared_ptr<BaseStrictObject> fget;
  std::shared_ptr<BaseStrictObject> fset;
  std::shared_ptr<BaseStrictObject> fdel;
  // named argument
  std::size_t namedOffset = args.size() - namedArgs.size();
  for (std::size_t i = 0; i < namedArgs.size(); ++i) {
    if (namedArgs[i] == "fget") {
      fget = args[i + namedOffset];
      seenGet = true;
    } else if (namedArgs[i] == "fset") {
      fset = args[i + namedOffset];
      seenSet = true;
    } else if (namedArgs[i] == "fdel") {
      fdel = args[i + namedOffset];
      seenDel = true;
    } else if (namedArgs[i] != "doc") {
      caller.raiseTypeError(
          "{} is not a valid keyword arg for property", namedArgs[i]);
    }
  }
  // positional argument
  if (namedOffset > 0) {
    seenDuplicated |= seenGet;
    fget = args[0];
  }
  if (namedOffset > 1) {
    seenDuplicated |= seenSet;
    fset = args[1];
  }
  if (namedOffset > 2) {
    seenDuplicated |= seenDel;
    fdel = args[2];
  }
  if (namedOffset > 4) {
    caller.raiseTypeError(
        "too many positional arguments for property, max 4 but got {}",
        namedOffset);
  }
  if (seenDuplicated) {
    caller.raiseTypeError("duplicated argument for property");
  }

  auto self = assertStaticCast<StrictProperty>(obj);
  if (fget != nullptr && fget != NoneObject()) {
    self->fget_ = std::move(fget);
  }
  if (fset != nullptr && fset != NoneObject()) {
    self->fset_ = std::move(fset);
  }
  if (fdel != nullptr && fdel != NoneObject()) {
    self->fdel_ = std::move(fdel);
  }

  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictProperty::propertyGetter(
    std::shared_ptr<StrictProperty> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  return std::make_shared<StrictProperty>(
      self->getType(), caller.caller, std::move(arg), self->fset_, self->fdel_);
}

std::shared_ptr<BaseStrictObject> StrictProperty::propertySetter(
    std::shared_ptr<StrictProperty> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  return std::make_shared<StrictProperty>(
      self->getType(), caller.caller, self->fget_, std::move(arg), self->fdel_);
}

std::shared_ptr<BaseStrictObject> StrictProperty::propertyDeleter(
    std::shared_ptr<StrictProperty> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  return std::make_shared<StrictProperty>(
      self->getType(), caller.caller, self->fget_, self->fset_, std::move(arg));
}

std::shared_ptr<BaseStrictObject> StrictProperty::property__get__(
    std::shared_ptr<StrictProperty> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject>) {
  if (inst == nullptr) {
    return self;
  }
  if (self->fget_ == nullptr) {
    caller.raiseExceptionStr(AttributeErrorType(), "unreadable property");
  }
  return iCall(self->fget_, {std::move(inst)}, kEmptyArgNames, caller);
}

std::shared_ptr<BaseStrictObject> StrictProperty::property__set__(
    std::shared_ptr<StrictProperty> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value) {
  if (self->fset_ == nullptr) {
    caller.raiseExceptionStr(AttributeErrorType(), "unwritable property");
  }
  iCall(
      self->fset_, {std::move(inst), std::move(value)}, kEmptyArgNames, caller);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictProperty::property__delete__(
    std::shared_ptr<StrictProperty> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  if (self->fdel_ == nullptr) {
    caller.raiseExceptionStr(AttributeErrorType(), "unwritable property");
  }
  iCall(self->fdel_, {std::move(inst)}, kEmptyArgNames, caller);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictPropertyType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictProperty>(obj);
  return StrictProperty::property__get__(
      std::move(self), caller, std::move(inst), std::move(type));
}

std::shared_ptr<BaseStrictObject> StrictPropertyType::setDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictProperty>(obj);
  return StrictProperty::property__set__(
      std::move(self), caller, std::move(inst), std::move(value));
}

std::shared_ptr<BaseStrictObject> StrictPropertyType::delDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictProperty>(obj);
  return StrictProperty::property__delete__(
      std::move(self), caller, std::move(inst));
}

std::unique_ptr<BaseStrictObject> StrictPropertyType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictProperty>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      caller,
      nullptr,
      nullptr,
      nullptr);
}

std::vector<std::type_index> StrictPropertyType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictPropertyType));
  return baseVec;
}

std::shared_ptr<StrictType> StrictPropertyType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictPropertyType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictPropertyType::addMethods() {
  addMethodDescr(kDunderInit, StrictProperty::property__init__);
  addMethod("getter", StrictProperty::propertyGetter);
  addMethod("setter", StrictProperty::propertySetter);
  addMethod("deleter", StrictProperty::propertyDeleter);
  addMethod("__get__", StrictProperty::property__get__);
  addMethod("__set__", StrictProperty::property__set__);
  addMethod("__delete__", StrictProperty::property__delete__);
  addStringOptionalMemberDescriptor<StrictProperty, &StrictProperty::doc_>(
      "__doc__");
}

bool StrictPropertyType::isDataDescr(const CallerContext&) {
  // properties are data descriptors
  return true;
}

// GetSetDescriptor

StrictGetSetDescriptor::StrictGetSetDescriptor(
    std::weak_ptr<StrictModuleObject> creator,
    std::string name,
    TDescrGetFunc fget,
    TDescrSetFunc fset,
    TDescrDelFunc fdel)
    : StrictInstance(GetSetDescriptorType(), std::move(creator)),
      name_(std::move(name)),
      fget_(fget),
      fset_(fset),
      fdel_(fdel) {}

std::shared_ptr<BaseStrictObject> StrictGetSetDescriptor::copy(
    const CallerContext&) {
  return shared_from_this();
}

// wrapped funcitons
std::shared_ptr<BaseStrictObject> StrictGetSetDescriptor::getsetdescr__get__(
    std::shared_ptr<StrictGetSetDescriptor> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> type) {
  if (inst == nullptr) {
    return self;
  }
  auto t = assertStaticCast<StrictType>(std::move(type));
  return self->getFget()(std::move(inst), std::move(t), caller);
}

std::shared_ptr<BaseStrictObject> StrictGetSetDescriptor::getsetdescr__set__(
    std::shared_ptr<StrictGetSetDescriptor> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value) {
  if (self->getFset() != nullptr) {
    self->getFset()(std::move(inst), std::move(value), caller);
    return NoneObject();
  }
  caller.raiseExceptionStr(
      AttributeErrorType(), "readonly attribute {}", self->getName());
}

std::shared_ptr<BaseStrictObject> StrictGetSetDescriptor::getsetdescr__delete__(
    std::shared_ptr<StrictGetSetDescriptor> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  if (self->getFdel() != nullptr) {
    self->getFdel()(std::move(inst), caller);
    return NoneObject();
  }
  caller.raiseExceptionStr(
      AttributeErrorType(), "readonly attribute {}", self->getName());
}

std::shared_ptr<BaseStrictObject> StrictGetSetDescriptorType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  auto descr = assertStaticCast<StrictGetSetDescriptor>(std::move(obj));
  return StrictGetSetDescriptor::getsetdescr__get__(
      std::move(descr), caller, std::move(inst), std::move(type));
}

std::shared_ptr<BaseStrictObject> StrictGetSetDescriptorType::setDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto descr = assertStaticCast<StrictGetSetDescriptor>(std::move(obj));
  return StrictGetSetDescriptor::getsetdescr__set__(
      std::move(descr), caller, std::move(inst), std::move(value));
}

std::shared_ptr<BaseStrictObject> StrictGetSetDescriptorType::delDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext& caller) {
  auto descr = assertStaticCast<StrictGetSetDescriptor>(std::move(obj));
  return StrictGetSetDescriptor::getsetdescr__delete__(
      std::move(descr), caller, std::move(inst));
}

std::vector<std::type_index> StrictGetSetDescriptorType::getBaseTypeinfos()
    const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictGetSetDescriptorType));
  return baseVec;
}

std::shared_ptr<StrictType> StrictGetSetDescriptorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictGetSetDescriptorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictGetSetDescriptorType::addMethods() {
  addMethod("__get__", StrictGetSetDescriptor::getsetdescr__get__);
  addMethod("__set__", StrictGetSetDescriptor::getsetdescr__set__);
  addMethod("__delete__", StrictGetSetDescriptor::getsetdescr__delete__);
}

bool StrictGetSetDescriptorType::isDataDescr(const CallerContext&) {
  return true;
}

} // namespace strictmod::objects
