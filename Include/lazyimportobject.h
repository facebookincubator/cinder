/* Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved */
/* File added for Lazy Imports */

/* Lazy object interface */

#ifndef Py_LAZYIMPORTOBJECT_H
#define Py_LAZYIMPORTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_LIMITED_API
PyAPI_DATA(PyTypeObject) PyLazyImport_Type;
#define PyLazyImport_CheckExact(op) Py_IS_TYPE((op), &PyLazyImport_Type)
#endif

#ifdef __cplusplus
}
#endif
#endif /* !Py_LAZYIMPORTOBJECT_H */
