#pragma once

#include "Python.h"

#include "internal/pycore_moduleobject.h"

#include "cinder/port-assert.h"

/*
 * Collection of misc. features implemented for Cinder which were previously
 * spread throughout the CPython code. Stubbed versions of these functions are
 * in stubs.cpp.
 *
 * Where possible please avoid re-implementing these back in the CPython sources.
 * Instead, make appropriate new .c/.cpp/.h files here. This will help simplify
 * future upstream merges, and make our additions/alternations clear and explicit.
 *
 * The file references indicate where these functions can be found in the 3.8
 * Cinder sources.
 */


// When implementing these functions, this may not be needed in new headers if
// the functions are only used by Cinder C++ code.
#ifdef __cplusplus
extern "C" {
#endif

// TODO(T125845107) Shadow frames
PyAPI_FUNC(PyObject *) _PyCoro_NewNoFrame(
    PyThreadState *tstate, PyCodeObject *code);
PyAPI_FUNC(PyObject *) _PyAsyncGen_NewNoFrame(PyCodeObject *code);
PyAPI_FUNC(PyObject *) _PyGen_NewNoFrame(PyCodeObject *code);

#ifdef __cplusplus
} // extern "C"
#endif
