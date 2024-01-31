// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

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
