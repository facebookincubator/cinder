// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/callable.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context_impl.h"

namespace strictmod::objects {

// method descr
StrictMethodDescr::StrictMethodDescr(
    std::weak_ptr<StrictModuleObject> creator,
    InstCallType func,
    std::shared_ptr<StrictType> declType,
    std::string name)
    : StrictInstance(MethodDescrType(), std::move(creator)),
      func_(func),
      declType_(std::move(declType)),
      funcName_(std::move(name)) {}

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
  if (declType != nullptr && !firstArgType.is_subtype(declType)) {
    caller.raiseTypeError(
        "descriptor {} requires a '{}' object but received '{}'",
        descr->getFuncName(),
        declType->getName(),
        firstArgType.getName());
  }
  return descr->getFunc()(
      args[0], std::vector(args.begin() + 1, args.end()), argNames, caller);
}

// builtin functions
StrictBuiltinFunctionOrMethod::StrictBuiltinFunctionOrMethod(
    std::weak_ptr<StrictModuleObject> creator,
    InstCallType func,
    std::shared_ptr<BaseStrictObject> inst,
    std::string name)
    : StrictInstance(BuiltinFunctionOrMethodType(), std::move(creator)),
      func_(func),
      inst_(std::move(inst)),
      name_(name),
      displayName_("<builtin function '" + name + "'>") {}

std::string StrictBuiltinFunctionOrMethod::getDisplayName() const {
  return displayName_;
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

// user methods
StrictMethod::StrictMethod(
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> func,
    std::shared_ptr<BaseStrictObject> inst)
    : StrictInstance(MethodType(), std::move(creator)),
      func_(std::move(func)),
      inst_(std::move(inst)) {}

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
} // namespace strictmod::objects
