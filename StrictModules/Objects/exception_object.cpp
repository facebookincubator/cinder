// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/exception_object.h"

#include "StrictModules/Objects/objects.h"

#include <sstream>
namespace strictmod::objects {
std::string StrictExceptionObject::getDisplayName() const {
  if (displayName_.empty()) {
    displayName_ = type_->getDisplayName();
  }
  return displayName_;
}
} // namespace strictmod::objects
