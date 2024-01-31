// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/union.h"

#include "cinderx/StrictModules/Objects/builtins.h"
#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"

#include <unordered_set>
namespace strictmod::objects {

static std::vector<std::shared_ptr<BaseStrictObject>> dedupAndFlattenArgsHelper(
    const std::vector<std::shared_ptr<BaseStrictObject>> args) {
  std::unordered_set<BaseStrictObject*> seen;
  std::vector<std::shared_ptr<BaseStrictObject>> result;
  result.reserve(args.size());

  auto addArgIfNew = [&seen,
                      &result](const std::shared_ptr<BaseStrictObject>& o) {
    if (seen.find(o.get()) == seen.end()) {
      seen.insert(o.get());
      result.push_back(o);
    }
  };

  for (auto& a : args) {
    auto unionObj = std::dynamic_pointer_cast<StrictUnion>(a);
    if (unionObj) {
      const auto& unionObjArgs = unionObj->getArgs();
      std::for_each(unionObjArgs.begin(), unionObjArgs.end(), addArgIfNew);
    } else {
      addArgIfNew(a);
    }
  }
  return result;
}

StrictUnion::StrictUnion(
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> args)
    : StrictInstance(UnionType(), std::move(creator)), args_(args) {}

std::string StrictUnion::getDisplayName() const {
  return fmt::format("{}", fmt::join(args_, "|"));
}

std::shared_ptr<BaseStrictObject> StrictUnion::copy(const CallerContext&) {
  return shared_from_this();
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictUnion::union__instancecheck__(
    std::shared_ptr<StrictUnion> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  for (auto arg : self->getArgs()) {
    if (arg == NoneObject()) {
      arg = NoneType();
    }
    auto argType = std::dynamic_pointer_cast<StrictType>(arg);
    if (argType) {
      auto isinstanceResult =
          isinstanceImpl(nullptr, caller, inst, std::move(argType));
      if (isinstanceResult == StrictTrue()) {
        return isinstanceResult;
      }
    }
  }
  return StrictFalse();
}

std::shared_ptr<BaseStrictObject> StrictUnion::union__subclasscheck__(
    std::shared_ptr<StrictUnion> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  for (const auto& arg : self->getArgs()) {
    auto argType = std::dynamic_pointer_cast<StrictType>(arg);
    if (argType) {
      auto issubclassResult =
          issubclassImpl(nullptr, caller, inst, std::move(argType));
      if (issubclassResult == StrictTrue()) {
        return issubclassResult;
      }
    }
  }
  return StrictFalse();
}

bool isTypingType(
    std::shared_ptr<BaseStrictObject> arg,
    const std::string& name) {
  auto argType = arg->getType();
  if (argType->getCreator().lock()->getModuleName() == "typing") {
    return argType->getName() == name;
  }
  return false;
}

static bool isUnionableHelper(const std::shared_ptr<BaseStrictObject>& arg) {
  if (arg == NoneObject()) {
    return true;
  }
  if (std::dynamic_pointer_cast<StrictType>(arg) != nullptr) {
    return true;
  }
  auto argType = arg->getType();
  if (argType == UnionType()) {
    return true;
  }
  // TODO: this is a hacky way to recognize typing library constructs
  // and identify them as unionable
  if (argType->getCreator().lock()->getModuleName() == "typing") {
    return argType->getName() == "TypeVar" ||
        argType->getName() == "_SpecialForm";
  }
  return false;
}

std::shared_ptr<BaseStrictObject> unionOrHelper(
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> left,
    std::shared_ptr<BaseStrictObject> right) {
  if (isUnionableHelper(left) && isUnionableHelper(right)) {
    auto args = dedupAndFlattenArgsHelper({std::move(left), std::move(right)});

    if (args.size() == 1) {
      return args[0];
    }
    return std::make_shared<StrictUnion>(caller.caller, std::move(args));
  }
  return NotImplemented();
}

std::shared_ptr<BaseStrictObject> StrictUnion::union__or__(
    std::shared_ptr<StrictUnion> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  return unionOrHelper(caller, std::move(self), std::move(inst));
}

std::shared_ptr<BaseStrictObject> StrictUnion::union__ror__(
    std::shared_ptr<StrictUnion> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  return unionOrHelper(caller, std::move(inst), std::move(self));
}

std::shared_ptr<BaseStrictObject> StrictUnion::union__args__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictUnion>(std::move(inst));
  if (self->argsObj_ == nullptr) {
    self->argsObj_ =
        std::make_shared<StrictTuple>(TupleType(), self->creator_, self->args_);
  }
  return self->argsObj_;
}

std::unique_ptr<BaseStrictObject> StrictUnionType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictUnion>(
      std::move(caller), std::vector<std::shared_ptr<BaseStrictObject>>());
}

std::shared_ptr<StrictType> StrictUnionType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictUnionType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictUnionType::addMethods() {
  addMethod("__instancecheck__", StrictUnion::union__instancecheck__);
  addMethod("__subclasscheck__", StrictUnion::union__subclasscheck__);
  addMethod("__or__", StrictUnion::union__or__);
  addMethod("__ror__", StrictUnion::union__ror__);
  addGetSetDescriptor(
      "__args__", StrictUnion::union__args__getter, nullptr, nullptr);
}

std::vector<std::type_index> StrictUnionType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictUnionType));
  return baseVec;
}
} // namespace strictmod::objects
