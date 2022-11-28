// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

namespace jit {

// A PyObject that is used to indicate that an iterator has finished
// normally. This must never escape into managed code.
extern PyObject g_iterDoneSentinel;

// Invoke __next__ on iterator
PyObject* invokeIterNext(PyObject* iterator);

// Invoke __next__ on iterator with readonly checks
PyObject* invokeIterNextReadonly(PyObject* iterator, int readonly_mask);

} // namespace jit
