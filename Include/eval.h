
/* Interface to execute compiled code */

#ifndef Py_EVAL_H
#define Py_EVAL_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_FUNC(PyObject *) _Py_DoImportFrom(PyThreadState *tstate,
                                        PyObject *v,
                                        PyObject *name);

PyAPI_FUNC(int) _Py_DoRaise(PyThreadState *, PyObject *, PyObject *);

PyAPI_FUNC(PyObject *) _PyEval_GetAIter(PyObject *obj);
PyAPI_FUNC(PyObject *) _PyEval_GetANext(PyObject *aiter);

PyAPI_FUNC(PyObject *) PyEval_EvalCode(PyObject *, PyObject *, PyObject *);

PyAPI_FUNC(PyObject *) PyEval_EvalCodeEx(PyObject *co,
                                         PyObject *globals,
                                         PyObject *locals,
                                         PyObject *const *args, int argc,
                                         PyObject *const *kwds, int kwdc,
                                         PyObject *const *defs, int defc,
                                         PyObject *kwdefs, PyObject *closure);

#ifndef Py_LIMITED_API
PyAPI_FUNC(PyObject *) _PyEval_EvalCodeWithName(
    PyObject *co,
    PyObject *globals, PyObject *locals,
    PyObject *const *args, Py_ssize_t argcount,
    PyObject *const *kwnames, PyObject *const *kwargs,
    Py_ssize_t kwcount, int kwstep,
    PyObject *const *defs, Py_ssize_t defcount,
    PyObject *kwdefs, PyObject *closure,
    PyObject *name, PyObject *qualname);

PyAPI_FUNC(PyObject *) _PyEval_CallTracing(PyObject *func, PyObject *args);

PyAPI_DATA(int) _PyEval_ShadowByteCodeEnabled; /* facebook */
PyAPI_DATA(int) _PyEval_LazyImportsEnabled; /* facebook */

PyAPI_FUNC(PyObject *) _PyEval_SuperLookupMethodOrAttr(
    PyThreadState *tstate,
    PyObject *super_globals,
    PyTypeObject *type,
    PyObject *self,
    PyObject *name,
    int call_no_args,
    int *meth_found);

#endif

#ifdef __cplusplus
}
#endif
#endif /* !Py_EVAL_H */
