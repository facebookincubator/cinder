#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "clinic/_importlib.c.h"

static PyObject *
is_lazy_key_impl(PyObject *module, PyObject *dict, PyObject *key)
{
    int res = PyDict_IsLazyImport(dict, key);

    if (res == 1) {
        return Py_BuildValue("O", Py_True);
    }
    return Py_BuildValue("O", Py_False);
}

static PyMethodDef ImportlibMethods[] = {
    IS_LAZY_KEY_METHODDEF
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
