// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/constants.h"
namespace strictmod::objects {
// NoneObject_
Ref<> NoneObject_::getPyObject() const {
  return Ref(Py_None);
}
std::string NoneObject_::getDisplayName() const {
  return "None";
}

// NotImplementedObject
Ref<> NotImplementedObject::getPyObject() const {
  return Ref(Py_NotImplemented);
}
std::string NotImplementedObject::getDisplayName() const {
  return "NotImplemented()";
}
} // namespace strictmod::objects
