// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/runtime_support.h"

#include "Python.h"
#include "internal/pycore_pyerrors.h"

#include "Jit/log.h"

namespace jit {

PyObject g_iterDoneSentinel = {
    _PyObject_EXTRA_INIT kImmortalInitialCount,
    nullptr};

PyObject* invokeIterNext(PyObject* iterator) {
  PyObject* val = (*iterator->ob_type->tp_iternext)(iterator);
  if (val != nullptr) {
    return val;
  }
  if (PyErr_Occurred()) {
    if (!PyErr_ExceptionMatches(PyExc_StopIteration)) {
      return nullptr;
    }
    PyErr_Clear();
  }
  Py_INCREF(&g_iterDoneSentinel);
  return &g_iterDoneSentinel;
}

} // namespace jit
