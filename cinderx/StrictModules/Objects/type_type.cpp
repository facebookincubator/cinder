// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/type_type.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"

namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> StrictTypeType::loadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr && descr->getTypeRef().isDataDescr(caller)) {
    // data descr found on metatype
    return iGetDescr(
        std::move(descr), std::move(obj), std::move(objType), caller);
  }
  // lookup in dict of obj and subtypes
  std::shared_ptr<StrictType> typ = std::dynamic_pointer_cast<StrictType>(obj);
  assert(typ != nullptr);
  auto dictDescr = typ->typeLookup(key, caller);
  if (dictDescr) {
    return iGetDescr(std::move(dictDescr), nullptr, std::move(typ), caller);
  }
  // lastly, invoke non-data descriptor, if any
  if (descr) {
    return iGetDescr(
        std::move(descr), std::move(obj), std::move(objType), caller);
  }
  return defaultValue;
}

void StrictTypeType::storeAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr && descr->getTypeRef().isDataDescr(caller)) {
    // data descr found on metatype
    iSetDescr(std::move(descr), std::move(obj), std::move(value), caller);
    return;
  }

  std::shared_ptr<StrictType> typ = assertStaticCast<StrictType>(obj);
  if (typ->isImmutable()) {
    caller.error<ImmutableException>(key, "type", typ->getName());
    return;
  }
  checkExternalModification(obj, caller);
  typ->setAttr(std::move(key), std::move(value));
}

void StrictTypeType::delAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr && descr->getTypeRef().isDataDescr(caller)) {
    iDelDescr(std::move(descr), std::move(obj), caller);
    return;
  }

  std::shared_ptr<StrictType> typ = assertStaticCast<StrictType>(obj);
  if (typ->isImmutable()) {
    caller.error<ImmutableException>(key, "type", typ->getName());
    return;
  }
  checkExternalModification(obj, caller);
  typ->setAttr(std::move(key), nullptr);
}

std::shared_ptr<StrictType> StrictTypeType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictTypeType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

void StrictTypeType::addMethods() {
  addMethodDescr("__call__", StrictType::type__call__);
  addBuiltinFunctionOrMethod("__new__", StrictType::type__new__);
  addMethod("mro", StrictType::typeMro);
  addMethod("__subclasscheck__", StrictType::type__subclasscheck__);
  addMethod("__or__", StrictType::type__or__);
  addMethod("__ror__", StrictType::type__ror__);
  addGetSetDescriptor(kDunderDict, getDunderDictAllowed, nullptr, nullptr);
  addGetSetDescriptor(
      "__bases__", StrictType::type__bases__Getter, nullptr, nullptr);
  addGetSetDescriptor(
      "__module__",
      StrictType::type__module__Getter,
      StrictType::type__module__Setter,
      nullptr);
  addGetSetDescriptor(
      "__mro__", StrictType::type__mro__Getter, nullptr, nullptr);
  addGetSetDescriptor(
      "__qualname__", StrictType::type__qualname__Getter, nullptr, nullptr);
  addStringMemberDescriptor<StrictType, &StrictType::name_>("__name__");
}

std::vector<std::type_index> StrictTypeType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictTypeType));
  return baseVec;
}
} // namespace strictmod::objects
