/* Copyright (c) Meta, Inc. and its affiliates. (http://www.facebook.com) */
#include "Python.h"

#include "pycore_shadow_frame.h"

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

#define _SF_STACK_SIZE 1024
static PyObject *
shadowframe_walk_and_populate(PyObject *self, PyObject *args)
{
    PyCodeObject* async_stack[_SF_STACK_SIZE] = {0};
    PyCodeObject* sync_stack[_SF_STACK_SIZE] = {0};
    int async_linenos[_SF_STACK_SIZE] = {0};
    int sync_linenos[_SF_STACK_SIZE] = {0};
    int async_len = 0;
    int sync_len = 0;
    int res = _PyShadowFrame_WalkAndPopulate(
        async_stack,
        async_linenos,
        sync_stack,
        sync_linenos,
        _SF_STACK_SIZE,
        &async_len,
        &sync_len
    );
    if (res) {
        PyErr_SetString(PyExc_RuntimeError, "_shadowframe_walk_and_populate: failed");
        return NULL;
    }

    PyObject *async_res = PyList_New(0);
    if (!async_res) {
        return NULL;
    }

    for (int i=0; i < _SF_STACK_SIZE; i++) {
        PyCodeObject* code = async_stack[i];
        if (code == NULL) {
            // end of stack
            if (i != async_len) {
                PyErr_Format(
                    PyExc_RuntimeError,
                    "Mismatch in async stack len: %d returned, %d calculated", async_len, i
                );
                return NULL;
            }
            break;
        }
        int lineno = async_linenos[i];
        PyObject *qname = PyUnicode_FromFormat(
                            "%U:%d:%U",
                            code->co_filename,
                            lineno,
                            code->co_qualname);
        if (!qname) {
            Py_DECREF(async_res);
            return NULL;
        }
        if (PyList_Append(async_res, qname)) {
            Py_DECREF(async_res);
            Py_DECREF(qname);
            return NULL;
        }
        Py_DECREF(qname);
    }

    PyObject *sync_res = PyList_New(0);
    if (!sync_res) {
        Py_DECREF(async_res);
        return NULL;
    }

    for (int i=0; i < _SF_STACK_SIZE; i++) {
        PyCodeObject* code = sync_stack[i];
        if (code == 0) {
            // end of stack
            if (i != sync_len) {
                PyErr_Format(
                    PyExc_RuntimeError,
                    "Mismatch in sync stack len: %d returned, %d calculated", sync_len, i
                );
                return NULL;
            }
            break;
        }
        int lineno = sync_linenos[i];
        PyObject *qname = PyUnicode_FromFormat(
                            "%U:%d:%U",
                            code->co_filename,
                            lineno,
                            code->co_qualname);
        if (!qname) {
            Py_DECREF(async_res);
            Py_DECREF(sync_res);
            return NULL;
        }
        if (PyList_Append(sync_res, qname)) {
            Py_DECREF(async_res);
            Py_DECREF(sync_res);
            Py_DECREF(qname);
            return NULL;
        }
        Py_DECREF(qname);
    }

    PyObject *result = PyTuple_New(2);
    if (!result) {
        Py_DECREF(async_res);
        Py_DECREF(sync_res);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 0, async_res);
    PyTuple_SET_ITEM(result, 1, sync_res);

    return result;
}
#undef _SF_STACK_SIZE

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
    {"_shadowframe_walk_and_populate", shadowframe_walk_and_populate, METH_NOARGS},
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
