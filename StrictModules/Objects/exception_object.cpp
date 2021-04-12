#include "StrictModules/Objects/exception_object.h"

#include "StrictModules/Objects/objects.h"

#include <sstream>
namespace strictmod::objects {
std::string StrictExceptionObject::getDisplayName() const {
  if (displayName_.empty()) {
    std::ostringstream os;
    os << "<exception " << type_->getDisplayName() << ">";
    displayName_ = os.str();
  }
  return displayName_;
}
} // namespace strictmod::objects
