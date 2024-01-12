#include "cinderx/StrictModules/Objects/lazy_object.h"

#include "cinderx/StrictModules/Compiler/abstract_module_loader.h"

#include <iostream>
namespace strictmod::objects {

StrictLazyObject::StrictLazyObject(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    ModuleLoader* loader,
    std::string modName,
    std::string unknownName,
    CallerContext context,
    std::optional<std::string> attrName)
    : BaseStrictObject(std::move(type), std::move(creator)),
      loader_(loader),
      modName_(std::move(modName)),
      unknownName_(std::move(unknownName)),
      context_(context),
      attrName_(std::move(attrName)),
      evaluated_(false) {}

void StrictLazyObject::forceEvaluate(const CallerContext& caller) {
  // Handle import cycles. If forceEvaluate ended up calling itself, then in
  // eager import the name cannot be resolved at this point, and obj_ should be
  // unset
  if (evaluated_) {
    return;
  }
  evaluated_ = true;
  std::shared_ptr<BaseStrictObject> result;
  auto mod = loader_->loadModuleValue(modName_);
  if (mod && attrName_) {
    result = iImportFrom(mod, *attrName_, context_, loader_);
  } else {
    result = mod;
  }
  if (!result) {
    result = makeUnknown(caller, "{}", unknownName_);
  }
  obj_ = result;
}

std::shared_ptr<BaseStrictObject> StrictLazyObject::copy(const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}
std::string StrictLazyObject::getDisplayName() const {
  return "lazy object";
}

// StrictLazyObjectType
std::shared_ptr<BaseStrictObject> StrictLazyObjectType::getDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::setDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::delDescr(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::loadAttr(
    std::shared_ptr<BaseStrictObject>,
    const std::string&,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

void StrictLazyObjectType::storeAttr(
    std::shared_ptr<BaseStrictObject>,
    const std::string&,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

void StrictLazyObjectType::delAttr(
    std::shared_ptr<BaseStrictObject>,
    const std::string&,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::binOp(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    operator_ty,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::reverseBinOp(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    operator_ty,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::unaryOp(
    std::shared_ptr<BaseStrictObject>,
    unaryop_ty,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::binCmpOp(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    cmpop_ty,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<StrictIteratorBase> StrictLazyObjectType::getElementsIter(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::vector<std::shared_ptr<BaseStrictObject>>
StrictLazyObjectType::getElementsVec(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::getElement(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

void StrictLazyObjectType::setElement(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

void StrictLazyObjectType::delElement(
    std::shared_ptr<BaseStrictObject>,
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::call(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<BaseStrictObject> StrictLazyObjectType::getTruthValue(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&) {
  throw std::runtime_error("shouldn't happen");
}

std::unique_ptr<BaseStrictObject> StrictLazyObjectType::constructInstance(
    std::weak_ptr<StrictModuleObject>) {
  throw std::runtime_error("shouldn't happen");
}

std::shared_ptr<StrictType> StrictLazyObjectType::recreate(
    std::string,
    std::weak_ptr<StrictModuleObject>,
    std::vector<std::shared_ptr<BaseStrictObject>>,
    std::shared_ptr<DictType>,
    std::shared_ptr<StrictType>,
    bool) {
  throw std::runtime_error("shouldn't happen");
}

std::vector<std::type_index> StrictLazyObjectType::getBaseTypeinfos() const {
  throw std::runtime_error("shouldn't happen");
}
} // namespace strictmod::objects
