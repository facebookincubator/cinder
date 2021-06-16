// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/constants.h"

#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context_impl.h"

namespace strictmod::objects {
// NoneObject_
Ref<> NoneObject_::getPyObject() const {
  return Ref(Py_None);
}
std::string NoneObject_::getDisplayName() const {
  return "None";
}

// NoneType_
Ref<> NoneType_::getPyObject() const {
  return Ref(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)));
}
std::string NoneType_::getDisplayName() const {
  return "NoneType";
}
std::shared_ptr<BaseStrictObject> NoneType_::getTruthValue(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller) {
  return caller.makeBool(false);
}

void NoneType_::addMethods() {
  addPyWrappedMethodObj<>(
      kDunderRepr,
      reinterpret_cast<PyObject*>(&_PyNone_Type),
      StrictString::strFromPyObj);
}

// NotImplementedObject
Ref<> NotImplementedObject::getPyObject() const {
  return Ref(Py_NotImplemented);
}
std::string NotImplementedObject::getDisplayName() const {
  return "NotImplemented()";
}
} // namespace strictmod::objects
