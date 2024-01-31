// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Watch or unwatch a dictionary.
 */
PyAPI_FUNC(void) Ci_Watchers_WatchDict(PyObject* dict);
PyAPI_FUNC(void) Ci_Watchers_UnwatchDict(PyObject* dict);

/*
 * Watch or unwatch a type.
 */
PyAPI_FUNC(void) Ci_Watchers_WatchType(PyTypeObject* type);
PyAPI_FUNC(void) Ci_Watchers_UnwatchType(PyTypeObject* type);


int Ci_Watchers_Init(void);
int Ci_Watchers_Fini(void);

#ifdef __cplusplus
}
#endif
