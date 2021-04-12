#include "StrictModules/Objects/module_type.h"

#include "StrictModules/Objects/objects.h"

namespace strictmod::objects {
void StrictModuleType::storeAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  auto mod = assertStaticCast<StrictModuleObject>(obj);
  caller.error<ImmutableException>(key, "module", mod->getModuleName());
}
} // namespace strictmod::objects
