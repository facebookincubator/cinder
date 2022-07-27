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

PyAPI_FUNC(PyObject *) Ci_dict_subscript(PyObject *mp, PyObject *key);
PyAPI_FUNC(PyObject *) Ci_list_subscript(PyObject *list, PyObject *item);
PyAPI_FUNC(PyObject *) Ci_tuple_subscript(PyObject *self, PyObject *item);

/* Force the dictionary to use a combined layout.
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) Ci_PyDict_ForceCombined(PyObject *);

PyAPI_FUNC(PyObject *) special_lookup(PyThreadState *tstate, PyObject *o, _Py_Identifier *id);
PyAPI_FUNC(int) check_args_iterable(PyThreadState *tstate, PyObject *func, PyObject *args);
PyAPI_FUNC(void) format_kwargs_error(PyThreadState *tstate, PyObject *func, PyObject *kwargs);
PyAPI_FUNC(void) format_awaitable_error(PyThreadState *tstate, PyTypeObject *type, int prevprevopcode, int prevopcode);

#ifdef __cplusplus
}
#endif
