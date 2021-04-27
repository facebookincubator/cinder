#ifndef __STRICTM_INSTANCE_H__
#define __STRICTM_INSTANCE_H__
#include "StrictModules/Objects/base_object.h"

namespace strictmod::objects {

class StrictInstance : public BaseStrictObject {
 public:
  StrictInstance(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      std::shared_ptr<DictType> dict = nullptr);

  StrictInstance(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<DictType> dict = nullptr);

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;
  virtual std::unique_ptr<BaseStrictObject> copy() const override;

  std::shared_ptr<BaseStrictObject> getAttr(const std::string& name);
  void setAttr(std::string name, std::shared_ptr<BaseStrictObject> value);

  DictType& getDict() {
    return *dict_;
  }

 protected:
  std::shared_ptr<DictType> dict_;
};
} // namespace strictmod::objects
#endif // !__STRICTM_INSTANCE_H__
