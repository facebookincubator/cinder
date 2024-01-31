// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/exception_object.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"

#include <sstream>
namespace strictmod::objects {
std::string StrictExceptionObject::getDisplayName() const {
  if (displayName_.empty()) {
    displayName_ = type_->getDisplayName();
  }
  return displayName_;
}

std::shared_ptr<BaseStrictObject> StrictExceptionObject::exception__new__(
    std::shared_ptr<StrictExceptionObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>>) {
  std::shared_ptr<StrictType> type;
  if (!args.empty()) {
    type = std::dynamic_pointer_cast<StrictType>(args[0]);
  }

  if (type == nullptr) {
    type = ExceptionType();
  }

  std::vector<std::shared_ptr<BaseStrictObject>> excArgs;
  if (!args.empty()) {
    excArgs.reserve(args.size() - 1);
    excArgs.insert(
        excArgs.end(),
        std::move_iterator(args.begin() + 1),
        std::move_iterator(args.end()));
  }

  auto excDict = std::make_shared<DictType>();
  (*excDict)["args"] = std::make_shared<StrictTuple>(
      TupleType(), caller.caller, std::move(excArgs));

  return std::make_shared<StrictExceptionObject>(
      std::move(type), caller.caller, std::move(excDict));
}

// StrictExceptionType
void StrictExceptionType::addMethods() {
  addStaticMethodKwargs("__new__", StrictExceptionObject::exception__new__);
}

std::shared_ptr<StrictType> StrictExceptionType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictExceptionType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictExceptionType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictExceptionType));
  return baseVec;
}
} // namespace strictmod::objects
