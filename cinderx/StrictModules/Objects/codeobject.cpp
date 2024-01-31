// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/codeobject.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"
namespace strictmod::objects {
StrictCodeObject::StrictCodeObject(
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictString> name,
    std::shared_ptr<StrictInt> argCount,
    std::shared_ptr<StrictInt> posOnlyArgCount,
    std::shared_ptr<StrictInt> kwOnlyArgCount,
    std::shared_ptr<StrictInt> flags,
    std::shared_ptr<BaseStrictObject> varNames)
    : StrictInstance(CodeObjectType(), std::move(creator)),
      name_(std::move(name)),
      argCount_(std::move(argCount)),
      posOnlyArgCount_(std::move(posOnlyArgCount)),
      kwOnlyArgCount_(std::move(kwOnlyArgCount)),
      flags_(std::move(flags)),
      varNames_(std::move(varNames)) {}

std::shared_ptr<BaseStrictObject> StrictCodeObject::copy(const CallerContext&) {
  return shared_from_this();
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictCodeObject::codeArgCountGetter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictCodeObject>(std::move(inst));
  return self->argCount_;
}

std::shared_ptr<BaseStrictObject> StrictCodeObject::codePosOnlyArgCountGetter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictCodeObject>(std::move(inst));
  return self->posOnlyArgCount_;
}

std::shared_ptr<BaseStrictObject> StrictCodeObject::codeNameGetter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictCodeObject>(std::move(inst));
  return self->name_;
}

std::shared_ptr<BaseStrictObject> StrictCodeObject::codeFlagsGetter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictCodeObject>(std::move(inst));
  return self->flags_;
}

std::shared_ptr<BaseStrictObject> StrictCodeObject::codeVarnamesGetter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictCodeObject>(std::move(inst));
  return self->varNames_;
}

std::shared_ptr<BaseStrictObject> StrictCodeObject::codeKwOnlyArgCountGetter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictCodeObject>(std::move(inst));
  return self->kwOnlyArgCount_;
}

std::shared_ptr<StrictType> StrictCodeObjectType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictCodeObjectType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictCodeObjectType::addMethods() {
  addGetSetDescriptor(
      "co_argcount", StrictCodeObject::codeArgCountGetter, nullptr, nullptr);
  addGetSetDescriptor(
      "co_posonlyargcount",
      StrictCodeObject::codePosOnlyArgCountGetter,
      nullptr,
      nullptr);
  addGetSetDescriptor(
      "co_name", StrictCodeObject::codeNameGetter, nullptr, nullptr);
  addGetSetDescriptor(
      "co_flags", StrictCodeObject::codeFlagsGetter, nullptr, nullptr);
  addGetSetDescriptor(
      "co_varnames", StrictCodeObject::codeVarnamesGetter, nullptr, nullptr);
  addGetSetDescriptor(
      "co_kwonlyargcount",
      StrictCodeObject::codeKwOnlyArgCountGetter,
      nullptr,
      nullptr);
}

std::vector<std::type_index> StrictCodeObjectType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictCodeObjectType));
  return baseVec;
}

} // namespace strictmod::objects
