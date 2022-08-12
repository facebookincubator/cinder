#pragma once

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This file contains signatures for functions that live in various
 * CPython-internal files (e.g., Objects/funcobject.c, Python/ceval.c) so they
 * can access private functions or data, and are not intended to be candidates
 * for upstreaming. They should all go away one way or another as part of our
 * CinderVM work.
 */

PyObject *Ci_PyClassMethod_GetFunc(PyObject *method);
PyObject *Ci_PyStaticMethod_GetFunc(PyObject *method);
PyObject *Ci_PyMethodDef_GetTypedSignature(PyMethodDef *method);

PyAPI_FUNC(PyObject *) Ci_dict_subscript(PyObject *mp, PyObject *key);
PyAPI_FUNC(PyObject *) Ci_list_subscript(PyObject *list, PyObject *item);
PyAPI_FUNC(PyObject *) Ci_tuple_subscript(PyObject *self, PyObject *item);

/* Force the dictionary to use a combined layout.
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) Ci_PyDict_ForceCombined(PyObject *);

PyObject **
Ci_PyObject_GetDictPtrAtOffset(PyObject *obj, Py_ssize_t dictoffset);

PyAPI_FUNC(PyObject *) special_lookup(PyThreadState *tstate, PyObject *o, _Py_Identifier *id);
PyAPI_FUNC(int) check_args_iterable(PyThreadState *tstate, PyObject *func, PyObject *args);
PyAPI_FUNC(void) format_kwargs_error(PyThreadState *tstate, PyObject *func, PyObject *kwargs);
PyAPI_FUNC(void) format_awaitable_error(PyThreadState *tstate, PyTypeObject *type, int prevprevopcode, int prevopcode);
PyAPI_FUNC(void)
    format_exc_check_arg(PyThreadState *, PyObject *, const char *, PyObject *);
int do_raise(PyThreadState *tstate, PyObject *exc, PyObject *cause);

PyAPI_FUNC(PyObject *) Ci_GetAIter(PyThreadState *tstate, PyObject *obj);
PyAPI_FUNC(PyObject *) Ci_GetANext(PyThreadState *tstate, PyObject *aiter);


/* Enable or disable interpreter type profiling for all threads or for a
   specific thread. */
PyAPI_FUNC(void) Ci_ThreadState_SetProfileInterpAll(int);
PyAPI_FUNC(void) Ci_ThreadState_SetProfileInterp(PyThreadState *, int);

/* Set the profile period for interpreter type profiling, in bytecode
   instructions. */
PyAPI_FUNC(void) Ci_RuntimeState_SetProfileInterpPeriod(long);

PyAPI_FUNC(PyObject *) match_keys(PyThreadState *tstate, PyObject *map, PyObject *keys);

PyAPI_FUNC(void) Ci_set_attribute_error_context(PyObject *v, PyObject *name);

extern int (*Ci_List_APPEND)(PyListObject *list, PyObject *item);

PyAPI_FUNC(PyObject *) Ci_List_Repeat(PyListObject *, Py_ssize_t);
PyAPI_FUNC(PyObject *) Ci_Tuple_Repeat(PyTupleObject *, Py_ssize_t);

// Originally in Include/object.h
#define Ci_Py_TPFLAG_CPYTHON_ALLOCATED (1UL << 2)
#define Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED (1UL << 3)
/* This is a generic type instantiation */
#define Ci_Py_TPFLAGS_GENERIC_TYPE_INST (1UL << 15)
/* This type is a generic type definition */
#define Ci_Py_TPFLAGS_GENERIC_TYPE_DEF (1UL << 16)

#define Ci_Py_TPFLAGS_FROZEN (1UL << 21)

// Implementation in Python/bltinmodule.c
PyObject *
builtin_next(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyAPI_FUNC(PyObject *)
Ci_Builtin_Next_Core(PyObject *it, PyObject *def);

#ifdef __cplusplus
}
#endif
