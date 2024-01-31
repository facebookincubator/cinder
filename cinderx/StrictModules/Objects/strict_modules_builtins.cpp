// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/strict_modules_builtins.h"

#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/rewriter_attributes.h"
namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> looseSlots(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value) {
  auto typ = std::dynamic_pointer_cast<StrictType>(value);
  if (typ) {
    checkExternalModification(typ, caller);
    auto& typAttrs = typ->ensureRewriterAttrs();
    typAttrs.setSlotsEnabled(true);

    bool baseHasLooseSlots = false;
    for (auto& base : typ->mro()) {
      if (base->hasRewritterAttrs() &&
          base->getRewriterAttrs().isLooseSlots()) {
        baseHasLooseSlots = true;
      }
    }
    if (!baseHasLooseSlots) {
      typAttrs.setLooseSlots(true);
    }
  }

  return value;
}

std::shared_ptr<BaseStrictObject> strictSlots(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value) {
  checkExternalModification(value, caller);
  value->ensureRewriterAttrs().setSlotsEnabled(true);
  return value;
}

std::shared_ptr<BaseStrictObject> extraSlot(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value,
    std::shared_ptr<BaseStrictObject> name) {
  auto nameStr = std::dynamic_pointer_cast<StrictString>(name);
  if (nameStr) {
    checkExternalModification(value, caller);
    auto& attrs = value->ensureRewriterAttrs();
    attrs.addExtraSlots(nameStr->getValue());
  }
  return value;
}

std::shared_ptr<BaseStrictObject> setMutable(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value) {
  checkExternalModification(value, caller);
  value->ensureRewriterAttrs().setMutable(true);
  return value;
}

std::shared_ptr<BaseStrictObject> markCachedProperty(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value,
    std::shared_ptr<BaseStrictObject> isAsync,
    std::shared_ptr<BaseStrictObject> originalDec) {
  checkExternalModification(value, caller);
  auto isAsyncTruthValue =
      std::dynamic_pointer_cast<StrictBool>(iGetTruthValue(isAsync, caller));
  bool isAsyncBool = isAsyncTruthValue && isAsyncTruthValue->getValue();
  value->ensureRewriterAttrs().setHasCachedProp(true);
  originalDec->ensureRewriterAttrs().setCachedPropKind(
      isAsyncBool ? CachedPropertyKind::kCachedAsync
                  : CachedPropertyKind::kCached);
  return value;
}
} // namespace strictmod::objects
