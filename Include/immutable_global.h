/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
/* facebook begin */

#ifndef Py_IMMUTABLE_GLOBAL_H
#define Py_IMMUTABLE_GLOBAL_H

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif
/* immutable_globals state */
#ifndef _PY_IMMUTABLE_GLOBALS
#define _PY_IMMUTABLE_GLOBALS
PyAPI_DATA(int) _PyImmutableGlobal_CreationContext;
PyAPI_DATA(int) _PyImmutableGlobal_DetectionContext;

void _PyImmutableGlobal_SetCreationContext(int flag);
void _PyImmutableGlobal_SetDetectionContext(int flag);
int _PyImmutableGlobal_MakeImmutable(PyObject* obj);

PyAPI_FUNC(PyTypeObject*) _PyImmutableGlobal_GetImmutableType(PyTypeObject* tp);

PyAPI_FUNC(int) _PyImmutableGlobal_Init(void);

#endif
#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
#endif /* !Py_IMMUTABLE_GLOBAL_H */
