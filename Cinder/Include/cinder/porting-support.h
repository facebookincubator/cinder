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


// Include/code.h
#define CO_STATICALLY_COMPILED   0x4000000
#define CO_FUTURE_LAZY_IMPORTS   0x8000000
#define CO_SHADOW_FRAME          0x10000000
#define CO_NORMAL_FRAME          0x20000000
#define CO_SUPPRESS_JIT          0x40000000


// Python/ceval.h

// TODO(T127678238)
PyAPI_FUNC(PyObject *) _PyEval_SuperLookupMethodOrAttr(
    PyThreadState *tstate,
    PyObject *super_globals,
    PyTypeObject *type,
    PyObject *self,
    PyObject *name,
    int call_no_args,
    int *meth_found);

// Include/genobject.h  TODO(T125856469) Eager coroutine execution
typedef struct {
    PyObject_HEAD
    PyObject *wh_coro_or_result_NOT_IMPLEMENTED;
    PyObject *wh_waiter_NOT_IMPLEMENTED;
} PyWaitHandleObject;
PyAPI_DATA(PyTypeObject) PyWaitHandle_Type;
#define _PyWaitHandle_CheckExact(op) (Py_TYPE(op) == &PyWaitHandle_Type)
PyAPI_FUNC(PyObject *)
    _PyWaitHandle_New(PyObject *coro_or_result, PyObject *waiter);
PyAPI_FUNC(void) _PyWaitHandle_Release(PyObject *wait_handle);

// TODO(T125845107) Shadow frames
PyAPI_FUNC(PyObject *) _PyCoro_NewNoFrame(
    PyThreadState *tstate, PyCodeObject *code);
PyAPI_FUNC(PyObject *) _PyAsyncGen_NewNoFrame(PyCodeObject *code);
PyAPI_FUNC(PyObject *) _PyGen_NewNoFrame(PyCodeObject *code);

// TODO(T124996749): Until we port immortal objects, it's safe to always say
// nothing is immortal.
#define Py_IS_IMMORTAL(v) ((void)v, 0)

// Include/cpython/abstract.h
// TODO(T125856469) Eager coroutine execution
// This needs to be "static inline" when implemented.
Py_ssize_t PyVectorcall_FLAGS(size_t n);
PyObject *_PyVectorcall_Call(PyObject *callable, PyObject *tuple, PyObject *kwargs, size_t flags);


#ifdef __cplusplus
} // extern "C"
#endif
