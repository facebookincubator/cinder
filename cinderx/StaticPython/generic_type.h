// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

// The rest of the TPFLAGS are in Include/object.h
/* This is a generic type instantiation */
#define Ci_Py_TPFLAGS_GENERIC_TYPE_INST (1UL << 15)
/* This type is a generic type definition */
#define Ci_Py_TPFLAGS_GENERIC_TYPE_DEF (1UL << 16)

typedef struct {
    PyTypeObject gtd_type; /* base type object */
    newfunc gtd_new;       /* real tp_new func for instances */
    Py_ssize_t gtd_size;   /* number of generic type parameters */
} _PyGenericTypeDef;

typedef struct {
    PyTypeObject *gtp_type;
    int gtp_optional;
} _PyGenericTypeParam;

typedef struct {
    PyHeapTypeObject gti_type; /* base type object */
    _PyGenericTypeDef *gti_gtd;
    Py_ssize_t gti_size;            /* number of generic type parameters */
    _PyGenericTypeParam gti_inst[]; /* generic type parameters */
} _PyGenericTypeInst;

CiAPI_FUNC(PyObject *)_PyClassLoader_GtdGetItem(_PyGenericTypeDef *type, PyObject *args);

CiAPI_STATIC_INLINE_FUNC(_PyGenericTypeDef *)
_PyClassLoader_GetGenericTypeDefFromType(PyTypeObject *gen_type);
CiAPI_STATIC_INLINE_FUNC(_PyGenericTypeDef *)
_PyClassLoader_GetGenericTypeDef(PyObject *gen_inst);

/* gets the generic type definition for an instance if it is an instance of a
 * generic type, or returns NULL if it is not */
static inline _PyGenericTypeDef *
_PyClassLoader_GetGenericTypeDefFromType(PyTypeObject *gen_type)
{
    if (!(gen_type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST)) {
        return NULL;
    }
    return ((_PyGenericTypeInst *)gen_type)->gti_gtd;
}

static inline _PyGenericTypeDef *
_PyClassLoader_GetGenericTypeDef(PyObject *gen_inst)
{
    PyTypeObject *inst_type = Py_TYPE(gen_inst);
    return _PyClassLoader_GetGenericTypeDefFromType(inst_type);
}

CiAPI_FUNC(int) _PyClassLoader_TypeDealloc(PyTypeObject *type);
CiAPI_FUNC(int) _PyClassLoader_TypeTraverse(PyTypeObject *type, visitproc visit, void *arg);
CiAPI_FUNC(void) _PyClassLoader_TypeClear(PyTypeObject *type);

#ifdef __cplusplus
}
#endif
