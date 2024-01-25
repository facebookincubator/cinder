/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#ifndef Ci_CHECKED_DICT_H
#define Ci_CHECKED_DICT_H

#include "Python.h"

#include "cinderx/StaticPython/classloader.h"

#ifdef __cplusplus
extern "C" {
#endif

CiAPI_DATA(_PyGenericTypeDef) Ci_CheckedDict_Type;
CiAPI_FUNC(PyObject *) Ci_CheckedDict_New(PyTypeObject *type);
CiAPI_FUNC(PyObject *) Ci_CheckedDict_NewPresized(PyTypeObject *type, Py_ssize_t minused);

CiAPI_FUNC(int) Ci_CheckedDict_Check(PyObject *x);
CiAPI_FUNC(int) Ci_CheckedDict_TypeCheck(PyTypeObject *type);

CiAPI_FUNC(int) Ci_CheckedDict_SetItem(PyObject *op, PyObject *key, PyObject *value);

CiAPI_FUNC(int) Ci_DictOrChecked_SetItem(PyObject *op, PyObject *key, PyObject *value);

#ifdef __cplusplus
}
#endif
#endif /* !Ci_CHECKED_DICT_H */
