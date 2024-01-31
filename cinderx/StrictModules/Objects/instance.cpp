// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/instance.h"

#include "cinderx/StrictModules/Objects/type.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/caller_context_impl.h"

#include <stdexcept>

namespace strictmod::objects {
StrictInstance::StrictInstance(
    std::shared_ptr<StrictType> type,
    std::shared_ptr<StrictModuleObject> creator,
    std::shared_ptr<DictType> dict)
    : StrictInstance(std::move(type), std::weak_ptr(creator), std::move(dict)) {
}

StrictInstance::StrictInstance(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<DictType> dict)
    : BaseStrictObject(std::move(type), std::move(creator)),
      dict_(dict == nullptr ? std::make_shared<DictType>() : std::move(dict)),
      dictObj_(),
      cleaned_(false),
      doc_() {}

Ref<> StrictInstance::getPyObject() const {
  return nullptr;
}

std::string StrictInstance::getDisplayName() const {
  return "<" + getTypeRef().getName() + " instance>";
}

std::shared_ptr<BaseStrictObject> StrictInstance::copy(
    const CallerContext& caller) {
  caller.error<UnsupportedException>("copy", type_->getName());
  return makeUnknown(caller, "copy({})", getDisplayName());
}

std::shared_ptr<BaseStrictObject> StrictInstance::getAttr(
    const std::string& name) {
  auto it = dict_->find(name);
  if (it != dict_->map_end()) {
    auto result = it->second.first;
    if (result->isLazy()) {
      auto lazy = std::static_pointer_cast<StrictLazyObject>(result);
      result = lazy->evaluate();
      (*dict_)[name] = result;
    }
    return result;
  }
  return std::shared_ptr<BaseStrictObject>();
}

void StrictInstance::setAttr(
    std::string name,
    std::shared_ptr<BaseStrictObject> value) {
  if (value) {
    (*dict_)[std::move(name)] = value;
  } else {
    dict_->erase(name);
  }
}

void StrictInstance::cleanContent(const StrictModuleObject* owner) {
  if (cleaned_ || (!creator_.expired() && owner != creator_.lock().get())) {
    return;
  }
  cleaned_ = true;
  if (dict_ != nullptr) {
    for (auto& item : *dict_) {
      auto& childData = item.second.first;
      if (childData) {
        childData->cleanContent(owner);
      }
    }
    dict_->clear();
  }
  if (dictObj_ != nullptr) {
    dictObj_->cleanContent(owner);
  }
}

} // namespace strictmod::objects
