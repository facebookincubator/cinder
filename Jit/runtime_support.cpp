// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/runtime_support.h"

#include "Python.h"
#include "cinder/port-assert.h"
#include "internal/pycore_pyerrors.h"
#include "pyreadonly.h"

#include "Jit/log.h"

namespace jit {

// TODO(T125857223): Use the external immortal refcount kImmortalInitialCount
// instead of this local copy.
static const Py_ssize_t kImmortalBitPos = 8 * sizeof(Py_ssize_t) - 4;
static const Py_ssize_t kImmortalBit = 1L << kImmortalBitPos;
static const Py_ssize_t kImmortalInitialCount = kImmortalBit;
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
