// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _PyClassLoader_Awaitable;

typedef PyObject * (*awaitable_cb)(struct _PyClassLoader_Awaitable *self, PyObject *state);

typedef int (*awaitable_presend)(struct _PyClassLoader_Awaitable *self);

/**
    Type-checking coroutines is more involved than normal, because all awaitables just
    yield new awaitables. In this case, we wrap up any awaitable into this struct,
    and do the required checks whenever a value is returned.
*/
typedef struct _PyClassLoader_Awaitable {
    PyObject_HEAD
    PyObject *state;
    PyObject *coro;
    PyObject *iter;
    awaitable_cb cb;
    awaitable_presend onsend;
    PyObject *awaiter;
} _PyClassLoader_Awaitable;

CiAPI_FUNC(PyObject *)
_PyClassLoader_NewAwaitableWrapper(
  PyObject *coro,
  int eager,
  PyObject *state,
  awaitable_cb cb,
  awaitable_presend onsend);

#ifdef __cplusplus
}
#endif
