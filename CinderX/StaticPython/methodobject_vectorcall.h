/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */

#ifndef Py_METHODOBJECT_VECTORCALL_H
#define Py_METHODOBJECT_VECTORCALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Python.h"

vectorcallfunc
Ci_PyCMethod_New_METH_TYPED(PyMethodDef *method);

#ifdef __cplusplus
}
#endif
#endif /* !Py_METHODOBJECT_VECTORCALL_H */
