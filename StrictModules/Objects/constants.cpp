#include "StrictModules/Objects/constants.h"
namespace strictmod::objects {
// NoneObject_
PyObject* NoneObject_::getPyObject() const {
  Py_RETURN_NONE;
}
std::string NoneObject_::getDisplayName() const {
  return "None";
}

// NotImplementedObject
PyObject* NotImplementedObject::getPyObject() const {
  Py_RETURN_NOTIMPLEMENTED;
}
std::string NotImplementedObject::getDisplayName() const {
  return "NotImplemented()";
}
} // namespace strictmod::objects
