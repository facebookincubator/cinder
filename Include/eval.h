
/* Interface to execute compiled code */

#ifndef Py_EVAL_H
#define Py_EVAL_H
#ifdef __cplusplus
extern "C" {
#endif

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

CiAPI_DATA(int) _PyEval_LazyImportsEnabled;
CiAPI_DATA(int) _PyEval_ShadowByteCodeEnabled;
CiAPI_DATA(int) _PyShadow_PolymorphicCacheEnabled;

CiAPI_FUNC(PyObject *) _PyFunction_CallStatic(PyFunctionObject *func,
                                 PyObject* const* args,
                                 Py_ssize_t nargsf,
                                 PyObject *kwnames);

#endif

#ifdef __cplusplus
}
#endif
#endif /* !Py_EVAL_H */
