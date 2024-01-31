// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/StrictModules/Objects/genericalias_object.h"

#include "cinderx/StrictModules/Objects/builtins.h"
#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/Objects/typing_helpers.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"

#include <unordered_set>
namespace strictmod::objects {

static bool isTuple(const std::shared_ptr<BaseStrictObject>& obj) {
  return obj->getTypeRef().isSubType(TupleType());
}

static std::vector<std::shared_ptr<BaseStrictObject>> unpackArgsHelper(
    std::shared_ptr<BaseStrictObject> obj) {
  if (!isTuple(obj)) {
    return {obj};
  }
  std::shared_ptr<StrictTuple> t = assertStaticCast<StrictTuple>(obj);
  return std::vector<std::shared_ptr<BaseStrictObject>>(t->getData());
}

StrictGenericAlias::StrictGenericAlias(
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> origin,
    std::shared_ptr<BaseStrictObject> args)
    : StrictGenericAlias(
          std::move(creator),
          std::move(origin),
          unpackArgsHelper(std::move(args))) {}

StrictGenericAlias::StrictGenericAlias(
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> origin,
    std::vector<std::shared_ptr<BaseStrictObject>> args)
    : StrictInstance(GenericAliasType(), std::move(creator)),
      args_(std::move(args)),
      parameters_(),
      origin_(std::move(origin)),
      argsObj_(),
      parametersObj_() {}

std::string StrictGenericAlias::getDisplayName() const {
  return "GenericAlias[]";
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::copy(
    const CallerContext&) {
  return shared_from_this();
}

void StrictGenericAlias::makeParametersHelper(const CallerContext& caller) {
  std::vector<std::shared_ptr<BaseStrictObject>> params;
  params.reserve(args_.size());
  std::string typeVarName = "TypeVar";
  std::string paramsName = "__parameters__";
  for (auto& arg : args_) {
    if (isTypingType(arg, typeVarName)) {
      params.emplace_back(arg);
    } else {
      auto subParamObj = iLoadAttr(arg, paramsName, nullptr, caller);
      if (subParamObj != nullptr &&
          subParamObj->getTypeRef().isSubType(TupleType())) {
        std::vector<std::shared_ptr<BaseStrictObject>> subParams =
            iGetElementsVec(std::move(subParamObj), caller);
        for (auto& subP : subParams) {
          params.emplace_back(subP);
        }
      }
    }
  }
  parameters_ = std::move(params);
}

std::vector<std::shared_ptr<BaseStrictObject>>
StrictGenericAlias::subParametersHelper(
    const CallerContext& caller,
    const std::shared_ptr<BaseStrictObject>& item) {
  std::vector<std::shared_ptr<BaseStrictObject>> itemArgs;
  if (isTuple(item)) {
    itemArgs = iGetElementsVec(item, caller);
  } else {
    itemArgs.emplace_back(item);
  }
  auto& parameters = parameters_.value();
  int nItems = itemArgs.size();
  int nParams = parameters.size();

  if (nItems != nParams) {
    caller.raiseExceptionStr(
        TypeErrorType(), "expected {} arguments but got {}", nParams, nItems);
  }
  auto vec_idx = [](auto& vec, auto& item) -> int {
    auto it = std::find(vec.begin(), vec.end(), item);
    if (it == vec.end()) {
      return -1;
    }
    return std::distance(vec.begin(), it);
  };

  auto substitute = [&](auto& ga) {
    auto subParamObj = iLoadAttr(ga, "__parameters__", nullptr, caller);
    if (subParamObj == nullptr) {
      return ga;
    }
    if (isTuple(subParamObj)) {
      std::vector<std::shared_ptr<BaseStrictObject>> newSubArgs;
      std::vector<std::shared_ptr<BaseStrictObject>> subParams =
          iGetElementsVec(std::move(subParamObj), caller);
      for (auto& subP : subParams) {
        int paramIdx = vec_idx(parameters, subP);
        if (paramIdx >= 0) {
          newSubArgs.emplace_back(itemArgs[paramIdx]);
        }
      }
      auto newSubArgsTuple = std::make_shared<StrictTuple>(
          TupleType(), caller.caller, std::move(newSubArgs));
      ga = iGetElement(ga, newSubArgsTuple, caller);
    }
    return ga;
  };

  std::vector<std::shared_ptr<BaseStrictObject>> newArgs;
  for (auto& arg : args_) {
    if (isTypingType(arg, "TypeVar")) {
      arg = itemArgs[vec_idx(parameters, arg)];
    } else {
      // if arg is generic alias, do substitution in arg
      arg = substitute(arg);
    }
    newArgs.emplace_back(arg);
  }
  return newArgs;
}

// wrapped methods

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__getitem__(
    std::shared_ptr<StrictGenericAlias> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> idx) {
  if (!self->parameters_.has_value()) {
    self->makeParametersHelper(caller);
  }
  std::vector<std::shared_ptr<BaseStrictObject>> newArgs =
      self->subParametersHelper(caller, idx);
  return std::make_shared<StrictGenericAlias>(
      caller.caller, self->origin_, std::move(newArgs));
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__mro_entries__(
    std::shared_ptr<StrictGenericAlias> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject>) {
  return std::make_shared<StrictTuple>(
      TupleType(),
      caller.caller,
      std::vector<std::shared_ptr<BaseStrictObject>>{self->origin_});
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__instancecheck__(
    std::shared_ptr<StrictGenericAlias>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject>) {
  caller.raiseExceptionStr(
      TypeErrorType(),
      "isinstance() argument 2 cannot be a parameterized generic");
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__subclasscheck__(
    std::shared_ptr<StrictGenericAlias>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject>) {
  caller.raiseExceptionStr(
      TypeErrorType(),
      "issubclass() argument 2 cannot be a parameterized generic");
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__args__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictGenericAlias>(std::move(inst));
  if (self->argsObj_ == nullptr) {
    self->argsObj_ =
        std::make_shared<StrictTuple>(TupleType(), self->creator_, self->args_);
  }
  return self->argsObj_;
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__parameters__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictGenericAlias>(std::move(inst));
  if (self->parametersObj_ == nullptr) {
    if (!self->parameters_.has_value()) {
      self->makeParametersHelper(caller);
    }
    self->parametersObj_ = std::make_shared<StrictTuple>(
        TupleType(), self->creator_, self->parameters_.value());
  }
  return self->parametersObj_;
}

std::shared_ptr<BaseStrictObject> StrictGenericAlias::ga__origin__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictGenericAlias>(std::move(inst));
  return self->origin_;
}

std::unique_ptr<BaseStrictObject> StrictGenericAliasType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  auto emptyArg = std::make_shared<StrictTuple>(
      TupleType(), caller, std::vector<std::shared_ptr<BaseStrictObject>>{});
  return std::make_unique<StrictGenericAlias>(
      std::move(caller), TypeType(), std::move(emptyArg));
}

std::shared_ptr<StrictType> StrictGenericAliasType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictGenericAliasType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictGenericAliasType::addMethods() {
  addMethod(kDunderGetItem, StrictGenericAlias::ga__getitem__);
  addMethod("__mro_entries__", StrictGenericAlias::ga__mro_entries__);
  addMethod("__instancecheck__", StrictGenericAlias::ga__instancecheck__);
  addMethod("__subclasscheck__", StrictGenericAlias::ga__subclasscheck__);

  addGetSetDescriptor(
      "__args__", StrictGenericAlias::ga__args__getter, nullptr, nullptr);

  addGetSetDescriptor(
      "__parameters__",
      StrictGenericAlias::ga__parameters__getter,
      nullptr,
      nullptr);
  addGetSetDescriptor(
      "__origin__", StrictGenericAlias::ga__origin__getter, nullptr, nullptr);
}

std::vector<std::type_index> StrictGenericAliasType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictGenericAliasType));
  return baseVec;
}

std::shared_ptr<BaseStrictObject> createGenericAlias(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> args) {
  // __class_getitem__ is implicitly a classmethod
  return std::make_shared<StrictGenericAlias>(caller.caller, obj, args);
}

} // namespace strictmod::objects
