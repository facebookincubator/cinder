/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
/* facebook begin */

#include "Python.h"
/** Controls whether an object being created is considered "global".
    For builtins, an instance created in the creation context will have
    ob_type replaced with an immutable version.
    For user defined types, an instance created in the creation context will
    have immutable __dict__ and/or slots.
*/
int _PyImmutableGlobal_CreationContext = 0;
/** Controls whether any action is taken when an immutable global is modified.
    Outside of the detection context, the behavior of immutable global is the
    same as a regular global. (except that it still has a different type)
 */
int _PyImmutableGlobal_DetectionContext = 0;

void
_PyImmutableGlobal_SetCreationContext(int flag) {
    _PyImmutableGlobal_CreationContext = flag;
}

void
_PyImmutableGlobal_SetDetectionContext(int flag) {
    _PyImmutableGlobal_DetectionContext = flag;
}

int
_PyImmutableGlobal_MakeImmutable(PyObject *obj) {
    if (_PyImmutableGlobal_CreationContext) {
        Py_TYPE(obj) = _PyImmutableGlobal_GetImmutableType(Py_TYPE(obj));
    }
    return 0;
}

PyTypeObject *
_PyImmutableGlobal_GetImmutableType(PyTypeObject *tp) {
    return tp;
}
