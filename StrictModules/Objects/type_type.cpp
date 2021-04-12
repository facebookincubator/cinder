#include "StrictModules/Objects/type_type.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> StrictTypeType::loadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller) {
  auto objType = obj->getType();
  auto descr = objType->typeLookup(key, caller);
  if (descr && descr->getTypeRef().isDataDescr()) {
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
  if (descr && descr->getTypeRef().isDataDescr()) {
    // data descr found on metatype
    iSetDescr(std::move(descr), std::move(obj), std::move(value), caller);
  }

  std::shared_ptr<StrictType> typ = std::dynamic_pointer_cast<StrictType>(obj);
  assert(typ != nullptr);
  if (typ->isImmutable()) {
    caller.error<ImmutableException>(key, "type", typ->getName());
    return;
  }
  checkExternalModification(obj, caller);
  typ->setAttr(key, std::move(value));
}
} // namespace strictmod::objects
