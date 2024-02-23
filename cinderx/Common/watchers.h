// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Watcher callback types.
 */
typedef int (*CodeWatcher)(PyCodeEvent, PyCodeObject*);
typedef int (*DictWatcher)(PyDict_WatchEvent, PyObject*, PyObject*, PyObject*);
typedef int (*FuncWatcher)(PyFunction_WatchEvent, PyFunctionObject*, PyObject*);
typedef int (*TypeWatcher)(PyTypeObject*);

typedef struct {
  CodeWatcher code_watcher;
  DictWatcher dict_watcher;
  FuncWatcher func_watcher;
  TypeWatcher type_watcher;
} WatcherState;

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

int Ci_Watchers_Init(const WatcherState* state);
int Ci_Watchers_Fini(void);

#ifdef __cplusplus
}
#endif
