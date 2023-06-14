#include "Python.h"

static int gdb_tracefunc(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg) {
  return 0;
}

static PyObject* gdb_enable_trace(PyObject* Py_UNUSED(a1), PyObject* Py_UNUSED(a2)) {
  PyEval_SetTrace(gdb_tracefunc, NULL);
  Py_RETURN_NONE;
}

static PyObject* gdb_disable_trace(PyObject* Py_UNUSED(a1), PyObject* Py_UNUSED(a2)) {
  PyEval_SetTrace(NULL, NULL);
  Py_RETURN_NONE;
}

static PyMethodDef gdb_dbg_methods[] = {
    {"gdb_enable_trace", gdb_enable_trace, METH_NOARGS, NULL},
    {"gdb_disable_trace", gdb_disable_trace, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef gdb_dbg_module = {
    PyModuleDef_HEAD_INIT,
    "gdb_dbg",
    "Bits and pieces to enable native debugging of Python",
    -1,
    gdb_dbg_methods
};

PyMODINIT_FUNC PyInit_gdb_dbg(void) {
    return PyModule_Create(&gdb_dbg_module);
}
