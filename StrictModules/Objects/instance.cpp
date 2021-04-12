#include "StrictModules/Objects/instance.h"

#include "StrictModules/Objects/type.h"

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
      dict_(dict == nullptr ? std::make_shared<DictType>() : std::move(dict)) {}

PyObject* StrictInstance::getPyObject() const {
  return nullptr;
}

std::string StrictInstance::getDisplayName() const {
  return "<" + getTypeRef().getName() + " instance>";
}

std::unique_ptr<BaseStrictObject> StrictInstance::copy() const {
  throw std::runtime_error("Copying StrictInstance unsupported");
}

std::shared_ptr<BaseStrictObject> StrictInstance::getAttr(
    const std::string& name) {
  auto it = dict_->find(name);
  if (it != dict_->end()) {
    return std::shared_ptr(it->second);
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

} // namespace strictmod::objects
