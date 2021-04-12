#ifndef Py_BLTINMODULE_H
#define Py_BLTINMODULE_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyTypeObject) PyFilter_Type;
PyAPI_DATA(PyTypeObject) PyMap_Type;
PyAPI_DATA(PyTypeObject) PyZip_Type;

PyObject *
_PyBuiltin_Next(PyObject *it, PyObject *def);

PyObject *
builtin_next(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

#ifdef __cplusplus
}
#endif
#endif /* !Py_BLTINMODULE_H */
