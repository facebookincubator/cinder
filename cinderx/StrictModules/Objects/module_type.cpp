// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/module_type.h"

#include "cinderx/StrictModules/Objects/objects.h"

namespace strictmod::objects {
void StrictModuleType::storeAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  auto mod = assertStaticCast<StrictModuleObject>(obj);
  caller.error<ImmutableException>(key, "module", mod->getModuleName());
}

std::shared_ptr<StrictType> StrictModuleType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictModuleType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictModuleType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictModuleType));
  return baseVec;
}

void StrictModuleType::addMethods() {
  addGetSetDescriptor(kDunderDict, getDunderDictAllowed, nullptr, nullptr);
}
} // namespace strictmod::objects
