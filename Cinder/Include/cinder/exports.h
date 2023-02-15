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
PyAPI_FUNC(PyObject *) Ci_module_lookupattro(PyObject *self, PyObject *name, int suppress);

PyAPI_FUNC(Py_hash_t) Ci_TupleHashItems(PyObject *const *items, Py_ssize_t len);

#define Ci_List_GET_SIZE(op) Py_SIZE((PyListObject *)op)
#define Ci_List_SET_ITEM(op, i, v) ((void)(((PyListObject *)op)->ob_item[i] = (v)))
#define Ci_List_GET_ITEM(op, i) (((PyListObject *)op)->ob_item[i])

/* Force the dictionary to use a combined layout.
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) Ci_PyDict_ForceCombined(PyObject *);

PyObject *Ci_CheckedDict_New(PyTypeObject *type);
PyObject *Ci_CheckedDict_NewPresized(PyTypeObject *type, Py_ssize_t minused);
int Ci_Dict_SetItemInternal(PyObject *op, PyObject *key, PyObject *value);

int Ci_CheckedDict_Check(PyObject *x);
PyAPI_FUNC(int) Ci_CheckedDict_TypeCheck(PyTypeObject *type);

PyAPI_FUNC(PyObject *) Ci_CheckedList_GetItem(PyObject *self, Py_ssize_t);
PyAPI_FUNC(PyObject *) Ci_CheckedList_New(PyTypeObject *type, Py_ssize_t);
PyAPI_FUNC(int) Ci_CheckedList_TypeCheck(PyTypeObject *type);

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

PyAPI_FUNC(PyObject *)
    Ci_match_class(PyThreadState *tstate, PyObject *subject, PyObject *type,
                Py_ssize_t nargs, PyObject *kwargs);
PyAPI_FUNC(PyObject *)
    Ci_match_keys(PyThreadState *tstate, PyObject *map, PyObject *keys);

PyAPI_FUNC(int) Ci_set_attribute_error_context(PyObject *v, PyObject *name);

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

PyAPI_FUNC(int) Ci_Type_AssignVersionTag(PyTypeObject *);

// Implementation in Python/bltinmodule.c
PyObject *
builtin_next(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyAPI_FUNC(PyObject *)
Ci_Builtin_Next_Core(PyObject *it, PyObject *def);

typedef enum {
  CI_SWD_STOP_STACK_WALK = 0,
  CI_SWD_CONTINUE_STACK_WALK = 1,
} CiStackWalkDirective;

/*
 * A callback that will be invoked by Ci_WalkStack for each entry on the Python
 * call stack.
 */
typedef CiStackWalkDirective (*CiWalkStackCallback)(void *data,
                                                    PyCodeObject *code,
                                                    int lineno);

/*
 * Walk the stack, invoking cb for each entry with the supplied data parameter
 * as its first argument.
 *
 * The return value of cb controls whether or not stack walking continues.
 */
PyAPI_FUNC(void)
    Ci_WalkStack(PyThreadState *tstate, CiWalkStackCallback cb, void *data);

PyAPI_FUNC(PyObject *)
    CiCoro_New_NoFrame(PyThreadState *tstate, PyCodeObject *code);
PyAPI_FUNC(PyObject *) CiAsyncGen_New_NoFrame(PyCodeObject *code);
PyAPI_FUNC(PyObject *) CiGen_New_NoFrame(PyCodeObject *code);
PyAPI_FUNC(int) CiGen_close_yf(PyObject *yf);
PyAPI_FUNC(int) CiGen_restore_error(PyObject *et, PyObject *ev, PyObject *tb);

PyAPI_FUNC(PyObject *) Ci_SuperLookupMethodOrAttr(
    PyThreadState *tstate,
    PyObject *super_globals,
    PyTypeObject *type,
    PyObject *self,
    PyObject *name,
    int call_no_args,
    int *meth_found);


#ifdef __cplusplus
}
#endif
