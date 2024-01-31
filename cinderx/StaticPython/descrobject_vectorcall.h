/* Copyright (c) Meta Platforms, Inc. and affiliates. */
#ifndef Py_DESCROBJECT_VECTORCALL_H
#define Py_DESCROBJECT_VECTORCALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Python.h"

vectorcallfunc
Ci_PyDescr_NewMethod_METH_TYPED(PyMethodDef *method);

PyObject *
Ci_method_get_typed_signature(PyMethodDescrObject *descr, void *closure);

#ifdef __cplusplus
}
#endif
#endif /* !Py_DESCROBJECT_VECTORCALL_H */
