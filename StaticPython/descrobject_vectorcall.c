/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */

#include "Python.h"
#include "cinderhooks.h"
#include "pycore_pystate.h"       // _PyThreadState_GET()

#include "StaticPython/classloader.h"
#include "StaticPython/descrobject_vectorcall.h"

static inline int
Ci_method_check_args(PyObject *func, PyObject *const *args, Py_ssize_t nargs, size_t nargsf, PyObject *kwnames)
{
    assert(!PyErr_Occurred());
    assert(PyObject_TypeCheck(func, &PyMethodDescr_Type));
    if (nargs < 1) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "descriptor '%U' of '%.100s' "
                         "object needs an argument",
                         funcstr, PyDescr_TYPE(func)->tp_name);
        }
        return -1;
    }
    PyObject *self = args[0];
    if (!_PyObject_RealIsSubclass((PyObject *)Py_TYPE(self),
                                  (PyObject *)PyDescr_TYPE(func)))
    {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "descriptor '%U' for '%.100s' objects "
                         "doesn't apply to a '%.100s' object",
                         funcstr, PyDescr_TYPE(func)->tp_name,
                         Py_TYPE(self)->tp_name);
        }
        return -1;
    }
    if (kwnames && PyTuple_GET_SIZE(kwnames)) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%.200s() takes no keyword arguments", funcstr);
        }
        return -1;
    }
    return 0;
}


typedef void *(*call_self_0)(PyObject *self);
typedef void *(*call_self_1)(PyObject *self, void *);
typedef void *(*call_self_2)(PyObject *self, void *, void *);


PyObject *
Ci_method_vectorcall_typed_0(PyObject *func,
                          PyObject *const *args,
                          size_t nargsf,
                          PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (Ci_method_check_args(func, args, nargs, nargsf, kwnames)) {
        return NULL;
    }
    if (nargs != 1) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%.200s() takes exactly one argument (%zd given)",
                         funcstr,
                         nargs - 1);
            }
        return NULL;
    }

    PyThreadState *tstate = _PyThreadState_GET();
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)Cix_method_enter_call(tstate, func);
    if (def == NULL) {
        return NULL;
    }

    PyObject *self = args[0];
    void *res = ((call_self_0)def->tmd_meth)(self);
    res = _PyClassLoader_ConvertRet(res, def->tmd_ret);

    Py_LeaveRecursiveCall();
    return (PyObject *)res;
}

#define CONV_ARGS(n)                                                          \
    void *final_args[n];                                                      \
    for (Py_ssize_t i = 0; i < n; i++) {                                      \
        final_args[i] =                                                       \
            _PyClassLoader_ConvertArg(self, def->tmd_sig[i], i + 1, nargsf, args, \
                                      &error);                                \
        if (error) {                                                          \
            if (!PyErr_Occurred()) {                                          \
                PyObject *funcstr = _PyObject_FunctionStr(func);              \
                if (funcstr != NULL) {                                        \
                    _PyClassLoader_ArgError(funcstr, i + 1, i,                \
                                            def->tmd_sig[i],  self);          \
                }                                                             \
            }                                                                 \
            goto done;                                                        \
        }                                                                     \
    }

PyObject *
Ci_method_vectorcall_typed_1(PyObject *func,
                          PyObject *const *args,
                          size_t nargsf,
                          PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (Ci_method_check_args(func, args, nargs, nargsf, kwnames)) {
        return NULL;
    } else if (nargs > 2) {

        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                            "%.200s() takes at most 1 argument, got %zd",
                            funcstr,
                            nargs - 1);
        }
        return NULL;
    }

    PyThreadState *tstate = _PyThreadState_GET();
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)Cix_method_enter_call(tstate, func);
    if (def == NULL) {
        return NULL;
    }

    PyObject *self = args[0];
    int error = 0;
    void *res = NULL;
    CONV_ARGS(1);

    res = ((call_self_1)def->tmd_meth)(self, final_args[0]);
    res = _PyClassLoader_ConvertRet(res, def->tmd_ret);

done:
    Py_LeaveRecursiveCall();
    return (PyObject *)res;
}

PyObject *
Ci_method_vectorcall_typed_2(PyObject *func,
                          PyObject *const *args,
                          size_t nargsf,
                          PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

    if (Ci_method_check_args(func, args, nargs, nargsf, kwnames)) {
        return NULL;
    } else if (nargs > 3) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                            "%.200s() expected at most 2 arguments, got %zd",
                            funcstr,
                            nargs - 1);
        }
        return NULL;
    }

    PyThreadState *tstate = _PyThreadState_GET();
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)Cix_method_enter_call(tstate, func);
    if (def == NULL) {
        return NULL;
    }

    PyObject *self = args[0];
    int error = 0;
    void *res = NULL;
    CONV_ARGS(2);


    res = ((call_self_2)def->tmd_meth)(self, final_args[0], final_args[1]);
    res = _PyClassLoader_ConvertRet(res, def->tmd_ret);

done:
    Py_LeaveRecursiveCall();
    return (PyObject *)res;
}

