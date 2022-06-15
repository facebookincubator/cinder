#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "clinic/_importlib.c.h"

static PyObject *
is_lazy_import_impl(PyObject *module, PyObject *dict, PyObject *key)
{
    int res = PyDict_IsLazyImport(dict, key);

    if (res == -1) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    else if (res == 1) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef ImportlibMethods[] = {
    IS_LAZY_IMPORT_METHODDEF
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef importlibmodule = {
    PyModuleDef_HEAD_INIT,
    "_importlib",
    NULL,
    0,
    ImportlibMethods
};

PyMODINIT_FUNC
PyInit__importlib(void)
{
    return PyModuleDef_Init(&importlibmodule);
}
