/* Copyright (c) Meta, Inc. and its affiliates. (http://www.facebook.com) */
#include "Python.h"

static PyObject *
call_pyeval_get_builtins(PyObject *self, PyObject *obj)
{
    PyObject *builtins = PyEval_GetBuiltins();
    Py_XINCREF(builtins);
    return builtins;
}

static PyObject *
call_pyeval_merge_compiler_flags(PyObject *self, PyObject *obj)
{
    PyCompilerFlags cf = {
      .cf_flags = 0,
      .cf_feature_version = 0,
    };
    PyEval_MergeCompilerFlags(&cf);
    return PyLong_FromLong(cf.cf_flags);
}

PyDoc_STRVAR(doc_testcindercapi, "Helpers to test Cinder specific C-APIs and Cinder specific modifications to upstream C-APIs");

static struct PyMethodDef testcindercapi_module_methods[] = {
  {"_pyeval_get_builtins",
   call_pyeval_get_builtins,
   METH_NOARGS,
   "Return the builtins for the top-most frame."},
  {"_pyeval_merge_compiler_flags",
   call_pyeval_merge_compiler_flags,
   METH_NOARGS,
   "Return compiler flags for the top-most frame via PyEval_MergeCompilerFlags."},
  {NULL, NULL},
};

static struct PyModuleDef testcindercapimodule = {
    PyModuleDef_HEAD_INIT,
    "_testcindercapi",
    doc_testcindercapi,
    -1,
    testcindercapi_module_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit__testcindercapi(void)
{
    return PyModule_Create(&testcindercapimodule);
}
