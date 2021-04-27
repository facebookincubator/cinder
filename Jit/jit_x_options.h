// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef Py_JIT_X_OPTIONS_H
#define Py_JIT_X_OPTIONS_H

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

// Utils for getting -X options passed to the Python runtime.

// If the given option is set, returns 0 and a borrowed reference to the value
// in value. If the option is not set, returns 0 and nullptr in value. Returns
// -1 on error.
int PyJIT_GetXOption(const char* option, PyObject** value);

// Returns 1 if the given option is set, 0 if not, and -1 on error.
int PyJIT_IsXOptionSet(const char* option);

#ifdef __cplusplus
}
#endif

#endif
