
// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/super.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"

#include <fmt/format.h>

namespace strictmod::objects {
// super
// Instance of super(T, obj)
StrictSuper::StrictSuper(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictType> currentClass,
    std::shared_ptr<BaseStrictObject> self,
    std::shared_ptr<StrictType> selfClass,
    bool ignoreUnknowns)
    : StrictInstance(std::move(type), std::move(creator)),
      currentClass_(std::move(currentClass)),
      self_(std::move(self)),
      selfClass_(selfClass),
      ignoreUnknowns_(ignoreUnknowns) {}

std::string StrictSuper::getDisplayName() const {
  if (self_ != nullptr) {
    return fmt::format("super({}, {})", currentClass_->getName(), self_);
  } else {
    return fmt::format("super({})", currentClass_->getName());
  }
}

// wrapped methods
std::shared_ptr<StrictType> superCheckHelper(
    const std::shared_ptr<StrictType>& currentType,
    const std::shared_ptr<BaseStrictObject>& obj,
    const CallerContext& caller) {
  auto objType = std::dynamic_pointer_cast<StrictType>(obj);
  if (objType != nullptr && objType->isSubType(currentType)) {
    return objType;
  } else if (obj->getTypeRef().isSubType(currentType)) {
    return obj->getType();
  } else {
    auto objClass = iLoadAttrOnType(obj, kDunderClass, nullptr, caller);
    if (objClass != nullptr) {
      objType = std::dynamic_pointer_cast<StrictType>(objClass);
      if (objType != nullptr && objType->isSubType(currentType)) {
        return objType;
      }
    }
  }
  caller.raiseTypeError(
      "super(type, obj): obj must be an instance or subtype of type");
}

/* handles super(T, obj) and super(T)
 * handling of super() is done in the interpreter due to need of
 * accessing __class__ and self
 */
std::shared_ptr<BaseStrictObject> StrictSuper::super__init__(
    std::shared_ptr<StrictSuper> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> currentClass, // T
    std::shared_ptr<BaseStrictObject> obj) {
  checkExternalModification(self, caller);
  auto currentType = std::dynamic_pointer_cast<StrictType>(currentClass);
  if (currentType == nullptr) {
    caller.raiseTypeError("super() arg 1 must be type, not {}", currentClass);
  }
  self->currentClass_ = currentType;
  if (obj != nullptr) {
    std::shared_ptr<StrictType> objType =
        superCheckHelper(currentType, obj, caller);
    self->self_ = std::move(obj);
    self->selfClass_ = std::move(objType);
  } else {
    self->self_ = nullptr;
    self->selfClass_ = nullptr;
  }
  return NoneObject();
}

//   std::shared_ptr<StrictType> currentClass_; // T
//   std::shared_ptr<BaseStrictObject> self_; // obj
//   std::shared_ptr<BaseStrictObject> selfClass; // type(obj)

std::shared_ptr<BaseStrictObject> StrictSuperType::loadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictSuper>(std::move(obj));
  // self_ is not bind, or loading __class__ of super (which is just 'super')
  if (self->getObjClass() == nullptr || key == kDunderClass) {
    return StrictObjectType::loadAttr(
        std::move(self), key, std::move(defaultValue), caller);
  }
  // find position in selfClass_.mro that's after currentClass_
  const auto& mro = self->getObjClass()->mro();
  std::size_t idx = 0;
  for (; idx < mro.size(); ++idx) {
    if (mro[idx] == self->getCurrentClass()) {
      break;
    }
  }
  // skip over currentClass_
  ++idx;
  for (; idx < mro.size(); ++idx) {
    auto clsObj = std::const_pointer_cast<BaseStrictObject>(mro[idx]);
    auto cls = std::dynamic_pointer_cast<StrictType>(clsObj);
    if (cls == nullptr) {
      if (!self->ignoreUnknowns()) {
        iLoadAttr(clsObj, key, defaultValue, caller);
      }
      continue;
    }
    auto descr = cls->getAttr(key);
    if (descr != nullptr) {
      std::shared_ptr<BaseStrictObject> instance;
      if (self->getObj() != self->getObjClass()) {
        instance = self->getObj();
      }
      return iGetDescr(
          std::move(descr), std::move(instance), self->getObjClass(), caller);
    }
  }
  return StrictObjectType::loadAttr(
      std::move(self), key, std::move(defaultValue), caller);
}

void StrictSuperType::storeAttr(
    std::shared_ptr<BaseStrictObject>,
    const std::string& key,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  caller.raiseExceptionStr(
      AttributeErrorType(), "super object has no attribute {}", key);
}

void StrictSuperType::delAttr(
    std::shared_ptr<BaseStrictObject>,
    const std::string& key,
    const CallerContext& caller) {
  caller.raiseExceptionStr(
      AttributeErrorType(), "super object has no attribute {}", key);
}

std::shared_ptr<BaseStrictObject> StrictSuperType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictSuper>(std::move(obj));
  if (inst == nullptr || self->getObj() != nullptr) {
    return self;
  }
  std::shared_ptr<StrictType> instType =
      superCheckHelper(self->getCurrentClass(), inst, caller);
  return std::make_shared<StrictSuper>(
      SuperType(), caller.caller, self->getCurrentClass(), inst, instType);
}

std::vector<std::type_index> StrictSuperType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictSuperType));
  return baseVec;
}

std::shared_ptr<StrictType> StrictSuperType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictSuperType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictSuperType::addMethods() {
  addMethodDefault(kDunderInit, StrictSuper::super__init__, nullptr);
}

bool StrictSuperType::isBaseType() const {
  return false;
}

std::unique_ptr<BaseStrictObject> StrictSuperType::constructInstance(
    std::weak_ptr<StrictModuleObject> caller) {
  return std::make_unique<StrictSuper>(
      SuperType(), caller, ObjectType(), nullptr, nullptr);
}
} // namespace strictmod::objects
