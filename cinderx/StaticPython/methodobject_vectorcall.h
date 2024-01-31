/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef Py_METHODOBJECT_VECTORCALL_H
#define Py_METHODOBJECT_VECTORCALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Python.h"

vectorcallfunc
Ci_PyCMethod_New_METH_TYPED(PyMethodDef *method);

PyObject *
Ci_meth_get__typed_signature__(PyCFunctionObject *m, void *closure);

#ifdef __cplusplus
}
#endif
#endif /* !Py_METHODOBJECT_VECTORCALL_H */
