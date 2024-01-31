/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include "Python.h"
#include "cinder/hooks.h"
#include "pycore_pystate.h" // _PyThreadState_GET()

#include "cinderx/StaticPython/classloader.h"
#include "cinderx/StaticPython/methodobject_vectorcall.h"

typedef void *(*call_self_0)(PyObject *self);
typedef void *(*call_self_1)(PyObject *self, void *);
typedef void *(*call_self_2)(PyObject *self, void *, void *);

PyObject *
Ci_cfunction_vectorcall_typed_0(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames)
{

    PyThreadState *tstate = _PyThreadState_GET();
    if (Cix_cfunction_check_kwargs(tstate, func, kwnames)) {
        return NULL;
    }

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 0) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%U() takes exactly one argument (%zd given)",
                         funcstr,
                         nargs);
        }
        return NULL;
    }

    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)Cix_cfunction_enter_call(tstate, func);
    if (def == NULL) {
        return NULL;
    }
    PyObject *self = PyCFunction_GET_SELF(func);

    void *res = ((call_self_0)def->tmd_meth)(self);
    res = _PyClassLoader_ConvertRet(res, def->tmd_ret);

    Py_LeaveRecursiveCall();
    return (PyObject *)res;
}

#define CONV_ARGS(n)                                                          \
    void *final_args[n];                                                      \
    for (Py_ssize_t i = 0; i < n; i++) {                                      \
        final_args[i] =                                                       \
            _PyClassLoader_ConvertArg(self, def->tmd_sig[i], i, nargsf, args, \
                                      &error);                                \
        if (error) {                                                          \
            if (!PyErr_Occurred()) {                                          \
                PyObject *funcstr = _PyObject_FunctionStr(func);              \
                if (funcstr != NULL) {                                        \
                    _PyClassLoader_ArgError(funcstr, i, i,                    \
                                            def->tmd_sig[i],  self);          \
                }                                                             \
            }                                                                 \
            goto done;                                                        \
        }                                                                     \
    }

PyObject *
Ci_cfunction_vectorcall_typed_1(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%U() takes exactly one argument (%zd given)",
                         funcstr,
                         nargs);
        }
        return NULL;
    }

    PyThreadState *tstate = _PyThreadState_GET();
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)Cix_cfunction_enter_call(tstate, func);
    if (def == NULL) {
        return NULL;
    }
    PyObject *self = PyCFunction_GET_SELF(func);

    int error = 0;
    void *res = NULL;
    CONV_ARGS(1)

    res = ((call_self_1)def->tmd_meth)(self, final_args[0]);
    res = _PyClassLoader_ConvertRet(res, def->tmd_ret);

done:
    Py_LeaveRecursiveCall();
    return (PyObject *)res;
}

PyObject *
Ci_cfunction_vectorcall_typed_2(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 2) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%U() takes exactly 2 argument s(%zd given)",
                         funcstr,
                         nargs);
        }
        return NULL;
    }

    PyThreadState *tstate = _PyThreadState_GET();
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)Cix_cfunction_enter_call(tstate, func);
    if (def == NULL) {
        return NULL;
    }
    PyObject *self = PyCFunction_GET_SELF(func);

    int error = 0;
    void *res = NULL;
    CONV_ARGS(2)

    res = ((call_self_2)def->tmd_meth)(self, final_args[0], final_args[1]);
    res = _PyClassLoader_ConvertRet(res, def->tmd_ret);

done:
    Py_LeaveRecursiveCall();
    return (PyObject *)res;
}

vectorcallfunc
Ci_PyCMethod_New_METH_TYPED(PyMethodDef *method)
{
    if ((method->ml_flags & Ci_METH_TYPED) != Ci_METH_TYPED) {
        return NULL;
    }

    vectorcallfunc vectorcall;
    Ci_PyTypedMethodDef *sig = (Ci_PyTypedMethodDef *)method->ml_meth;
    Py_ssize_t arg_cnt = 0;
    while (sig->tmd_sig[arg_cnt] != NULL) {
        arg_cnt++;
    }
    switch (arg_cnt) {
        case 0: vectorcall = Ci_cfunction_vectorcall_typed_0; break;
        case 1: vectorcall = Ci_cfunction_vectorcall_typed_1; break;
        case 2: vectorcall = Ci_cfunction_vectorcall_typed_2; break;
        default:
            PyErr_Format(PyExc_SystemError,
                        "%s() method: unsupported argument count", method->ml_name, arg_cnt);
            return NULL;
    }
    return vectorcall;
}

PyObject *
Ci_meth_get__typed_signature__(PyCFunctionObject *m, void *closure)
{
    return Ci_PyMethodDef_GetTypedSignature(m->m_ml);
}
