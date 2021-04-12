#ifndef __STRICTM_EXCEPTION_OBJ_H__
#define __STRICTM_EXCEPTION_OBJ_H__

#include "StrictModules/Objects/instance.h"

namespace strictmod::objects {
class StrictExceptionObject : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;

  virtual std::string getDisplayName() const override;

 private:
  mutable std::string displayName_;
};

} // namespace strictmod::objects
#endif //__STRICTM_EXCEPTION_OBJ_H__
