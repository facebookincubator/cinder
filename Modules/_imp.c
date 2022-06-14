#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "clinic/_imp.c.h"

static PyObject *
is_lazy_key_impl(PyObject *module, PyObject *dict, PyObject *key)
{
    int res = PyDict_IsLazyKey(dict, key);
    return Py_BuildValue("i", res);
}

static PyMethodDef LazyDebugMethods[] = {
    IS_LAZY_KEY_METHODDEF
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef lazydebugmodule = {
    PyModuleDef_HEAD_INIT,
    "_lazydebug",
    NULL,
    0,
    LazyDebugMethods
};

PyMODINIT_FUNC
PyInit__lazydebug(void)
{
    return PyModuleDef_Init(&lazydebugmodule);
}
