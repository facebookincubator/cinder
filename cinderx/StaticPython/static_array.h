/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef Ci_STATIC_ARRAY_H
#define Ci_STATIC_ARRAY_H

#include "cinder/exports.h"

#ifdef __cplusplus
extern "C" {
#endif

CiAPI_DATA(PyTypeObject) PyStaticArray_Type;
#define PyStaticArray_CheckExact(op) Py_IS_TYPE(op, &PyStaticArray_Type)

typedef struct {
    PyObject_VAR_HEAD
    /* ob_item contains space for 'ob_size' elements. */
    int64_t ob_item[1];
} PyStaticArrayObject;


CiAPI_FUNC(int) _Ci_StaticArray_Set(PyObject *array, Py_ssize_t index, PyObject *value);
CiAPI_FUNC(PyObject*) _Ci_StaticArray_Get(PyObject *array, Py_ssize_t index);

#ifdef __cplusplus
}
#endif

#endif
