// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/runtime_support.h"

#include "Python.h"
#include "cinder/port-assert.h"
#include "internal/pycore_pyerrors.h"
#include "pyreadonly.h"

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

PyObject* invokeIterNextReadonly(PyObject* iterator, int readonly_mask) {
  if (readonly_mask && PyReadonly_BeginReadonlyOperation(readonly_mask) != 0) {
    return nullptr;
  }
  PyObject* val = (*iterator->ob_type->tp_iternext)(iterator);
  if (readonly_mask && PyReadonly_CheckReadonlyOperation(0, 0) != 0) {
    return nullptr;
  }
  if (readonly_mask && PyReadonly_VerifyReadonlyOperationCompleted() != 0) {
    return nullptr;
  }
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
