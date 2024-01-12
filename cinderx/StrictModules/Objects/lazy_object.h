#pragma once

#include "cinderx/StrictModules/Objects/instance.h"
#include "cinderx/StrictModules/Objects/iterable_objects.h"
#include "cinderx/StrictModules/Objects/object_type.h"

#include <optional>

namespace strictmod::compiler {
class ModuleLoader;
}
namespace strictmod::objects {
/**
 * Module level lazy object. The object contains
 * a lazily evaluated module, and an optional name.
 *
 * If there is no name, the lazy object represents the module
 * itself, lazily evaluated
 *
 * Otherwise, the lazy object represents the member of the module.
 * The module (but not the member in the module) is lazily evaluated
 */
class StrictLazyObject : public BaseStrictObject {
 public:
  StrictLazyObject(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      ModuleLoader* loader,
      std::string modName,
      std::string unknownName,
      CallerContext context,
      std::optional<std::string> attrName = std::nullopt);

  std::shared_ptr<BaseStrictObject> evaluate() {
    if (obj_ == nullptr) {
      forceEvaluate(context_);
    }
    return obj_;
  }

  virtual bool isLazy() const override {
    return true;
  }

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;
  virtual std::string getDisplayName() const override;

 private:
  ModuleLoader* loader_;
  std::string modName_;
  std::string unknownName_;
  CallerContext context_;
  std::optional<std::string> attrName_;
  std::shared_ptr<BaseStrictObject> obj_;
  bool evaluated_;

  void forceEvaluate(const CallerContext& caller);
};

class StrictLazyObjectType : public StrictType {
 public:
  using StrictType::StrictType;

  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> setDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> delDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> loadAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> defaultValue,
      const CallerContext& caller) override;

  virtual void storeAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual void delAttr(
      std::shared_ptr<BaseStrictObject> obj,
      const std::string& key,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> binOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> right,
      operator_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> reverseBinOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> left,
      operator_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> unaryOp(
      std::shared_ptr<BaseStrictObject> obj,
      unaryop_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> binCmpOp(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> right,
      cmpop_ty op,
      const CallerContext& caller) override;

  virtual std::shared_ptr<StrictIteratorBase> getElementsIter(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::vector<std::shared_ptr<BaseStrictObject>> getElementsVec(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> getElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      const CallerContext& caller) override;

  virtual void setElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller) override;

  virtual void delElement(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> index,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& argNames,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

} // namespace strictmod::objects
