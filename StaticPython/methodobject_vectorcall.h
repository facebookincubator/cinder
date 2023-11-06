/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */

#ifndef Py_METHODOBJECT_VECTORCALL_H
#define Py_METHODOBJECT_VECTORCALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Python.h"

PyObject *
Ci_cfunction_vectorcall_typed_0(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames);

PyObject *
Ci_cfunction_vectorcall_typed_1(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames);

PyObject *
Ci_cfunction_vectorcall_typed_2(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames);

#ifdef __cplusplus
}
#endif
#endif /* !Py_METHODOBJECT_VECTORCALL_H */
