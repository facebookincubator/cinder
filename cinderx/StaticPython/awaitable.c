#include "cinderx/StaticPython/awaitable.h"

#include "structmember.h"

static int
awaitable_traverse(_PyClassLoader_Awaitable *self, visitproc visit, void *arg)
{
    Py_VISIT(self->state);
    Py_VISIT(self->coro);
    Py_VISIT(self->iter);
    return 0;
}

static int
awaitable_clear(_PyClassLoader_Awaitable *self)
{
    Py_CLEAR(self->state);
    Py_CLEAR(self->coro);
    Py_CLEAR(self->iter);
    return 0;
}

static void
awaitable_dealloc(_PyClassLoader_Awaitable *self)
{
    PyObject_GC_UnTrack((PyObject *)self);
    awaitable_clear(self);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
awaitable_get_iter(_PyClassLoader_Awaitable *self) {
    PyObject *iter = _PyCoro_GetAwaitableIter(self->coro);
    if (iter == NULL) {
        return NULL;
    }
    if (self->awaiter != NULL) {
       _PyAwaitable_SetAwaiter(iter, self->awaiter);
    }
    if (PyCoro_CheckExact(iter)) {
        PyObject *yf = _PyGen_yf((PyGenObject*)iter);
        if (yf != NULL) {
            Py_DECREF(yf);
            Py_DECREF(iter);
            PyErr_SetString(PyExc_RuntimeError,
                            "coroutine is being awaited already");
            return NULL;
        }
    }
    return iter;
}

static PyObject *
awaitable_await(_PyClassLoader_Awaitable *self)
{
    PyObject *iter = awaitable_get_iter(self);
    if (iter == NULL) {
        return NULL;
    }
    Py_XSETREF(self->iter, iter);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PySendResult
awaitable_itersend(_PyClassLoader_Awaitable *self,
                   PyObject *value,
                   PyObject **pResult)
{
    *pResult = NULL;

    PyObject *iter = self->iter;
    if (iter == NULL) {
        iter = awaitable_get_iter(self);
        if (iter == NULL) {
            return PYGEN_ERROR;
        }
        self->iter = iter;
    }

    if (self->onsend != NULL) {
        awaitable_presend send = self->onsend;
        self->onsend = NULL;
        if (send(self)) {
            *pResult = NULL;
            return PYGEN_ERROR;
        }
    }

    PyObject *result;

    PySendResult status = PyIter_Send(iter, value, &result);
    if (status == PYGEN_RETURN) {
        result = self->cb(self, result);
        if (result == NULL) {
            status = PYGEN_ERROR;
        }
    } else if (status == PYGEN_ERROR) {
        result = self->cb(self, NULL);
        if (result != NULL) {
            status = PYGEN_RETURN;
        }
    }

    *pResult = result;
    return status;
}

static void
awaitable_setawaiter(_PyClassLoader_Awaitable *awaitable, PyObject *awaiter) {
    if (awaitable->iter != NULL) {
        _PyAwaitable_SetAwaiter(awaitable->iter, awaiter);
    }
    awaitable->awaiter = awaiter;
}

static PyAsyncMethodsWithExtra awaitable_as_async = {
    .ame_async_methods = {
        (unaryfunc)awaitable_await,
        NULL,
        NULL,
        (sendfunc)awaitable_itersend,
    },
    .ame_setawaiter = (setawaiterfunc)awaitable_setawaiter,
};

static PyObject *
awaitable_send(_PyClassLoader_Awaitable *self, PyObject *value)
{
    PyObject *result;
    PySendResult status = awaitable_itersend(self, value, &result);
    if (status == PYGEN_ERROR || status == PYGEN_NEXT) {
        return result;
    }

    assert(status == PYGEN_RETURN);
    _PyGen_SetStopIterationValue(result);
    Py_DECREF(result);
    return NULL;
}

static PyObject *
awaitable_next(_PyClassLoader_Awaitable *self)
{
    return awaitable_send(self, Py_None);
}

static PyObject *
awaitable_throw(_PyClassLoader_Awaitable *self, PyObject *args)
{
    PyObject *iter = self->iter;
    if (iter == NULL) {
        iter = awaitable_get_iter(self);
        if (iter == NULL) {
            return NULL;
        }
        self->iter = iter;
    }
    _Py_IDENTIFIER(throw);
    PyObject *method = _PyObject_GetAttrId(iter, &PyId_throw);
    if (method == NULL) {
        return NULL;
    }
    PyObject *ret = PyObject_CallObject(method, args);
    Py_DECREF(method);
    if (ret != NULL) {
        return ret;
    } else if (_PyGen_FetchStopIterationValue(&ret) < 0) {
        /* Deliver exception result to callback */
        ret = self->cb(self, NULL);
        if (ret != NULL) {
            _PyGen_SetStopIterationValue(ret);
            Py_DECREF(ret);
            return NULL;
        }
        return ret;
    }

    ret = self->cb(self, ret);
    if (ret != NULL) {
        _PyGen_SetStopIterationValue(ret);
        Py_DECREF(ret);
    }
    return NULL;
}

static PyObject *
awaitable_close(_PyClassLoader_Awaitable *self, PyObject *val)
{
    PyObject *iter = self->iter;
    if (iter == NULL) {
        iter = awaitable_get_iter(self);
        if (iter == NULL) {
            return NULL;
        }
        self->iter = iter;
    }
    _Py_IDENTIFIER(close);
    PyObject *ret = _PyObject_CallMethodIdObjArgs(iter, &PyId_close, val, NULL);
    Py_CLEAR(self->iter);
    return ret;
}

static PyMethodDef awaitable_methods[] = {
    {"send",  (PyCFunction)awaitable_send, METH_O, NULL},
    {"throw", (PyCFunction)awaitable_throw, METH_VARARGS, NULL},
    {"close", (PyCFunction)awaitable_close, METH_NOARGS, NULL},
    {NULL, NULL},
};

static PyMemberDef awaitable_memberlist[] = {
    {"__coro__", T_OBJECT, offsetof(_PyClassLoader_Awaitable, coro), READONLY},
    {NULL}  /* Sentinel */
};


static PyTypeObject _PyClassLoader_AwaitableType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0).tp_name = "awaitable_wrapper",
    sizeof(_PyClassLoader_Awaitable),
    0,
    .tp_dealloc = (destructor)awaitable_dealloc,
    .tp_as_async = (PyAsyncMethods *)&awaitable_as_async,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_AM_EXTRA,
    .tp_traverse = (traverseproc)awaitable_traverse,
    .tp_clear = (inquiry)awaitable_clear,
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc)awaitable_next,
    .tp_methods = awaitable_methods,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
    .tp_members = awaitable_memberlist,
};

PyObject *
_PyClassLoader_NewAwaitableWrapper(PyObject *coro, int eager, PyObject *state, awaitable_cb cb, awaitable_presend onsend) {
    if (PyType_Ready(&_PyClassLoader_AwaitableType) < 0) {
        return NULL;
    }
    _PyClassLoader_Awaitable *awaitable =
        PyObject_GC_New(_PyClassLoader_Awaitable,
                        &_PyClassLoader_AwaitableType);


    Py_INCREF(state);
    awaitable->state = state;
    awaitable->cb = cb;
    awaitable->onsend = onsend;
    awaitable->awaiter = NULL;

    if (eager) {
        Ci_PyWaitHandleObject *handle = (Ci_PyWaitHandleObject *)coro;
        Py_INCREF(handle->wh_coro_or_result);
        awaitable->coro = handle->wh_coro_or_result;
        awaitable->iter = handle->wh_coro_or_result;
        handle->wh_coro_or_result = (PyObject *)awaitable;
        return coro;
    }

    awaitable->coro = coro;
    awaitable->iter = NULL;
    return (PyObject *)awaitable;
}
