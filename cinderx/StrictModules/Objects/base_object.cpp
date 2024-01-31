// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/base_object.h"

#include "cinderx/StrictModules/Objects/module.h"
#include "cinderx/StrictModules/exceptions.h"

namespace strictmod::objects {
std::string formatArgs(
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames) {
  std::ostringstream os;
  auto posSize = args.size() - argNames.size();
  assert(posSize >= 0);
  auto it = args.begin();
  // positional arguments
  for (; it != args.begin() + posSize; ++it) {
    os << (*it)->getDisplayName() << ",";
  }
  // named keyword arguments
  auto nameIt = argNames.begin();
  for (; it != args.end(); ++it) {
    os << (*nameIt) << "=" << (*it)->getDisplayName() << ",";
    ++nameIt;
  }
  return os.str();
}

void checkExternalModification(
    std::shared_ptr<BaseStrictObject> modified,
    const CallerContext& caller) {
  auto ownerM = modified->getCreator();
  auto callerM = caller.caller;
  if ((!ownerM.expired() && !callerM.expired()) &&
      (ownerM.owner_before(callerM) || callerM.owner_before(ownerM))) {
    caller.error<ModifyImportValueException>(
        modified->getDisplayName(),
        ownerM.lock()->getModuleName(),
        callerM.lock()->getModuleName());
  }
}

bool BaseStrictObject::isHashable() const {
  return false;
}
size_t BaseStrictObject::hash() const {
  return 0;
}
bool BaseStrictObject::eq(const BaseStrictObject&) const {
  return false;
}

bool StrictObjectEqual::operator()(
    const std::shared_ptr<BaseStrictObject>& lhs,
    const std::shared_ptr<BaseStrictObject>& rhs) const {
  // same object
  if (lhs == rhs) {
    return true;
  }
  return lhs->eq(*rhs) || rhs->eq(*lhs);
}

size_t StrictObjectHasher::operator()(
    const std::shared_ptr<BaseStrictObject>& obj) const {
  if (obj->isHashable()) {
    return obj->hash();
  }
  return std::hash<std::shared_ptr<BaseStrictObject>>{}(obj);
}
} // namespace strictmod::objects
