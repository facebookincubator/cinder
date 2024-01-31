// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/callable.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context_impl.h"

namespace strictmod::objects {

// method descr
StrictMethodDescr::StrictMethodDescr(
    std::weak_ptr<StrictModuleObject> creator,
    InstCallType func,
    std::shared_ptr<StrictType> declType,
    std::string name)
    : StrictInstance(MethodDescrType(), std::move(creator)),
      func_(std::move(func)),
      declType_(std::move(declType)),
      funcName_(std::move(name)) {}

std::shared_ptr<BaseStrictObject> StrictMethodDescr::copy(
    const CallerContext&) {
  return shared_from_this();
}

std::shared_ptr<BaseStrictObject> StrictMethodDescrType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  if (inst == nullptr) {
    return obj;
  }
  auto descr = assertStaticCast<StrictMethodDescr>(obj);
  // caller.module, obj.func, inst, obj.name
  return std::make_shared<StrictBuiltinFunctionOrMethod>(
      caller.caller, descr->getFunc(), std::move(inst), descr->getFuncName());
}

std::shared_ptr<BaseStrictObject> StrictMethodDescrType::call(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller) {
  auto descr = assertStaticCast<StrictMethodDescr>(obj);
  // need at least a self parameter
  if (args.size() - argNames.size() <= 0) {
    caller.raiseTypeError(
        "descriptor {} needs an argument", descr->getFuncName());
  }
  auto declType = descr->getDeclaredType();
  auto& firstArgType = args[0]->getTypeRef();
  if (declType != nullptr && !firstArgType.isSubType(declType)) {
    caller.raiseTypeError(
        "descriptor {} requires a '{}' object but received '{}'",
        descr->getFuncName(),
        declType->getName(),
        firstArgType.getName());
  }
  return descr->getFunc()(
      args[0], std::vector(args.begin() + 1, args.end()), argNames, caller);
}

std::shared_ptr<StrictType> StrictMethodDescrType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictMethodDescrType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictMethodDescrType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictMethodDescrType));
  return baseVec;
}

bool StrictMethodDescrType::isCallable(const CallerContext&) {
  return true;
}

// builtin functions
StrictBuiltinFunctionOrMethod::StrictBuiltinFunctionOrMethod(
    std::weak_ptr<StrictModuleObject> creator,
    InstCallType func,
    std::shared_ptr<BaseStrictObject> inst,
    std::string name)
    : StrictInstance(BuiltinFunctionOrMethodType(), std::move(creator)),
      func_(std::move(func)),
      inst_(std::move(inst)),
      name_(name),
      displayName_("<builtin function '" + name + "'>") {}

std::string StrictBuiltinFunctionOrMethod::getDisplayName() const {
  return displayName_;
}

std::shared_ptr<BaseStrictObject> StrictBuiltinFunctionOrMethod::copy(
    const CallerContext&) {
  return shared_from_this();
}

std::shared_ptr<BaseStrictObject> StrictBuiltinFunctionOrMethodType::call(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& names,
    const CallerContext& caller) {
  assert(
      std::dynamic_pointer_cast<StrictBuiltinFunctionOrMethod>(obj) != nullptr);
  std::shared_ptr<StrictBuiltinFunctionOrMethod> m =
      std::static_pointer_cast<StrictBuiltinFunctionOrMethod>(obj);
  return m->getFunc()(m->getInst(), args, names, caller);
}

std::shared_ptr<StrictType> StrictBuiltinFunctionOrMethodType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictBuiltinFunctionOrMethodType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index>
StrictBuiltinFunctionOrMethodType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictBuiltinFunctionOrMethodType));
  return baseVec;
}

bool StrictBuiltinFunctionOrMethodType::isCallable(const CallerContext&) {
  return true;
}

// user methods
StrictMethod::StrictMethod(
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> func,
    std::shared_ptr<BaseStrictObject> inst)
    : StrictInstance(MethodType(), std::move(creator)),
      func_(std::move(func)),
      inst_(std::move(inst)) {}

std::shared_ptr<BaseStrictObject> StrictMethod::copy(const CallerContext&) {
  return shared_from_this();
}

std::shared_ptr<BaseStrictObject> StrictMethodType::loadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  auto method = assertStaticCast<StrictMethod>(obj);
  auto methodType = method->getType();
  auto descr = methodType->typeLookup(key, caller);
  // if attribute exists on method, invoke the descr
  // (if descr is not actually a descriptor, StrictObjectType::getDescr
  // does the right thing)
  if (descr != nullptr) {
    return iGetDescr(std::move(descr), method, methodType, caller);
  }
  // otherwise look for the attribute on the wrapper function object
  return iLoadAttr(method->getFunc(), key, std::move(defaultValue), caller);
}

std::shared_ptr<BaseStrictObject> StrictMethodType::call(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& names,
    const CallerContext& caller) {
  auto method = assertStaticCast<StrictMethod>(obj);
  std::vector<std::shared_ptr<BaseStrictObject>> instArgs;
  instArgs.reserve(args.size() + 1);
  instArgs.push_back(method->getInst());
  instArgs.insert(instArgs.end(), args.begin(), args.end());

  return iCall(method->getFunc(), instArgs, names, caller);
}

std::shared_ptr<StrictType> StrictMethodType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictMethodType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictMethodType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictMethodType));
  return baseVec;
}

bool StrictMethodType::isCallable(const CallerContext&) {
  return true;
}

template <typename T>
std::shared_ptr<BaseStrictObject> method__func__Getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  return (assertStaticCast<T>(std::move(inst))->getFunc());
}

void StrictMethodType::addMethods() {
  addGetSetDescriptor(
      "__func__", method__func__Getter<StrictMethod>, nullptr, nullptr);
}

// class (user) Method

StrictClassMethod::StrictClassMethod(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> func)
    : StrictInstance(std::move(type), std::move(creator)),
      func_(std::move(func)) {}

std::shared_ptr<BaseStrictObject> StrictClassMethod::copy(
    const CallerContext&) {
  return shared_from_this();
}

std::shared_ptr<BaseStrictObject> StrictClassMethod::classmethod__init__(
    std::shared_ptr<StrictClassMethod> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> func) {
  checkExternalModification(self, caller);
  self->func_ = std::move(func);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictClassMethod::classmethod__get__(
    std::shared_ptr<StrictClassMethod> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject> ctx) {
  return std::make_shared<StrictMethod>(
      caller.caller, self->getFunc(), std::move(ctx));
}

std::shared_ptr<BaseStrictObject> StrictClassMethodType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  std::shared_ptr<StrictClassMethod> method =
      assertStaticCast<StrictClassMethod>(obj);
  return StrictClassMethod::classmethod__get__(
      std::move(method), caller, std::move(inst), std::move(type));
}

std::unique_ptr<BaseStrictObject> StrictClassMethodType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictClassMethod>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      caller,
      nullptr);
}

std::shared_ptr<StrictType> StrictClassMethodType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictClassMethodType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictClassMethodType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictClassMethodType));
  return baseVec;
}

void StrictClassMethodType::addMethods() {
  addMethod(kDunderInit, StrictClassMethod::classmethod__init__);
  addMethod("__get__", StrictClassMethod::classmethod__get__);
  addGetSetDescriptor(
      "__func__", method__func__Getter<StrictClassMethod>, nullptr, nullptr);
}

// static (user) Method

StrictStaticMethod::StrictStaticMethod(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> func)
    : StrictInstance(std::move(type), std::move(creator)),
      func_(std::move(func)) {}

std::shared_ptr<BaseStrictObject> StrictStaticMethod::copy(
    const CallerContext&) {
  return shared_from_this();
}

std::shared_ptr<BaseStrictObject> StrictStaticMethod::staticmethod__init__(
    std::shared_ptr<StrictStaticMethod> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> func) {
  checkExternalModification(self, caller);
  self->func_ = std::move(func);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> StrictStaticMethod::staticmethod__get__(
    std::shared_ptr<StrictStaticMethod> self,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>) {
  return self->getFunc();
}

std::shared_ptr<BaseStrictObject> StrictStaticMethodType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller) {
  std::shared_ptr<StrictStaticMethod> method =
      assertStaticCast<StrictStaticMethod>(obj);
  return StrictStaticMethod::staticmethod__get__(
      std::move(method), caller, std::move(inst), std::move(type));
}

std::unique_ptr<BaseStrictObject> StrictStaticMethodType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictStaticMethod>(
      std::static_pointer_cast<StrictType>(shared_from_this()),
      caller,
      nullptr);
}

std::shared_ptr<StrictType> StrictStaticMethodType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictStaticMethodType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictStaticMethodType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictStaticMethodType));
  return baseVec;
}

void StrictStaticMethodType::addMethods() {
  addMethod(kDunderInit, StrictStaticMethod::staticmethod__init__);
  addMethod("__get__", StrictStaticMethod::staticmethod__get__);
  addGetSetDescriptor(
      "__func__", method__func__Getter<StrictStaticMethod>, nullptr, nullptr);
}
} // namespace strictmod::objects
