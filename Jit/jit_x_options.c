#include "Jit/jit_x_options.h"

int PyJIT_GetXOption(const char* option, PyObject** value) {
  PyObject* xoptions = PySys_GetXOptions();
  if (xoptions == NULL) {
    return -1;
  }

  PyObject* key = PyUnicode_FromString(option);
  if (key == NULL) {
    return -1;
  }

  *value = PyDict_GetItemWithError(xoptions, key);
  Py_DECREF(key);
  if (*value == NULL && PyErr_Occurred()) {
    return -1;
  }

  return 0;
}

int PyJIT_IsXOptionSet(const char* option) {
  PyObject* xoptions = PySys_GetXOptions();
  if (xoptions == NULL) {
    return -1;
  }

  PyObject* key = PyUnicode_FromString(option);
  if (key == NULL) {
    return -1;
  }

  PyObject* value = PyDict_GetItemWithError(xoptions, key);
  Py_DECREF(key);
  if (value == NULL) {
    if (PyErr_Occurred()) {
      PyErr_Clear();
      return -1;
    }
    return 0;
  }
  return 1;
}
