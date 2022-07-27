
/* Interface to execute compiled code */

#ifndef Py_EVAL_H
#define Py_EVAL_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_FUNC(PyObject *) _Py_DoImportFrom(PyThreadState *tstate,
                                        PyObject *v,
                                        PyObject *name);

PyAPI_FUNC(PyObject *) PyEval_EvalCode(PyObject *, PyObject *, PyObject *);

PyAPI_FUNC(PyObject *) PyEval_EvalCodeEx(PyObject *co,
                                         PyObject *globals,
                                         PyObject *locals,
                                         PyObject *const *args, int argc,
                                         PyObject *const *kwds, int kwdc,
                                         PyObject *const *defs, int defc,
                                         PyObject *kwdefs, PyObject *closure);

#ifndef Py_LIMITED_API
PyAPI_FUNC(PyObject *) _PyEval_CallTracing(PyObject *func, PyObject *args);

PyAPI_DATA(int) _PyEval_LazyImportsEnabled;
PyAPI_DATA(int) _PyEval_ShadowByteCodeEnabled;
PyAPI_DATA(int) _PyShadow_PolymorphicCacheEnabled;

#endif

#ifdef __cplusplus
}
#endif
#endif /* !Py_EVAL_H */
