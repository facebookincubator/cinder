#ifndef __STRICTM_STRING_OBJ_H__
#define __STRICTM_STRING_OBJ_H__

#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/object_type.h"
namespace strictmod::objects {

class StrictString : public StrictInstance {
 public:
  StrictString(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      std::string value);

  StrictString(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::string value);

  StrictString(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      PyObject* pyValue);

  virtual bool isHashable() const override;
  virtual size_t hash() const override;
  virtual bool eq(const BaseStrictObject& other) const override;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> str__len__(
      std::shared_ptr<StrictString> self,
      const CallerContext& caller);

 private:
  mutable Ref<> pyStr_;
  std::string value_;
};

class StrictStringType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::shared_ptr<StrictModuleObject> caller) override;

  virtual Ref<> getPyObject() const override;

  virtual void addMethods() override;
};

} // namespace strictmod::objects

#endif // #ifndef __STRICTM_STRING_OBJ_H__
