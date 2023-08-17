// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include <Python.h>

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Cinder global state.
 *
 * Initializes the JIT, and shared infrastructure such as watchers.
 *
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) Cinder_Init(void);

/* Finalize Cinder global state.
 *
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) Cinder_Fini(void);

/*
 * Initialize per-subinterpreter Cinder state.
 *
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) Cinder_InitSubInterp(void);

/*
 * Watch or unwatch a dictionary.
 */
PyAPI_FUNC(void) Cinder_WatchDict(PyObject* dict);
PyAPI_FUNC(void) Cinder_UnwatchDict(PyObject* dict);

/*
 * Watch or unwatch a type.
 */
PyAPI_FUNC(void) Cinder_WatchType(PyTypeObject* type);
PyAPI_FUNC(void) Cinder_UnwatchType(PyTypeObject* type);


#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* Py_LIMITED_API */
