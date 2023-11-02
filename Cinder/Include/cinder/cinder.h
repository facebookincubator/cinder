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
 * Watch or unwatch a dictionary.
 */
PyAPI_FUNC(void) Cinder_WatchDict(PyObject* dict);
PyAPI_FUNC(void) Cinder_UnwatchDict(PyObject* dict);

/*
 * Watch or unwatch a type.
 */
PyAPI_FUNC(void) Cinder_WatchType(PyTypeObject* type);
PyAPI_FUNC(void) Cinder_UnwatchType(PyTypeObject* type);


/*
 * Enable parallel garbage collection for generations >= min_gen, using
 * num_threads threads to parallelize the process.
 *
 * Performance tends to scale linearly with the number of threads used,
 * plateauing once the number of threads equals the number of cores.
 *
 * Returns 0 on success or -1 with an exception set on error.
 */
PyAPI_FUNC(int) Cinder_EnableParallelGC(size_t min_gen, size_t num_threads);

/*
 * Returns a dictionary containing parallel gc settings or None when
 * parallel gc is disabled.
 */
PyAPI_FUNC(PyObject *) Cinder_GetParallelGCSettings(void);

/*
 * Disable parallel gc.
 *
 * This will not affect the current collection if run from a finalizer.
 */
PyAPI_FUNC(void) Cinder_DisableParallelGC(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* Py_LIMITED_API */
