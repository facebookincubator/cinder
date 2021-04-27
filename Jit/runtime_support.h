// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef JIT_RUNTIME_SUPPORT_H
#define JIT_RUNTIME_SUPPORT_H

#include "Python.h"

namespace jit {

// A PyObject that is used to indicate that an iterator has finished
// normally. This must never escape into managed code.
extern PyObject g_iterDoneSentinel;

// Invoke __next__ on iterator
PyObject* invokeIterNext(PyObject* iterator);

// Run periodic tasks and give other threads a chance to run.
//
// Returns a borrowed reference to Py_True on success. On error, returns
// nullptr with an exception set.
PyObject* runPeriodicTasks();

} // namespace jit

#endif
