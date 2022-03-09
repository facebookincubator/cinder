/*
 * C Extension module to test Python internal C APIs (Include/internal).
 */

#if !defined(Py_BUILD_CORE_BUILTIN) && !defined(Py_BUILD_CORE_MODULE)
#  error "Py_BUILD_CORE_BUILTIN or Py_BUILD_CORE_MODULE must be defined"
#endif

#define PY_SSIZE_T_CLEAN

#include "Python.h"
#include "pycore_initconfig.h"
#include "pycore_shadow_frame.h"


#ifdef MS_WINDOWS
#include <windows.h>

static int
_add_windows_config(PyObject *configs)
{
    HMODULE hPython3;
    wchar_t py3path[MAX_PATH];
    PyObject *dict = PyDict_New();
    PyObject *obj = NULL;
    if (!dict) {
        return -1;
    }

    hPython3 = GetModuleHandleW(PY3_DLLNAME);
    if (hPython3 && GetModuleFileNameW(hPython3, py3path, MAX_PATH)) {
        obj = PyUnicode_FromWideChar(py3path, -1);
    } else {
        obj = Py_None;
        Py_INCREF(obj);
    }
    if (obj &&
        !PyDict_SetItemString(dict, "python3_dll", obj) &&
        !PyDict_SetItemString(configs, "windows", dict)) {
        Py_DECREF(obj);
        Py_DECREF(dict);
        return 0;
    }
    Py_DECREF(obj);
    Py_DECREF(dict);
    return -1;
}
#endif


static PyObject *
get_configs(PyObject *self, PyObject *Py_UNUSED(args))
{
    PyObject *dict = _Py_GetConfigsAsDict();
#ifdef MS_WINDOWS
    if (dict) {
        if (_add_windows_config(dict) < 0) {
            Py_CLEAR(dict);
        }
    }
#endif
    return dict;
}

#define _SF_STACK_SIZE 1024
static PyObject *
test_shadowframe_walk_and_populate(PyObject *self, PyObject *args)
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
        PyErr_SetString(PyExc_RuntimeError, "test_shadowframe_walk_and_populate: failed");
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


static PyMethodDef TestMethods[] = {
    {"get_configs", get_configs, METH_NOARGS},
    {"test_shadowframe_walk_and_populate", test_shadowframe_walk_and_populate, METH_NOARGS},
    {NULL, NULL} /* sentinel */
};


static struct PyModuleDef _testcapimodule = {
    PyModuleDef_HEAD_INIT,
    "_testinternalcapi",
    NULL,
    -1,
    TestMethods,
    NULL,
    NULL,
    NULL,
    NULL
};


PyMODINIT_FUNC
PyInit__testinternalcapi(void)
{
    return PyModule_Create(&_testcapimodule);
}
