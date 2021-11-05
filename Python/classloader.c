/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#include "Python.h"
#include "classloader.h"
#include "opcode.h"
#include "structmember.h"
#include "Jit/pyjit.h"
#include "pycore_object.h"  // PyHeapType_CINDER_EXTRA
#include "pycore_tupleobject.h" // _PyTuple_FromArray
#include "pycore_unionobject.h" // _Py_Union()

static PyObject *classloader_cache;
static PyObject *genericinst_cache;

static void
vtabledealloc(_PyType_VTable *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->vt_slotmap);
    Py_XDECREF(op->vt_thunks);
    Py_XDECREF(op->vt_original);

    for (Py_ssize_t i = 0; i < op->vt_size; i++) {
        Py_XDECREF(op->vt_entries[i].vte_state);
    }
    PyObject_GC_Del((PyObject *)op);
}

static int
vtabletraverse(_PyType_VTable *op, visitproc visit, void *arg)
{
    for (Py_ssize_t i = 0; i < op->vt_size; i++) {
        Py_VISIT(op->vt_entries[i].vte_state);
    }
    Py_VISIT(op->vt_original);
    Py_VISIT(op->vt_thunks);
    return 0;
}

static int
vtableclear(_PyType_VTable *op)
{
    for (Py_ssize_t i = 0; i < op->vt_size; i++) {
        Py_CLEAR(op->vt_entries[i].vte_state);
    }
    Py_CLEAR(op->vt_original);
    Py_CLEAR(op->vt_thunks);
    return 0;
}

PyTypeObject _PyType_VTableType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "vtable",
    sizeof(_PyType_VTable) - sizeof(_PyType_VTableEntry),
    sizeof(_PyType_VTableEntry),
    .tp_dealloc = (destructor)vtabledealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
                Py_TPFLAGS_TUPLE_SUBCLASS, /* tp_flags */
    .tp_traverse = (traverseproc)vtabletraverse,
    .tp_clear = (inquiry)vtableclear,
};

typedef struct {
    _PyClassLoader_TypeCheckState thunk_tcs;
    /* the class that the thunk exists for (used for error reporting) */
    PyTypeObject *thunk_cls;
    /* 1 if the the original function is an async function */
    int thunk_coroutine;
    /* 1 if the the original function is a classmethod */
    int thunk_classmethod;
    /* a pointer which can be used for an indirection in *PyClassLoader_GetIndirectPtr.
     * This will be the current value of the function when it's not patched and will
     * be the thunk when it is. */
    PyObject *thunk_funcref; /* borrowed */
    /* the vectorcall entry point for the thunk */
    vectorcallfunc thunk_vectorcall;
} _Py_StaticThunk;

static int
awaitable_traverse(_PyClassLoader_Awaitable *self, visitproc visit, void *arg)
{
    Py_VISIT(self->retinfo);
    Py_VISIT(self->coro);
    Py_VISIT(self->iter);
    return 0;
}

static int
awaitable_clear(_PyClassLoader_Awaitable *self)
{
    Py_CLEAR(self->retinfo);
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

static PyObject *
rettype_check(PyTypeObject *cls, PyObject *ret, _PyClassLoader_RetTypeInfo *rt_info);


int
used_in_vtable(PyObject *value);

static PySendResult
awaitable_itersend(PyThreadState* tstate,
                   _PyClassLoader_Awaitable *self,
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

    PyObject *result;
    PySendResult status = PyIter_Send(tstate, iter, value, &result);
    if (status == PYGEN_RETURN) {
        result = rettype_check(Py_TYPE(self), result, self->retinfo);
        if (result == NULL) {
            status = PYGEN_ERROR;
        }
    }

    *pResult = result;
    return status;
}

static PyAsyncMethodsWithExtra awaitable_as_async = {
    .ame_async_methods = {
        (unaryfunc)awaitable_await,
        NULL,
        NULL,
    },
    .ame_send = (sendfunc)awaitable_itersend,
};

static PyObject *
awaitable_send(_PyClassLoader_Awaitable *self, PyObject *value)
{
    PyObject *result;
    PySendResult status = awaitable_itersend(PyThreadState_GET(), self, value, &result);
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

extern int _PyObject_GetMethod(PyObject *, PyObject *, PyObject **);

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
    if (ret != NULL || _PyGen_FetchStopIterationValue(&ret) < 0) {
        return ret;
    }
    return rettype_check(Py_TYPE(self), ret, self->retinfo);
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
};

static int
rettype_check_traverse(_PyClassLoader_RetTypeInfo *op, visitproc visit, void *arg)
{
    visit((PyObject *)op->rt_expected, arg);
    return 0;
}

static int
rettype_check_clear(_PyClassLoader_RetTypeInfo *op)
{
    Py_CLEAR(op->rt_expected);
    Py_CLEAR(op->rt_name);
    return 0;
}

PyObject *
classloader_get_func_name(PyObject *name);

static PyObject *
rettype_check(PyTypeObject *cls, PyObject *ret, _PyClassLoader_RetTypeInfo *rt_info)
{
    if (ret == NULL) {
        return NULL;
    }

    int type_code = _PyClassLoader_GetTypeCode(rt_info->rt_expected);
    int overflow = 0;
    if (type_code != TYPED_OBJECT) {
        size_t int_val;
        switch (type_code) {
            case TYPED_BOOL:
                if (PyBool_Check(ret)) {
                    return ret;
                }
                break;
            case TYPED_INT8:
            case TYPED_INT16:
            case TYPED_INT32:
            case TYPED_INT64:
            case TYPED_UINT8:
            case TYPED_UINT16:
            case TYPED_UINT32:
            case TYPED_UINT64:
                if (PyLong_Check(ret)) {
                    if (_PyClassLoader_OverflowCheck(ret, type_code, &int_val)) {
                        return ret;
                    }
                    overflow = 1;
                }
                break;
            default:
                PyErr_SetString(PyExc_RuntimeError, "unsupported primitive return type");
                Py_DECREF(ret);
                return NULL;
        }
    }

    if (overflow || !((rt_info->rt_optional && ret == Py_None) ||
                         _PyObject_RealIsInstance(ret, (PyObject *)rt_info->rt_expected))) {
        /* The override returned an incompatible value, report error */
        const char *msg;
        PyObject *exc_type = PyExc_TypeError;
        if (overflow) {
            exc_type = PyExc_OverflowError;
            msg = "unexpected return type from %s.%U, expected %s, got out-of-range %s (%R)";
        } else if (rt_info->rt_optional) {
            msg = "unexpected return type from %s.%U, expected  Optional[%s], "
                  "got %s";
        } else {
            msg = "unexpected return type from %s.%U, expected %s, got %s";
        }

        PyErr_Format(exc_type,
                     msg,
                     cls->tp_name,
                     classloader_get_func_name(rt_info->rt_name),
                     rt_info->rt_expected->tp_name,
                     Py_TYPE(ret)->tp_name,
                     ret);

        Py_DECREF(ret);
        return NULL;
    }
    return ret;
}

static PyObject *
type_vtable_coroutine(_PyClassLoader_TypeCheckState *state,
                       PyObject *const *args,
                       size_t nargsf,
                       PyObject *kwnames)
{
    PyObject *coro;
    PyObject *callable = state->tcs_value;
    if (Py_TYPE(callable) == &PyClassMethod_Type) {
        // We need to do some special set up for class methods when invoking.
        callable = _PyClassMethod_GetFunc(state->tcs_value);
        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
        assert(nargs > 0);
        PyObject *classmethod_args[nargs];
        classmethod_args[0] = (PyObject *) Py_TYPE(args[0]);
        for (Py_ssize_t i = 1; i < nargs; ++i) {
          classmethod_args[i] = args[i];
        }
        args = classmethod_args;
        coro = _PyObject_Vectorcall(callable, args, nargsf, kwnames);
    } else if (nargsf & _Py_VECTORCALL_INVOKED_CLASSMETHOD) {
        // In this case, we have a patched class method, and the self has been
        // handled via descriptors already.
      coro = _PyObject_Vectorcall(callable,
                                  args + 1,
                                  (PyVectorcall_NARGS(nargsf) - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                  kwnames);
    } else {
        coro = _PyObject_Vectorcall(callable, args, nargsf, kwnames);
    }
    if (coro == NULL) {
        return NULL;
    }

    int eager = _PyWaitHandle_CheckExact(coro);
    if (eager) {
        PyWaitHandleObject *handle = (PyWaitHandleObject *)coro;
        if (handle->wh_waiter == NULL) {
            if (rettype_check(Py_TYPE(callable),
                    handle->wh_coro_or_result, (_PyClassLoader_RetTypeInfo *)state)) {
                return coro;
            }
            _PyWaitHandle_Release(coro);
            return NULL;
        }
    }

    if (PyType_Ready(&_PyClassLoader_AwaitableType) < 0) {
        return NULL;
    }
    _PyClassLoader_Awaitable *awaitable =
        PyObject_GC_New(_PyClassLoader_Awaitable,
                        &_PyClassLoader_AwaitableType);
    if (awaitable == NULL) {
        return NULL;
    }

    Py_INCREF(state);
    awaitable->retinfo = (_PyClassLoader_RetTypeInfo *)state;

    if (eager) {
        PyWaitHandleObject *handle = (PyWaitHandleObject *)coro;
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

static PyObject *
type_vtable_nonfunc(_PyClassLoader_TypeCheckState *state,
                    PyObject **args,
                    size_t nargsf,
                    PyObject *kwnames)
{

    PyObject *self = args[0];
    PyObject *descr = state->tcs_value;
    PyObject *name = state->tcs_rt.rt_name;
    PyObject *res;
    /* we have to perform the descriptor checks at runtime because the
     * descriptor type can be modified preventing us from being able to have
     * more optimized fast paths */
    if (!PyDescr_IsData(descr)) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr != NULL) {
            PyObject *dict = *dictptr;
            if (dict != NULL) {
                PyObject *value = PyDict_GetItem(dict, name);
                if (value != NULL) {
                    /* descriptor was overridden by instance value */
                    res = _PyObject_Vectorcall(value, args, nargsf, kwnames);
                    goto done;
                }
            }
        }
    }

    if (Py_TYPE(descr)->tp_descr_get != NULL) {
        PyObject *self = args[0];
        PyObject *get = Py_TYPE(descr)->tp_descr_get(
            descr, self, (PyObject *)Py_TYPE(self));
        if (get == NULL) {
            return NULL;
        }

        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

        res =
            _PyObject_Vectorcall(get,
                                 args + 1,
                                 (nargs - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                 kwnames);
        Py_DECREF(get);
        goto done;
    }
    res = _PyObject_Vectorcall(descr, args, nargsf, kwnames);
done:
    return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
}

static PyObject *
type_vtable_func_overridable(_PyClassLoader_TypeCheckState *state,
                             PyObject **args,
                             size_t nargsf,
                             PyObject *kwnames)
{
    PyObject *self = args[0];
    PyObject **dictptr = _PyObject_GetDictPtr(self);
    PyObject *dict = dictptr != NULL ? *dictptr : NULL;
    PyObject *res;
    if (dict != NULL) {
        /* ideally types using INVOKE_METHOD are defined w/o out dictionaries,
         * which allows us to avoid this lookup.  If they're not then we'll
         * fallback to supporting looking in the dictionary */
        PyObject *name = state->tcs_rt.rt_name;
        PyObject *callable = PyDict_GetItem(dict, name);
        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
        if (callable != NULL) {
            res = _PyObject_Vectorcall(callable,
                                       args + 1,
                                       (nargs - 1) |
                                           PY_VECTORCALL_ARGUMENTS_OFFSET,
                                       kwnames);
            goto done;
        }
    }

    PyFunctionObject *func = (PyFunctionObject *)state->tcs_value;
    res = func->vectorcall((PyObject *)func, args, nargsf, kwnames);

done:
    return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
}

PyObject *_PyFunction_CallStatic(PyFunctionObject *func,
                                 PyObject **args,
                                 Py_ssize_t nargsf,
                                 PyObject *kwnames);
PyObject *_PyEntry_StaticEntry(PyFunctionObject *func,
                               PyObject **args,
                               Py_ssize_t nargsf,
                               PyObject *kwnames);
PyObject *_PyEntry_StaticEntryNArgs(PyFunctionObject *func,
                                    PyObject **args,
                                    Py_ssize_t nargsf,
                                    PyObject *kwnames);
PyObject *_PyEntry_StaticEntryP0Defaults(PyFunctionObject *func,
                                         PyObject **args,
                                         Py_ssize_t nargsf,
                                         PyObject *kwnames);

static inline int
is_static_entry(vectorcallfunc func)
{
    return func == (vectorcallfunc)_PyEntry_StaticEntry ||
           func == (vectorcallfunc)_PyEntry_StaticEntryNArgs ||
           func == (vectorcallfunc)_PyEntry_StaticEntryP0Defaults;
}

static PyObject *
type_vtable_func_lazyinit(PyTupleObject *state,
                          PyObject **stack,
                          size_t nargsf,
                          PyObject *kwnames)
{
    /* func is (vtable, index, function) */
    _PyType_VTable *vtable = (_PyType_VTable *)PyTuple_GET_ITEM(state, 0);
    long index = PyLong_AS_LONG(PyTuple_GET_ITEM(state, 1));
    PyFunctionObject *func = (PyFunctionObject *)PyTuple_GET_ITEM(state, 2);

    PyObject *res = func->vectorcall((PyObject *)func, stack, nargsf, kwnames);
    if (vtable->vt_entries[index].vte_entry ==
        (vectorcallfunc)type_vtable_func_lazyinit) {
        /* We could have already updated this on a recursive call */
        if (vtable->vt_entries[index].vte_state == (PyObject *)state) {
            vtable->vt_entries[index].vte_state = (PyObject *)func;
            if (is_static_entry(func->vectorcall)) {
                /* this will always be invoked statically via the v-table */
                vtable->vt_entries[index].vte_entry =
                    (vectorcallfunc)_PyFunction_CallStatic;
            } else {
                vtable->vt_entries[index].vte_entry = func->vectorcall;
            }
            Py_INCREF(func);
            Py_DECREF(state);
        }
    }

    return res;
}

static PyObject *
type_vtable_staticmethod(PyObject *state,
                          PyObject *const *stack,
                          size_t nargsf,
                          PyObject *kwnames)
{
    /* func is (vtable, index, function) */
    PyObject *func = _PyStaticMethod_GetFunc(state);
    return _PyObject_Vectorcall(func, stack + 1, nargsf - 1, kwnames);
}

static PyObject *
type_vtable_classmethod(PyObject *state,
                        PyObject *const *stack,
                        size_t nargsf,
                        PyObject *kwnames)
{
    PyObject *func = _PyClassMethod_GetFunc(state);
    return _PyObject_Vectorcall(func, stack, nargsf, kwnames);
}

static PyObject *
type_vtable_classmethod_overridable(_PyClassLoader_TypeCheckState *state,
                                    PyObject **args,
                                    size_t nargsf,
                                    PyObject *kwnames)
{
    if (nargsf & _Py_VECTORCALL_INVOKED_CLASSMETHOD && PyClassMethod_Check(state->tcs_value)) {
        PyFunctionObject *func = (PyFunctionObject *)_PyClassMethod_GetFunc(state->tcs_value);
        return func->vectorcall((PyObject *)func, args, nargsf, kwnames);
    }
    // Invoked via an instance, we need to check its dict to see if the classmethod was
    // overridden.
    PyObject *self = args[0];
    PyObject **dictptr = _PyObject_GetDictPtr(self);
    PyObject *dict = dictptr != NULL ? *dictptr : NULL;
    PyObject *res;
    if (dict != NULL) {
        /* ideally types using INVOKE_METHOD are defined w/o out dictionaries,
         * which allows us to avoid this lookup.  If they're not then we'll
         * fallback to supporting looking in the dictionary */
        PyObject *name = state->tcs_rt.rt_name;
        PyObject *callable = PyDict_GetItem(dict, name);
        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
        if (callable != NULL) {
            res = _PyObject_Vectorcall(callable,
                                       args + 1,
                                       (nargs - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                       kwnames);
            return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
        }
    }

    PyFunctionObject *func = (PyFunctionObject *)_PyClassMethod_GetFunc(state->tcs_value);
    return func->vectorcall((PyObject *)func, args, nargsf, kwnames);
}

static PyObject *
type_vtable_func_missing(PyObject *state, PyObject **args, Py_ssize_t nargs)
{
    PyObject *self = args[0];
    PyObject *name = PyTuple_GET_ITEM(state, 0);

    PyErr_Format(PyExc_AttributeError,
                 "'%s' object has no attribute %R",
                 Py_TYPE(self)->tp_name,
                 name);
    return NULL;
}

static int
type_vtable_set_opt_slot(PyTypeObject *tp,
                         PyObject *name,
                         _PyType_VTable *vtable,
                         Py_ssize_t slot,
                         PyObject *value)
{
    vectorcallfunc entry = ((PyFunctionObject *)value)->vectorcall;
    if (entry == (vectorcallfunc)PyEntry_LazyInit) {
        /* entry point isn't initialized yet, we want to run it once, and
         * then update our own entry point */
        PyObject *state = PyTuple_New(3);
        if (state == NULL) {
            return -1;
        }
        PyTuple_SET_ITEM(state, 0, (PyObject *)vtable);
        Py_INCREF(vtable);
        PyObject *new_index = PyLong_FromSize_t(slot);
        if (new_index == NULL) {
            Py_DECREF(state);
            return -1;
        }
        PyTuple_SET_ITEM(state, 1, new_index);
        PyTuple_SET_ITEM(state, 2, value);
        Py_INCREF(value);
        Py_XDECREF(vtable->vt_entries[slot].vte_state);
        vtable->vt_entries[slot].vte_state = state;
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_func_lazyinit;
    } else {
        Py_XDECREF(vtable->vt_entries[slot].vte_state);
        vtable->vt_entries[slot].vte_state = value;
        if (is_static_entry(entry)) {
            /* this will always be invoked statically via the v-table */
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)_PyFunction_CallStatic;
        } else {
            vtable->vt_entries[slot].vte_entry = entry;
        }
        Py_INCREF(value);
    }
    return 0;
}

static PyObject *
thunk_call(_Py_StaticThunk *thunk, PyObject *args, PyObject *kwds);

typedef struct {
    PyObject_HEAD;
    PyObject *propthunk_target;
    /* the vectorcall entry point for the thunk */
    vectorcallfunc propthunk_vectorcall;
} _Py_PropertyThunk;



static int
propthunktraverse(_Py_PropertyThunk *op, visitproc visit, void *arg)
{
    visit(op->propthunk_target, arg);
    return 0;
}

static int
propthunkclear(_Py_PropertyThunk *op)
{
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_CLEAR(op->propthunk_target);
    return 0;
}

static void
propthunkdealloc(_Py_PropertyThunk *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->propthunk_target);
    PyObject_GC_Del((PyObject *)op);
}

static PyObject *
propthunk_get(_Py_PropertyThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames)
{
    size_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "property get expected 1 argument");
        return NULL;
    }

    descrgetfunc f = Py_TYPE(thunk->propthunk_target)->tp_descr_get;
    if (f == NULL) {
        Py_INCREF(thunk->propthunk_target);
        return thunk->propthunk_target;
    }

    PyObject *res = f(thunk->propthunk_target, args[0], (PyObject *)(Py_TYPE(args[0])));
    return res;
}

static PyObject *
propthunk_set(_Py_PropertyThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames)
{
    size_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "property set expected 1 argument");
        return NULL;
    }

    descrsetfunc f = Py_TYPE(thunk->propthunk_target)->tp_descr_set;
    if (f == NULL) {
        PyErr_Format(PyExc_TypeError,
            "'%s' doesn't support __set__", Py_TYPE(thunk->propthunk_target)->tp_name);
        return NULL;
    }
    if (f(thunk->propthunk_target, args[0], args[1])) {
        return NULL;
    }
    Py_RETURN_NONE;
}

PyTypeObject _PyType_PropertyThunk = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "property_thunk",
    sizeof(_Py_PropertyThunk),
    .tp_dealloc = (destructor)propthunkdealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
        _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_traverse = (traverseproc)propthunktraverse,
    .tp_clear = (inquiry)propthunkclear,
    .tp_vectorcall_offset = offsetof(_Py_PropertyThunk, propthunk_vectorcall),
    .tp_call = (ternaryfunc)thunk_call,
};

static PyObject *g_missing_fget = NULL;
static PyObject *g_missing_fset = NULL;

static PyObject *
classloader_get_property_missing_fget() {
    if (g_missing_fget == NULL) {
        PyObject *mod = PyImport_ImportModule("_static");
        if (mod == NULL) {
            return NULL;
        }
        PyObject *func = PyObject_GetAttrString(mod, "_property_missing_fget");
        Py_DECREF(mod);
        if (func == NULL) {
            return NULL;
        }
        g_missing_fget = func;
    }
    return g_missing_fget;
}

static PyObject *
classloader_get_property_missing_fset() {
    if (g_missing_fset == NULL) {
        PyObject *mod = PyImport_ImportModule("_static");
        if (mod == NULL) {
            return NULL;
        }
        PyObject *func = PyObject_GetAttrString(mod, "_property_missing_fset");
        Py_DECREF(mod);
        if (func == NULL) {
            return NULL;
        }
        g_missing_fset = func;
    }
    return g_missing_fset;
}

static PyObject *
classloader_get_property_fget(PyObject *property) {
    if (Py_TYPE(property) == &PyProperty_Type) {
        PyObject *func = ((propertyobject *)property)->prop_get;
        if (func == NULL) {
            func = classloader_get_property_missing_fget();
        }
        Py_XINCREF(func);
        return func;
    } else {
        _Py_PropertyThunk *thunk = PyObject_GC_New(_Py_PropertyThunk, &_PyType_PropertyThunk);
        if (thunk == NULL) {
            return NULL;
        }
        thunk->propthunk_vectorcall = (vectorcallfunc)propthunk_get;
        thunk->propthunk_target = property;
        Py_INCREF(property);
        return (PyObject *)thunk;
    }
}

static PyObject *
classloader_get_property_fset(PyObject *property) {
    if (Py_TYPE(property) == &PyProperty_Type) {
        PyObject *func = ((propertyobject *)property)->prop_set;
        if (func == NULL) {
            func = classloader_get_property_missing_fset();
        }
        Py_XINCREF(func);
        return func;
    } else {
        _Py_PropertyThunk *thunk = PyObject_GC_New(_Py_PropertyThunk, &_PyType_PropertyThunk);
        if (thunk == NULL) {
            return NULL;
        }
        thunk->propthunk_vectorcall = (vectorcallfunc)propthunk_set;
        thunk->propthunk_target = property;
        Py_INCREF(property);
        return (PyObject *)thunk;
    }
}

static PyObject *
classloader_get_property_method(PyObject *property, PyTupleObject *name)
{
    PyObject *fname = PyTuple_GET_ITEM(name, 1);
    if (_PyUnicode_EqualToASCIIString(fname, "fget")) {
        return classloader_get_property_fget(property);
    } else if (_PyUnicode_EqualToASCIIString(fname, "fset")) {
        return classloader_get_property_fset(property);
    }
    PyErr_Format(PyExc_RuntimeError, "bad property method name %R in classloader", fname);
    return NULL;
}

static int
classloader_is_property_tuple(PyTupleObject *name)
{
    if (PyTuple_GET_SIZE(name) != 2) {
        return 0;
    }
    PyObject *property_method_name = PyTuple_GET_ITEM(name, 1);
    if (!PyUnicode_Check(property_method_name)) {
        return 0;
    }
    return _PyUnicode_EqualToASCIIString(property_method_name, "fget")
      || _PyUnicode_EqualToASCIIString(property_method_name, "fset");
}

PyObject *
classloader_get_func_name(PyObject *name) {
    if (PyTuple_Check(name) &&
        classloader_is_property_tuple((PyTupleObject *)name)) {
        return PyTuple_GET_ITEM(name, 0);
    }
    return name;
}

PyTypeObject *
resolve_function_rettype(PyObject *funcobj,
                         int *optional,
                         int *coroutine) {
    assert(PyFunction_Check(funcobj));
    PyFunctionObject *func = (PyFunctionObject *)funcobj;
    if (((PyCodeObject *)func->func_code)->co_flags & CO_COROUTINE) {
        *coroutine = 1;
    }
    return _PyClassLoader_ResolveType(_PyClassLoader_GetReturnTypeDescr(func),
                                      optional);
}

PyObject *
_PyClassLoader_GetReturnTypeDescr(PyFunctionObject *func)
{
    return _PyClassLoader_GetCodeReturnTypeDescr(
        (PyCodeObject *)func->func_code);
}

PyObject *
_PyClassLoader_GetCodeReturnTypeDescr(PyCodeObject* code)
{
    return PyTuple_GET_ITEM(
        code->co_consts, PyTuple_GET_SIZE(code->co_consts) - 1);
}

static int
_PyClassLoader_TypeCheckState_traverse(_PyClassLoader_TypeCheckState *op, visitproc visit, void *arg)
{
    rettype_check_traverse((_PyClassLoader_RetTypeInfo *)op, visit, arg);
    visit(op->tcs_value, arg);
    return 0;
}

static int
_PyClassLoader_TypeCheckState_clear(_PyClassLoader_TypeCheckState *op)
{
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_CLEAR(op->tcs_value);
    return 0;
}

static void
_PyClassLoader_TypeCheckState_dealloc(_PyClassLoader_TypeCheckState *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_XDECREF(op->tcs_value);
    PyObject_GC_Del((PyObject *)op);
}

PyTypeObject _PyType_TypeCheckState = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "vtable_state_obj",
    sizeof(_PyClassLoader_TypeCheckState),
    .tp_dealloc = (destructor)_PyClassLoader_TypeCheckState_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
        _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_traverse = (traverseproc)_PyClassLoader_TypeCheckState_traverse,
    .tp_clear = (inquiry)_PyClassLoader_TypeCheckState_clear,
};

static int
type_vtable_setslot_typecheck(PyObject *ret_type,
                              int optional,
                              int coroutine,
                              int classmethod,
                              PyObject *name,
                              _PyType_VTable *vtable,
                              Py_ssize_t slot,
                              PyObject *value)
{
    _PyClassLoader_TypeCheckState *state = PyObject_GC_New(
        _PyClassLoader_TypeCheckState, &_PyType_TypeCheckState);
    if (state == NULL) {
        Py_XDECREF(ret_type);
        return -1;
    }
    state->tcs_value = value;
    Py_INCREF(value);
    state->tcs_rt.rt_name = name;
    Py_INCREF(name);
    state->tcs_rt.rt_expected = (PyTypeObject *)ret_type;
    Py_INCREF(ret_type);
    state->tcs_rt.rt_optional = optional;

    Py_XDECREF(vtable->vt_entries[slot].vte_state);
    vtable->vt_entries[slot].vte_state = (PyObject *)state;
    if (coroutine) {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_coroutine;
    } else if (PyFunction_Check(value)) {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_func_overridable;
    } else if (classmethod) {
        vtable->vt_entries[slot].vte_entry = (vectorcallfunc)type_vtable_classmethod_overridable;
    } else {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_nonfunc;
    }
    return 0;
}

static int
type_init_subclass_vtables(PyTypeObject *target_type)
{
    /* TODO: This can probably be a lot more efficient.  If a type
     * hasn't been fully loaded yet we can probably propagate the
     * parent dict down, and either initialize the slot to the parent
     * slot (if not overridden) or initialize the slot to the child slot.
     * We then only need to populate the child dict w/ its members when
     * a member is accessed from the child type.  When we init the child
     * we can check if it's dict sharing with its parent. */
    PyObject *ref;
    PyObject *subclasses = target_type->tp_subclasses;
    if (subclasses != NULL) {
        Py_ssize_t i = 0;
        while (PyDict_Next(subclasses, &i, NULL, &ref)) {
            assert(PyWeakref_CheckRef(ref));
            ref = PyWeakref_GET_OBJECT(ref);
            if (ref == Py_None) {
                continue;
            }

            PyTypeObject *subtype = (PyTypeObject *) ref;
            if (subtype->tp_cache != NULL) {
                /* already inited */
                continue;
            }

            _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(subtype, 1);
            if (vtable == NULL) {
                return -1;
            }
        }
    }
    return 0;
}

static void
_PyClassLoader_UpdateDerivedSlot(PyTypeObject *type,
                                 PyObject *name,
                                 Py_ssize_t index,
                                 PyObject *state,
                                 vectorcallfunc func)
{
    /* Update any derived types which don't have slots */
    PyObject *ref;
    PyObject *subclasses = type->tp_subclasses;
    if (subclasses != NULL) {
        Py_ssize_t i = 0;
        while (PyDict_Next(subclasses, &i, NULL, &ref)) {
            assert(PyWeakref_CheckRef(ref));
            ref = PyWeakref_GET_OBJECT(ref);
            if (ref == Py_None) {
                continue;
            }

            PyTypeObject *subtype = (PyTypeObject *)ref;
            PyObject *override = PyDict_GetItem(subtype->tp_dict, name);
            if (override != NULL) {
                /* subtype overrides the value */
                continue;
            }

            assert(subtype->tp_cache != NULL);
            _PyType_VTable *subvtable = (_PyType_VTable *)subtype->tp_cache;
            Py_XDECREF(subvtable->vt_entries[index].vte_state);
            subvtable->vt_entries[index].vte_state = state;
            Py_INCREF(state);
            subvtable->vt_entries[index].vte_entry = func;

            _PyClassLoader_UpdateDerivedSlot(
                subtype, name, index, state, func);
        }
    }
}

static int
thunktraverse(_Py_StaticThunk *op, visitproc visit, void *arg)
{
    rettype_check_traverse((_PyClassLoader_RetTypeInfo *)op, visit, arg);
    visit(op->thunk_tcs.tcs_value, arg);
    visit((PyObject *)op->thunk_cls, arg);
    return 0;
}

static int
thunkclear(_Py_StaticThunk *op)
{
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_CLEAR(op->thunk_tcs.tcs_value);
    Py_CLEAR(op->thunk_cls);
    return 0;
}

static void
thunkdealloc(_Py_StaticThunk *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_XDECREF(op->thunk_tcs.tcs_value);
    Py_XDECREF(op->thunk_cls);
    PyObject_GC_Del((PyObject *)op);
}


PyObject *
thunk_vectorcall(_Py_StaticThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames) {
    if (thunk->thunk_tcs.tcs_value == NULL) {
        PyErr_Format(PyExc_TypeError, "%s.%U has been deleted", thunk->thunk_cls->tp_name, thunk->thunk_tcs.tcs_rt.rt_name);
        return NULL;
    }
    if (thunk->thunk_classmethod) {
        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
        if (nargs == 0) {
            PyErr_Format(PyExc_TypeError, "%s.%U must be invoked with >= 1 arguments",
                         thunk->thunk_cls->tp_name, thunk->thunk_tcs.tcs_rt.rt_name);
            return NULL;
        }

        if (thunk->thunk_coroutine) {
          return type_vtable_coroutine((_PyClassLoader_TypeCheckState *)thunk, args + 1,
                                       nargs - 1, kwnames);
        }
        PyObject *res = _PyObject_Vectorcall(thunk->thunk_tcs.tcs_value, args + 1, nargs - 1, kwnames);
        return rettype_check(thunk->thunk_cls, res, (_PyClassLoader_RetTypeInfo *)thunk);
    }

    if (thunk->thunk_coroutine) {
        return type_vtable_coroutine((_PyClassLoader_TypeCheckState *)thunk, args,
            nargsf & ~_Py_AWAITED_CALL_MARKER, kwnames);
    }

    PyObject *res = _PyObject_Vectorcall(thunk->thunk_tcs.tcs_value, args, nargsf & ~_Py_AWAITED_CALL_MARKER, kwnames);
    return rettype_check(thunk->thunk_cls, res, (_PyClassLoader_RetTypeInfo *)thunk);
}


static PyObject *
thunk_call(_Py_StaticThunk *thunk, PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_RuntimeError, "thunk_call shouldn't be invokable");
    return NULL;
}

PyTypeObject _PyType_StaticThunk = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "static_thunk",
    sizeof(_Py_StaticThunk),
    .tp_dealloc = (destructor)thunkdealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
        _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_traverse = (traverseproc)thunktraverse,
    .tp_clear = (inquiry)thunkclear,
    .tp_vectorcall_offset = offsetof(_Py_StaticThunk, thunk_vectorcall),
    .tp_call = (ternaryfunc)thunk_call,
};

int
get_func_or_special_callable(PyObject *dict, PyObject *name, PyObject **result);

int _PyClassLoader_InitTypeForPatching(PyTypeObject *type) {
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    if (vtable != NULL && vtable->vt_original != NULL) {
        return 0;
    }
    if (_PyClassLoader_EnsureVtable(type, 0) == NULL) {
        return -1;
    }
    vtable = (_PyType_VTable *)type->tp_cache;

    PyObject *name, *slot, *clsitem;
    PyObject *slotmap = vtable->vt_slotmap;
    PyObject *origitems = vtable->vt_original = PyDict_New();

    Py_ssize_t i = 0;
    while (PyDict_Next(slotmap, &i, &name, &slot)) {
        if (get_func_or_special_callable(type->tp_dict, name, &clsitem)) {
             return -1;
        }
        if (clsitem != NULL) {
            if (PyDict_SetItem(origitems, name, clsitem)) {
                Py_DECREF(clsitem);
                goto error;
            }
            Py_DECREF(clsitem);
        }
    }
    return 0;
error:
    vtable->vt_original = NULL;
    Py_DECREF(origitems);
    return -1;
}

PyObject *
_PyClassLoader_ResolveReturnType(PyObject *func, int *optional, int *coroutine, int *classmethod) {
    *coroutine = *optional = *classmethod = 0;
    PyTypeObject *res = NULL;
    if (PyFunction_Check(func) && _PyClassLoader_IsStaticFunction(func)) {
        res = resolve_function_rettype(func, optional, coroutine);
    } else if (Py_TYPE(func) == &PyStaticMethod_Type) {
        PyObject *static_func = _PyStaticMethod_GetFunc(func);
        if (_PyClassLoader_IsStaticFunction(static_func)) {
            res = resolve_function_rettype(static_func, optional, coroutine);
        }
    } else if (Py_TYPE(func) == &PyClassMethod_Type) {
        PyObject *static_func = _PyClassMethod_GetFunc(func);
        if (_PyClassLoader_IsStaticFunction(static_func)) {
            res = resolve_function_rettype(static_func, optional, coroutine);
        }
        *classmethod = 1;
    } else if (Py_TYPE(func) == &PyMethodDescr_Type) {
        // builtin methods for now assumed to return object
        res = &PyBaseObject_Type;
        Py_INCREF(res);
        *optional = 1;
    } else if (Py_TYPE(func) == &PyProperty_Type) {
        propertyobject *property = (propertyobject *)func;
        PyObject *fget = property->prop_get;
        if (_PyClassLoader_IsStaticFunction(fget)) {
            res = resolve_function_rettype(fget, optional, coroutine);
        }
    } else if (_PyClassLoader_IsStaticBuiltin(func)) {
        PyMethodDef *def = ((PyCFunctionObject *)func)->m_ml;
        _PyTypedMethodDef *tmd = (_PyTypedMethodDef *)(def->ml_meth);
        switch (tmd->tmd_ret) {
            case _Py_SIG_VOID:
            case _Py_SIG_ERROR:
                res = (PyTypeObject *)&_PyNone_Type;
                Py_INCREF(res);
                break;
        }
    }
    return (PyObject *)res;
}

int
get_func_or_special_callable(PyObject *dict, PyObject *name, PyObject **result) {
    if (PyTuple_CheckExact(name) && classloader_is_property_tuple((PyTupleObject *) name)) {
        PyObject *property = PyDict_GetItem(dict, PyTuple_GET_ITEM(name, 0));
        if (property == NULL) {
            *result = NULL;
            return 0;
        }

        *result = classloader_get_property_method(property,
                                                  (PyTupleObject *) name);
        if (*result == NULL) {
            return -1;
        }
        return 0;
    } else {
        *result = PyDict_GetItem(dict, name);
    }
    Py_XINCREF(*result);
    return 0;
}

int
_PyClassLoader_GetStaticallyInheritedMember(PyTypeObject *type, PyObject *name, PyObject **result) {
    PyObject *mro = type->tp_mro, *base;

    for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(mro); i++) {
        PyTypeObject *next = (PyTypeObject *)PyTuple_GET_ITEM(type->tp_mro, i);
        PyObject *dict;
        if (next->tp_cache != NULL &&
                ((_PyType_VTable *)next->tp_cache)->vt_original != NULL) {
            dict = ((_PyType_VTable *)next->tp_cache)->vt_original;
        } else {
            dict = next->tp_dict;
        }
        if (dict == NULL) {
            continue;
        }

        if (get_func_or_special_callable(dict, name, &base)) {
            return -1;
        }
        if (base != NULL) {
            if (!used_in_vtable(base)) {
                Py_DECREF(base);
                continue;
            }
            *result = base;
            return 0;
        }
    }
    *result = NULL;
    return 0;
}

static PyObject *g_fget = NULL;
static PyObject *g_fset = NULL;

PyObject *
get_property_getter_descr_tuple(PyObject *name)
{
    if (g_fget == NULL) {
        g_fget = PyUnicode_FromStringAndSize("fget", 4);
    }
    PyObject *getter_tuple = PyTuple_New(2);
    Py_INCREF(name);
    PyTuple_SET_ITEM(getter_tuple, 0, name);
    Py_INCREF(g_fget);
    PyTuple_SET_ITEM(getter_tuple, 1, g_fget);
    return getter_tuple;
}

PyObject *
get_property_setter_descr_tuple(PyObject *name)
{
    if (g_fset == NULL) {
        g_fset = PyUnicode_FromStringAndSize("fset", 4);
    }
    PyObject *setter_tuple = PyTuple_New(2);
    Py_INCREF(name);
    PyTuple_SET_ITEM(setter_tuple, 0, name);
    Py_INCREF(g_fset);
    PyTuple_SET_ITEM(setter_tuple, 1, g_fset);
    return setter_tuple;
}

static void
update_thunk(_Py_StaticThunk *thunk, PyObject *previous, PyObject *new_value)
{
    Py_CLEAR(thunk->thunk_tcs.tcs_value);
    if (new_value != NULL) {
        thunk->thunk_tcs.tcs_value = new_value;
        Py_INCREF(new_value);
    }
    if (new_value == previous) {
        thunk->thunk_funcref = previous;
    } else {
        thunk->thunk_funcref = (PyObject *)thunk;
    }
}

int
_PyClassLoader_UpdateSlot(PyTypeObject *type,
                          PyObject *name,
                          PyObject *new_value)
{
    assert(type->tp_cache != NULL);
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    PyObject *slotmap = vtable->vt_slotmap;

    PyObject *slot = PyDict_GetItem(slotmap, name);
    if (slot == NULL) {
        return 0;
    }

    PyObject *previous;
    if (vtable->vt_original != NULL) {
        assert(type->tp_flags & Py_TPFLAGS_IS_STATICALLY_DEFINED);
        previous = PyDict_GetItem(vtable->vt_original, name);
    } else {
        /* non-static type can't influence our original static return type */
        assert(!(type->tp_flags & Py_TPFLAGS_IS_STATICALLY_DEFINED));
        previous = NULL;
    }

    /* update the value that exists in our thunks for performing indirections
     * necessary for patched INVOKE_FUNCTION calls */
    if (vtable->vt_thunks != NULL) {
        _Py_StaticThunk *thunk = (_Py_StaticThunk *)PyDict_GetItem(vtable->vt_thunks, name);
        if (thunk != NULL) {
            update_thunk(thunk, previous, new_value);
        }
    }

    int deleting = new_value == NULL;

    /* we need to search in the MRO if we don't contain the
     * item directly or we're currently deleting the current value */
    PyObject *base = NULL;
    if (previous == NULL || new_value == NULL) {
        if (_PyClassLoader_GetStaticallyInheritedMember(type, name, &base)) {
            return -1;
        }

        assert(base != NULL || previous != NULL);

        if (base != NULL) {
            if (previous == NULL) {
                /* we use the inherited member as the current member for this class */
                previous = base;
            }
            if (new_value == NULL) {
                /* after deletion we pick up the inherited member as our current value */
                new_value = base;
            }
        }
    }

    assert(previous != NULL);

    // if this is a property slot, also update the getter and setter slots
    if (Py_TYPE(previous) == &PyProperty_Type) {
        PyTupleObject *getter_tuple = (PyTupleObject *)get_property_getter_descr_tuple(name);
        PyObject *new_getter = deleting ? NULL : classloader_get_property_fget(new_value);
        if(_PyClassLoader_UpdateSlot(type, (PyObject *)getter_tuple, new_getter)) {
            Py_DECREF(getter_tuple);
            Py_XDECREF(new_getter);
            Py_XDECREF(base);
            return -1;
        }
        Py_XDECREF(new_getter);
        Py_DECREF(getter_tuple);

        PyTupleObject *setter_tuple = (PyTupleObject *)get_property_setter_descr_tuple(name);
        PyObject *new_setter = deleting ? NULL : classloader_get_property_fset(new_value);
        if(_PyClassLoader_UpdateSlot(type, (PyObject *)setter_tuple, new_setter)) {
            Py_DECREF(setter_tuple);
            Py_XDECREF(new_setter);
            Py_XDECREF(base);
            return -1;
        }
        Py_XDECREF(new_setter);
        Py_DECREF(setter_tuple);
    }

    int cur_optional, cur_coroutine, cur_classmethod;
    PyObject *cur_type = _PyClassLoader_ResolveReturnType(previous, &cur_optional,
                                                          &cur_coroutine, &cur_classmethod);

    assert(cur_type != NULL);
    Py_ssize_t index = PyLong_AsSsize_t(slot);

    /* we make no attempts to keep things efficient when types start getting
     * mutated.  We always install the less efficient type checked functions,
     * rather than having to deal with a proliferation of states */
    if (new_value == NULL) {
        /* The value is deleted, and we didn't find one in a base class.
         * We'll put in a value which raises AttributeError */
        PyObject *missing_state = PyTuple_New(3);
        if (missing_state == NULL) {
            Py_DECREF(cur_type);
            Py_XDECREF(base);
            return -1;
        }

        PyObject *func_name = classloader_get_func_name(name);
        PyTuple_SET_ITEM(missing_state, 0, func_name);
        PyTuple_SET_ITEM(missing_state, 1, cur_type);
        PyObject *optional = cur_optional ? Py_True : Py_False;
        PyTuple_SET_ITEM(missing_state, 2, optional);
        Py_INCREF(func_name);
        Py_INCREF(cur_type);
        Py_INCREF(optional);

        Py_XDECREF(vtable->vt_entries[index].vte_state);
        vtable->vt_entries[index].vte_state = missing_state;
        vtable->vt_entries[index].vte_entry = (vectorcallfunc)type_vtable_func_missing;
    } else if (type_vtable_setslot_typecheck(cur_type,
                                             cur_optional,
                                             cur_coroutine,
                                             cur_classmethod,
                                             name,
                                             vtable,
                                             index,
                                             new_value)) {
        Py_DECREF(cur_type);
        Py_XDECREF(base);
        return -1;
    }
    Py_DECREF(cur_type);
    Py_XDECREF(base);

    /* propagate slot update to derived classes that don't override
     * the function (but first, ensure they have initialized vtables) */
    if (type_init_subclass_vtables(type) != 0) {
        return -1;
    }
    _PyClassLoader_UpdateDerivedSlot(type,
                                     name,
                                     index,
                                     vtable->vt_entries[index].vte_state,
                                     vtable->vt_entries[index].vte_entry);
    return 0;
}

static int
type_vtable_setslot(PyTypeObject *tp,
                    PyObject *name,
                    _PyType_VTable *vtable,
                    Py_ssize_t slot,
                    PyObject *value)
{
    if (tp->tp_dictoffset == 0) {
        if (_PyClassLoader_IsStaticFunction(value)) {
            return type_vtable_set_opt_slot(tp, name, vtable, slot, value);
        } else if (Py_TYPE(value) == &PyStaticMethod_Type &&
                _PyClassLoader_IsStaticFunction(_PyStaticMethod_GetFunc(value))) {
            Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
            vtable->vt_entries[slot].vte_entry = type_vtable_staticmethod;
            Py_INCREF(value);
            return 0;
        } else if (Py_TYPE(value) == &PyClassMethod_Type &&
                   _PyClassLoader_IsStaticFunction(_PyClassMethod_GetFunc(value))) {
            Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
            vtable->vt_entries[slot].vte_entry = type_vtable_classmethod;
            Py_INCREF(value);
            return 0;
        } else if (Py_TYPE(value) == &PyMethodDescr_Type) {
            Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
            vtable->vt_entries[slot].vte_entry =
                ((PyMethodDescrObject *)value)->vectorcall;
            Py_INCREF(value);
            return 0;
        }
    }

    PyObject *original;
    if (vtable->vt_original != NULL) {
        assert(tp->tp_flags & Py_TPFLAGS_IS_STATICALLY_DEFINED);
        original = PyDict_GetItem(vtable->vt_original, name);
        Py_INCREF(original);
    } else if (_PyClassLoader_IsStaticFunction(value) || _PyClassLoader_IsStaticBuiltin(value)) {
        /* non-static type can't influence our original static return type */
        original = value;
        Py_INCREF(value);
    } else {
        if (_PyClassLoader_GetStaticallyInheritedMember(tp, name, &original)) {
            return -1;
        }
        if (original == NULL) {
            PyErr_Format(PyExc_RuntimeError,
                        "unable to resolve base method for %R in %s",
                        name, tp->tp_name);
            return -1;
        }
    }

    int optional = 0, coroutine = 0, classmethod = 0;
    PyObject *ret_type = _PyClassLoader_ResolveReturnType(original, &optional, &coroutine, &classmethod);
    Py_DECREF(original);
    if (ret_type == NULL) {
        PyErr_Format(PyExc_RuntimeError,
                    "missing type annotation on static compiled method %R of %s",
                    name, tp->tp_name);
        return -1;
    }

    int res = type_vtable_setslot_typecheck(
        ret_type, optional, coroutine, classmethod, name, vtable, slot, value);
    Py_DECREF(ret_type);
    return res;
}

static PyObject *
type_vtable_lazyinit(PyObject *name,
                     PyObject **args,
                     size_t nargsf,
                     PyObject *kwnames)
{
    PyObject *self = args[0];
    PyTypeObject *type;
    if (nargsf & _Py_VECTORCALL_INVOKED_CLASSMETHOD) {
        type = (PyTypeObject *)self;
    }
    else {
        type = Py_TYPE(self);
    }
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    PyObject *mro = type->tp_mro;
    Py_ssize_t slot =
        PyLong_AsSsize_t(PyDict_GetItem(vtable->vt_slotmap, name));

    assert(vtable != NULL);

    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        PyObject *value;
        PyTypeObject *cur_type = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        if (get_func_or_special_callable(cur_type->tp_dict, name, &value)) {
            return NULL;
        }
        if (value != NULL) {
            if (type_vtable_setslot(type, name, vtable, slot, value)) {
                return NULL;
            }
            Py_DECREF(value);

            return vtable->vt_entries[slot].vte_entry(
                vtable->vt_entries[slot].vte_state, args, nargsf, kwnames);
        }
    }

    PyErr_Format(
        PyExc_TypeError, "'%s' has no attribute %U", type->tp_name, name);
    return NULL;
}

void
_PyClassLoader_ClearCache()
{
    Py_CLEAR(classloader_cache);
    Py_CLEAR(genericinst_cache);
}

void
_PyClassLoader_ReinitVtable(_PyType_VTable *vtable)
{
    PyObject *name, *slot;
    PyObject *slotmap = vtable->vt_slotmap;
    Py_ssize_t i = 0;
    while (PyDict_Next(slotmap, &i, &name, &slot)) {
        Py_ssize_t index = PyLong_AsSsize_t(slot);
        vtable->vt_entries[index].vte_state = name;
        Py_INCREF(name);
        vtable->vt_entries[index].vte_entry = (vectorcallfunc) type_vtable_lazyinit;
    }
}

int
used_in_vtable_worker(PyObject *value) {
    if (Py_TYPE(value) == &PyMethodDescr_Type) {
        return 1;
    } else if (_PyClassLoader_IsStaticFunction(value)) {
        return 1;
    } else if (_PyClassLoader_IsStaticBuiltin(value)) {
        return 1;
    }
    return 0;
}

int
used_in_vtable(PyObject *value)
{
    if (used_in_vtable_worker(value)) {
        return 1;
    } else if (Py_TYPE(value) == &PyStaticMethod_Type &&
               used_in_vtable_worker(_PyStaticMethod_GetFunc(value))) {
        return 1;
    } else if (Py_TYPE(value) == &PyClassMethod_Type &&
               used_in_vtable_worker(_PyClassMethod_GetFunc(value))) {
        return 1;
    } else if (Py_TYPE(value) == &PyProperty_Type) {
        PyObject *getter = classloader_get_property_fget(value);
        PyObject *setter = classloader_get_property_fset(value);
        int res = used_in_vtable_worker(getter) || used_in_vtable_worker(setter);
        Py_XDECREF(getter);
        Py_XDECREF(setter);
        return res;
    }
    return 0;
}

int
_PyClassLoader_UpdateSlotMap(PyTypeObject *self, PyObject *slotmap) {
    PyObject *key, *value;
    Py_ssize_t i;

    /* Now add indexes for anything that is new in our class */
    int slot_index = PyDict_Size(slotmap);
    i = 0;
    while (PyDict_Next(self->tp_dict, &i, &key, &value)) {
        if (PyDict_GetItem(slotmap, key) || !used_in_vtable(value)) {
            /* we either share the same slot, or this isn't a static function,
             * so it doesn't need a slot */
            continue;
        }

        PyObject *index = PyLong_FromLong(slot_index++);
        int err = PyDict_SetItem(slotmap, key, index);
        Py_DECREF(index);
        if (err) {
            return -1;
        }
        if (Py_TYPE(value) == &PyProperty_Type) {
            PyObject *getter_index = PyLong_FromLong(slot_index++);
            PyObject *getter_tuple = get_property_getter_descr_tuple(key);
            err = PyDict_SetItem(slotmap, getter_tuple, getter_index);
            Py_DECREF(getter_index);
            Py_DECREF(getter_tuple);
            if (err) {
                return -1;
            }
            PyObject *setter_index = PyLong_FromLong(slot_index++);
            PyObject *setter_tuple = get_property_setter_descr_tuple(key);
            err = PyDict_SetItem(slotmap, setter_tuple, setter_index);
            Py_DECREF(setter_index);
            Py_DECREF(setter_tuple);
            if (err) {
                return -1;
            }
        }
    }
    return 0;
}

int is_static_type(PyTypeObject *type) {
    return (type->tp_flags & (Py_TPFLAGS_IS_STATICALLY_DEFINED|Py_TPFLAGS_GENERIC_TYPE_INST)) ||
        !(type->tp_flags & Py_TPFLAGS_HEAPTYPE);
}

_PyType_VTable *
_PyClassLoader_EnsureVtable(PyTypeObject *self, int init_subclasses)
{
    _PyType_VTable *vtable = (_PyType_VTable *)self->tp_cache;
    PyObject *slotmap = NULL;
    PyObject *mro;

    if (self == &PyBaseObject_Type) {
        PyErr_SetString(PyExc_RuntimeError, "cannot initialize vtable for builtins.object");
        return NULL;
    }
    if (vtable != NULL) {
        return vtable;
    }

    mro = self->tp_mro;
    Py_ssize_t mro_size = PyTuple_GET_SIZE(mro);
    if (mro_size > 1) {
        /* TODO: Non-type objects in mro? */
        /* TODO: Multiple inheritance */

        /* Get the size of the next element which is a static class
         * in our mro, we'll build on it.  We don't care about any
         * non-static classes because we don't generate invokes to them */
        PyTypeObject *next;
        for (Py_ssize_t i = 1; i < mro_size; i++) {
            next = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
            if (is_static_type(next)) {
                break;
            }
        }

        assert(PyType_Check(next));
        assert(is_static_type(next));
        if (next != &PyBaseObject_Type) {
            _PyType_VTable *base_vtable = (_PyType_VTable *)next->tp_cache;
            if (base_vtable == NULL) {
                base_vtable = _PyClassLoader_EnsureVtable(next, 0);

                if (base_vtable == NULL) {
                    return NULL;
                }

                if (init_subclasses &&
                    type_init_subclass_vtables(next)) {
                    return NULL;
                }

                if (self->tp_cache != NULL) {
                    /* we have recursively initialized the current v-table,
                     * no need to continue with initialization now */
                    return (_PyType_VTable *)self->tp_cache;
                }
            }

            PyObject *next_slotmap = base_vtable->vt_slotmap;
            assert(next_slotmap != NULL);

            slotmap = PyDict_Copy(next_slotmap);
            if (slotmap == NULL) {
                return NULL;
            }
        }
    }

    if (slotmap == NULL) {
        slotmap = _PyDict_NewPresized(PyDict_Size(self->tp_dict));
    }

    if (slotmap == NULL) {
        return NULL;
    }

    if (is_static_type(self)) {
        if (_PyClassLoader_UpdateSlotMap(self, slotmap)) {
            Py_DECREF(slotmap);
            return NULL;
        }
    }

    /* finally allocate the vtable, which will have empty slots initially */
    Py_ssize_t slot_count = PyDict_Size(slotmap);
    vtable =
        PyObject_GC_NewVar(_PyType_VTable, &_PyType_VTableType, slot_count);

    if (vtable == NULL) {
        Py_DECREF(slotmap);
        return NULL;
    }
    vtable->vt_size = slot_count;
    vtable->vt_thunks = NULL;
    vtable->vt_original = NULL;
    vtable->vt_slotmap = slotmap;
    self->tp_cache = (PyObject *)vtable;

    _PyClassLoader_ReinitVtable(vtable);

    PyObject_GC_Track(vtable);

    if (init_subclasses && type_init_subclass_vtables(self)) {
        return NULL;
    }

    return vtable;
}

static int
clear_vtables_recurse(PyTypeObject *type)
{
    PyObject *subclasses = type->tp_subclasses;
    PyObject *ref;
    Py_CLEAR(type->tp_cache);
    if (subclasses != NULL) {
        Py_ssize_t i = 0;
        while (PyDict_Next(subclasses, &i, NULL, &ref)) {
            assert(PyWeakref_CheckRef(ref));
            ref = PyWeakref_GET_OBJECT(ref);
            if (ref == Py_None) {
                continue;
            }

            assert(PyType_Check(ref));
            if (clear_vtables_recurse((PyTypeObject *)ref)) {
                return -1;
            }
        }
    }
    return 0;
}

int
_PyClassLoader_ClearVtables()
{
    /* Recursively clear all vtables.
     *
     * This is really only intended for use in tests to avoid state pollution.
     */
    Py_CLEAR(classloader_cache);
    return clear_vtables_recurse(&PyBaseObject_Type);
}

PyObject *_PyClassLoader_GetGenericInst(PyObject *type,
                                        PyObject **args,
                                        Py_ssize_t nargs);

static int classloader_verify_type(PyObject *type, PyObject *path) {
    if (type == NULL || !PyType_Check(type)) {
        PyErr_Format(
            PyExc_TypeError,
            "bad name provided for class loader: %R, not a class",
            path);
        return -1;
    }
    return 0;
}

static PyObject *
classloader_instantiate_generic(PyObject *gtd, PyObject *name, PyObject *path) {
    if (!PyType_Check(gtd)) {
        PyErr_Format(PyExc_TypeError,
                        "generic type instantiation without type: %R on "
                        "%U from %s",
                        path,
                        name,
                        gtd->ob_type->tp_name);
        return NULL;
    }
    PyObject *tmp_tuple = PyTuple_New(PyTuple_GET_SIZE(name));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(name); i++) {
        int optional;
        PyObject *param = (PyObject *)_PyClassLoader_ResolveType(
            PyTuple_GET_ITEM(name, i), &optional);
        if (param == NULL) {
            Py_DECREF(tmp_tuple);
            return NULL;
        }
        if (optional) {
            PyObject *union_args = PyTuple_New(2);
            if (union_args == NULL) {
                Py_DECREF(tmp_tuple);
                return NULL;
            }
            /* taking ref from _PyClassLoader_ResolveType */
            PyTuple_SET_ITEM(union_args, 0, param);
            PyTuple_SET_ITEM(union_args, 1, Py_None);
            Py_INCREF(Py_None);

            PyObject *union_obj = _Py_Union(union_args);
            if (union_obj == NULL) {
                Py_DECREF(union_args);
                Py_DECREF(tmp_tuple);
                return NULL;
            }
            Py_DECREF(union_args);
            param = union_obj;
        }
        PyTuple_SET_ITEM(tmp_tuple, i, param);
    }

    PyObject *next = _PyClassLoader_GetGenericInst(
        gtd,
        ((PyTupleObject *)tmp_tuple)->ob_item,
        PyTuple_GET_SIZE(tmp_tuple));
    Py_DECREF(tmp_tuple);
    return next;
}

static PyObject *
classloader_get_member(PyObject *path,
                       Py_ssize_t items,
                       PyObject **container,
                       PyObject **containerkey)
{
    PyThreadState *tstate = PyThreadState_GET();
    PyObject *cur = tstate->interp->modules;

    if (cur == NULL) {
        PyErr_Format(
            PyExc_RuntimeError,
            "classloader_get_member() when import system is pre-init or post-teardown"
        );
        return NULL;
    }
    Py_INCREF(cur);

    if (container) {
        *container = NULL;
    }
    if (containerkey) {
        *containerkey = NULL;
    }
    for (Py_ssize_t i = 0; i < items; i++) {
        PyObject *d = NULL;
        PyObject *name = PyTuple_GET_ITEM(path, i);

        if (container != NULL) {
            Py_CLEAR(*container);
            Py_INCREF(cur);
            *container = cur;
        }

        if (PyTuple_CheckExact(name) &&
            !classloader_is_property_tuple((PyTupleObject *) name)) {
            PyObject *next = classloader_instantiate_generic(cur, name, path);
            if (next == NULL) {
                goto error;
            }
            Py_DECREF(cur);
            cur = next;
            continue;
        }

        if (PyDict_Check(cur)) {
            d = cur;
        } else if (PyModule_CheckExact(cur)) {
            d = PyModule_GetDict(cur);
        } else if (PyType_Check(cur)) {
            d = ((PyTypeObject *)cur)->tp_dict;
        }

        if (d == NULL) {
            PyObject *next = PyObject_GetAttr(cur, name);
            if (next == NULL) {
                PyErr_Format(
                    PyExc_TypeError,
                    "bad name provided for class loader: %R on %R from %s",
                    path,
                    name,
                    cur->ob_type->tp_name);
                goto error;
            }
            Py_DECREF(cur);
            cur = next;
            continue;
        }

        PyObject *et = NULL, *ev = NULL, *tb = NULL;
        PyObject *next;
        if (containerkey != NULL) {
            *containerkey = name;
        }
        if (get_func_or_special_callable(d, name, &next)) {
            return NULL;
        }

        if (next == NULL && d == tstate->interp->modules) {
            /* import module in case it's not available in sys.modules */
            PyObject *mod = PyImport_ImportModuleLevelObject(name, NULL, NULL, NULL, 0);
            if (mod == NULL) {
                PyErr_Fetch(&et, &ev, &tb);
            } else {
                next = _PyDict_GetItem_Unicode(d, name);
                Py_INCREF(next);
                Py_DECREF(mod);
            }
        } else if (next == Py_None && d == tstate->interp->builtins) {
            /* special case builtins.None, it's used to represent NoneType */
            Py_DECREF(next);
            next = (PyObject *)&_PyNone_Type;
            Py_INCREF(next);
        }

        if (next == NULL) {
            PyErr_Format(
                PyExc_TypeError,
                "bad name provided for class loader, %R doesn't exist in %R",
                name,
                path);
            _PyErr_ChainExceptions(et, ev, tb);
            goto error;
        }
        Py_DECREF(cur);
        cur = next;
    }

    return cur;
error:
    if (container) {
        Py_CLEAR(*container);
    }
    Py_DECREF(cur);
    return NULL;
}

int _PyClassLoader_GetTypeCode(PyTypeObject *type) {
    if (!(type->tp_flags & Py_TPFLAG_CPYTHON_ALLOCATED)) {
        return TYPED_OBJECT;
    }

    return PyHeapType_CINDER_EXTRA(type)->type_code;
}

/* Resolve a tuple type descr to a `prim_type` integer (`TYPED_*`); return -1
 * and set an error if the type cannot be resolved. */
int
_PyClassLoader_ResolvePrimitiveType(PyObject *descr) {
    int optional;
    PyTypeObject *type = _PyClassLoader_ResolveType(descr, &optional);
    if (type == NULL) {
        return -1;
    }
    int res = _PyClassLoader_GetTypeCode(type);
    Py_DECREF(type);
    return res;
}

/* Resolve a tuple type descr in the form ("module", "submodule", "Type") to a
 * PyTypeObject*` and `optional` integer out param.
 */
PyTypeObject *
_PyClassLoader_ResolveType(PyObject *descr, int *optional)
{
    if (!PyTuple_Check(descr) || PyTuple_GET_SIZE(descr) < 2) {
        PyErr_Format(PyExc_TypeError, "unknown type %R", descr);
        return NULL;
    }

    Py_ssize_t items = PyTuple_GET_SIZE(descr);
    PyObject *last = PyTuple_GET_ITEM(descr, items - 1);

    if (PyUnicode_Check(last) &&
        PyUnicode_CompareWithASCIIString(last, "?") == 0) {
        *optional = 1;
        items--;
    } else {
        *optional = 0;
    }

    if (classloader_cache != NULL) {
        PyObject *cache = PyDict_GetItem(classloader_cache, descr);
        if (cache != NULL) {
            Py_INCREF(cache);
            return (PyTypeObject *)cache;
        }
    }

    PyObject *res = classloader_get_member(descr, items, NULL, NULL);
    if (classloader_verify_type(res, descr)) {
        Py_XDECREF(res);
        return NULL;
    }

    if (classloader_cache == NULL) {
        classloader_cache = PyDict_New();
        if (classloader_cache == NULL) {
            Py_DECREF(res);
            return NULL;
        }
    }

    if (PyDict_SetItem(classloader_cache, descr, res)) {
        Py_DECREF(res);
        return NULL;
    }

    return (PyTypeObject *)res;
}

static int
classloader_init_slot(PyObject *path)
{
    /* path is "mod.submod.Class.func", start search from
     * sys.modules */
    PyTypeObject *target_type;
    PyObject *cur =
        classloader_get_member(path, PyTuple_GET_SIZE(path), (PyObject **)&target_type, NULL);
    if (cur == NULL) {
        assert(target_type == NULL);
        return -1;
    } else if (classloader_verify_type((PyObject *)target_type, path)) {
        Py_DECREF(cur);
        Py_XDECREF(target_type);
        return -1;
    }

    /* Now we need to update or make the v-table for this type */
    _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(target_type, 0);
    if (vtable == NULL) {
        Py_XDECREF(target_type);
        Py_DECREF(cur);
        return -1;
    }

    PyObject *slot_map = vtable->vt_slotmap;
    PyObject *slot_name = PyTuple_GET_ITEM(path, PyTuple_GET_SIZE(path) - 1);

    PyObject *new_index = PyDict_GetItem(slot_map, slot_name);
    assert(new_index != NULL);

    if (PyDict_SetItem(classloader_cache, path, new_index) ||
        type_init_subclass_vtables(target_type)) {
        Py_DECREF(target_type);
        Py_DECREF(cur);
        return -1;
    }

    Py_DECREF(target_type);
    Py_DECREF(cur);
    return 0;
}

Py_ssize_t
_PyClassLoader_ResolveMethod(PyObject *path)
{
    if (classloader_cache == NULL) {
        classloader_cache = PyDict_New();
        if (classloader_cache == NULL) {
            return -1;
        }
    }

    /* TODO: Should we gracefully handle when there are two
     * classes with the same name? */
    PyObject *slot_index_obj = PyDict_GetItem(classloader_cache, path);
    if (slot_index_obj == NULL && classloader_init_slot(path)) {
        return -1;
    }

    slot_index_obj = PyDict_GetItem(classloader_cache, path);
    return PyLong_AS_LONG(slot_index_obj);
}

_Py_StaticThunk *
get_or_make_thunk(PyObject *func, PyObject *original, PyTypeObject* type, PyObject *name) {
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    if (vtable->vt_thunks == NULL) {
        vtable->vt_thunks = PyDict_New();
        if (vtable->vt_thunks == NULL) {
            return NULL;
        }
    }
    _Py_StaticThunk *thunk = (_Py_StaticThunk *)PyDict_GetItem(vtable->vt_thunks, name);
    if (thunk != NULL) {
        Py_INCREF(thunk);
        return thunk;
    }
    thunk = PyObject_GC_New(_Py_StaticThunk, &_PyType_StaticThunk);
    if (thunk == NULL) {
        return NULL;
    }
    thunk->thunk_tcs.tcs_value = func;
    Py_INCREF(func);
    PyObject *func_name = classloader_get_func_name(name);
    thunk->thunk_tcs.tcs_rt.rt_name = func_name;
    Py_INCREF(func_name);
    thunk->thunk_cls = type;
    Py_INCREF(type);
    thunk->thunk_vectorcall = (vectorcallfunc)&thunk_vectorcall;
    if (func == original) {
        thunk->thunk_funcref = original;
    } else {
        thunk->thunk_funcref = (PyObject *)thunk;
    }
    thunk->thunk_tcs.tcs_rt.rt_expected = (PyTypeObject *)_PyClassLoader_ResolveReturnType(
                                                               original,
                                                               &thunk->thunk_tcs.tcs_rt.rt_optional,
                                                               &thunk->thunk_coroutine,
                                                               &thunk->thunk_classmethod);
    if (thunk->thunk_tcs.tcs_rt.rt_expected == NULL) {
        Py_DECREF(thunk);
        return NULL;
    }
    if (PyDict_SetItem(vtable->vt_thunks, name, (PyObject *)thunk)) {
        Py_DECREF(thunk);
        return NULL;
    }
    return thunk;
}

PyObject *
_PyClassLoader_ResolveFunction(PyObject *path, PyObject **container)
{
    PyObject *containerkey;
    PyObject *func =
        classloader_get_member(path, PyTuple_GET_SIZE(path), container, &containerkey);

    PyObject *original = NULL;
    if (container != NULL && *container != NULL && PyType_Check(*container)) {
        assert(containerkey != NULL);

        PyTypeObject *type = (PyTypeObject *)*container;
        if (type->tp_cache != NULL) {
            PyObject *originals = ((_PyType_VTable *)type->tp_cache)->vt_original;
            if (originals != NULL) {
              original = PyDict_GetItem(originals, containerkey);
                if (original == func) {
                    original = NULL;
                }
            }
        }
    }

    if (func != NULL) {
        if (Py_TYPE(func) == &PyStaticMethod_Type) {
            PyObject *res = _PyStaticMethod_GetFunc(func);
            Py_INCREF(res);
            Py_DECREF(func);
            func = res;
        }
        else if (Py_TYPE(func) == &PyClassMethod_Type) {
            PyObject *res = _PyClassMethod_GetFunc(func);
            Py_INCREF(res);
            Py_DECREF(func);
            func = res;
        }
    }

    if (original != NULL) {
        PyObject *res = (PyObject *)get_or_make_thunk(func, original, (PyTypeObject*)*container, containerkey);
        Py_DECREF(func);
        return res;
    }
    return func;
}

PyObject **
_PyClassLoader_GetIndirectPtr(PyObject *path, PyObject *func, PyObject *container) {
    PyObject **cache = NULL;
    PyObject *name = PyTuple_GET_ITEM(path, PyTuple_GET_SIZE(path) - 1);
    if (PyModule_Check(container) && _PyVectorcall_Function(func) != NULL) {
        /* modules have no special translation on things we invoke, so
         * we just rely upon the normal JIT dict watchers */
        PyObject *dict = PyModule_Dict(container);
        if (dict != NULL) {
            cache = _PyJIT_GetDictCache(dict, name);
        }
    } else if (PyType_Check(container)) {
        if (_PyVectorcall_Function(func) == NULL) {
            goto done;
        }

        _PyType_VTable *vtable = _PyClassLoader_EnsureVtable((PyTypeObject *)container, 0);
        if (vtable == NULL) {
            goto done;
        }

        /* we pass func in for original here.  Either the thunk will already exist
         * in which case the value has been patched, or it won't yet exist in which
         * case func is the original function in the type. */
        _Py_StaticThunk *thunk = get_or_make_thunk(func, func, (PyTypeObject *)container, name);
        if (thunk == NULL) {
            return NULL;
        }

        cache = &thunk->thunk_funcref;
        Py_DECREF(thunk);
    }
done:

    return cache;
}

int
_PyClassLoader_IsImmutable(PyObject *container) {
    if (PyType_Check(container)) {
        PyTypeObject *type = (PyTypeObject *)container;
        if (type->tp_flags & Py_TPFLAGS_FROZEN ||
            !(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
            return 1;
        }
    }

    if (PyStrictModule_CheckExact(container) &&
        ((PyStrictModuleObject *)container)->global_setter == NULL) {
        return 1;
    }
    return 0;
}

PyMethodDescrObject *
_PyClassLoader_ResolveMethodDef(PyObject *path)
{
    PyTypeObject *target_type;
    PyObject *cur =
        classloader_get_member(path, PyTuple_GET_SIZE(path), (PyObject **)&target_type, NULL);

    if (cur == NULL) {
        assert(target_type == NULL);
        return NULL;
    } else if (classloader_verify_type((PyObject *)target_type, path) ||
               target_type->tp_flags & Py_TPFLAGS_BASETYPE) {
        Py_XDECREF(target_type);
        Py_DECREF(cur);
        return NULL;
    }

    Py_DECREF(target_type);
    if (Py_TYPE(cur) == &PyMethodDescr_Type) {
        return (PyMethodDescrObject*)cur;
    }

    Py_DECREF(cur);
    return NULL;
}


int
_PyClassLoader_AddSubclass(PyTypeObject *base, PyTypeObject *type)
{
    if (base->tp_cache == NULL) {
        /* nop if base class vtable isn't initialized */
        return 0;
    }

    _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(type, 0);
    if (vtable == NULL) {
        return -1;
    }
    return 0;
}

int
_PyClassLoader_PrimitiveTypeToStructMemberType(int primitive_type)
{
    switch (primitive_type) {
    case TYPED_INT8:
        return T_BYTE;
    case TYPED_INT16:
        return T_SHORT;
    case TYPED_INT32:
        return T_INT;
    case TYPED_INT64:
        return T_LONG;
    case TYPED_UINT8:
        return T_UBYTE;
    case TYPED_UINT16:
        return T_USHORT;
    case TYPED_UINT32:
        return T_UINT;
    case TYPED_UINT64:
        return T_ULONG;
    case TYPED_BOOL:
        return T_BOOL;
    case TYPED_DOUBLE:
        return T_DOUBLE;
    case TYPED_SINGLE:
        return T_FLOAT;
    case TYPED_CHAR:
        return T_CHAR;
    case TYPED_OBJECT:
        return T_OBJECT_EX;
    default:
        PyErr_Format(
            PyExc_ValueError, "unknown struct type: %d", primitive_type);
        return -1;
    }
}

Py_ssize_t
_PyClassLoader_PrimitiveTypeToSize(int primitive_type)
{
    switch (primitive_type) {
    case TYPED_INT8:
        return sizeof(char);
    case TYPED_INT16:
        return sizeof(short);
    case TYPED_INT32:
        return sizeof(int);
    case TYPED_INT64:
        return sizeof(long);
    case TYPED_UINT8:
        return sizeof(unsigned char);
    case TYPED_UINT16:
        return sizeof(unsigned short);
    case TYPED_UINT32:
        return sizeof(unsigned int);
    case TYPED_UINT64:
        return sizeof(unsigned long);
    case TYPED_BOOL:
        return sizeof(char);
    case TYPED_DOUBLE:
        return sizeof(double);
    case TYPED_SINGLE:
        return sizeof(float);
    case TYPED_CHAR:
        return sizeof(char);
    case TYPED_OBJECT:
        return sizeof(PyObject *);
    default:
        PyErr_Format(
            PyExc_ValueError, "unknown struct type: %d", primitive_type);
        return -1;
    }
}

static int
classloader_init_field(PyObject *path, int *field_type)
{
    /* path is "mod.submod.Class.func", start search from
     * sys.modules */
    PyObject *cur =
        classloader_get_member(path, PyTuple_GET_SIZE(path), NULL, NULL);
    if (cur == NULL) {
        return -1;
    }

    if (Py_TYPE(cur) == &PyMemberDescr_Type) {
        if (field_type != NULL) {
            switch (((PyMemberDescrObject *)cur)->d_member->type) {
            case T_BYTE:
                *field_type = TYPED_INT8;
                break;
            case T_SHORT:
                *field_type = TYPED_INT16;
                break;
            case T_INT:
                *field_type = TYPED_INT32;
                break;
            case T_LONG:
                *field_type = TYPED_INT64;
                break;
            case T_UBYTE:
                *field_type = TYPED_UINT8;
                break;
            case T_USHORT:
                *field_type = TYPED_UINT16;
                break;
            case T_UINT:
                *field_type = TYPED_UINT32;
                break;
            case T_ULONG:
                *field_type = TYPED_UINT64;
                break;
            case T_BOOL:
                *field_type = TYPED_BOOL;
                break;
            case T_DOUBLE:
                *field_type = TYPED_DOUBLE;
                break;
            case T_FLOAT:
                *field_type = TYPED_SINGLE;
                break;
            case T_CHAR:
                *field_type = TYPED_CHAR;
                break;
            case T_OBJECT_EX:
                *field_type = TYPED_OBJECT;
                break;
            default:
                Py_DECREF(cur);
                PyErr_Format(
                    PyExc_ValueError, "unknown static type: %U", path);
                return -1;
            }
        }
        Py_DECREF(cur);
        Py_ssize_t offset = ((PyMemberDescrObject *)cur)->d_member->offset;
        return offset;
    } else if (Py_TYPE(cur) == &_PyTypedDescriptor_Type) {
        if (field_type != NULL) {
            *field_type = TYPED_OBJECT;
            assert(((_PyTypedDescriptor *)cur)->td_offset %
                       sizeof(Py_ssize_t) ==
                   0);
        }
        Py_DECREF(cur);
        return ((_PyTypedDescriptor *)cur)->td_offset;
    }

    Py_DECREF(cur);
    PyErr_Format(PyExc_TypeError, "bad field for class loader XX %R", path);
    return -1;
}

/* Resolves the offset for a given field, returning -1 on failure with an error
 * set or the field offset.  Path is a tuple in the form
 * ('module', 'class', 'field_name')
 */
Py_ssize_t
_PyClassLoader_ResolveFieldOffset(PyObject *path, int *field_type)
{
    if (classloader_cache == NULL) {
        classloader_cache = PyDict_New();
        if (classloader_cache == NULL) {
            return -1;
        }
    }

    /* TODO: Should we gracefully handle when there are two
     * classes with the same name? */
    PyObject *slot_index_obj = PyDict_GetItem(classloader_cache, path);
    if (slot_index_obj != NULL) {
        PyObject *offset = PyTuple_GET_ITEM(slot_index_obj, 0);
        if (field_type != NULL) {
            PyObject *type = PyTuple_GET_ITEM(slot_index_obj, 1);
            *field_type = PyLong_AS_LONG(type);
        }
        return PyLong_AS_LONG(offset);
    }

    int tmp_field_type = 0;
    Py_ssize_t slot_index = classloader_init_field(path, &tmp_field_type);
    if (slot_index < 0) {
        return -1;
    }
    slot_index_obj = PyLong_FromLong(slot_index);
    if (slot_index_obj == NULL) {
        return -1;
    }

    PyObject *field_type_obj = PyLong_FromLong(tmp_field_type);
    if (field_type_obj == NULL) {
        Py_DECREF(slot_index);
        return -1;
    }

    PyObject *cache = PyTuple_New(2);
    if (cache == NULL) {
        Py_DECREF(slot_index_obj);
        Py_DECREF(field_type_obj);
        return -1;
    }
    PyTuple_SET_ITEM(cache, 0, slot_index_obj);
    PyTuple_SET_ITEM(cache, 1, field_type_obj);

    if (PyDict_SetItem(classloader_cache, path, cache)) {
        Py_DECREF(cache);
        return -1;
    }

    Py_DECREF(cache);
    if (field_type != NULL) {
        *field_type = tmp_field_type;
    }

    return slot_index;
}

static void
typed_descriptor_dealloc(_PyTypedDescriptor *self)
{
    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->td_name);
    Py_XDECREF(self->td_type);
    Py_TYPE(self)->tp_free(self);
}

static int
typed_descriptor_traverse(_PyTypedDescriptor *self, visitproc visit, void *arg)
{
    Py_VISIT(self->td_type);
    return 0;
}

static int
typed_descriptor_clear(_PyTypedDescriptor *self)
{
    Py_CLEAR(self->td_type);
    return 0;
}

static PyObject *
typed_descriptor_get(PyObject *self, PyObject *obj, PyObject *cls)
{
    _PyTypedDescriptor *td = (_PyTypedDescriptor *)self;

    if (obj == NULL) {
        Py_INCREF(self);
        return self;
    }

    PyObject *res = *(PyObject **)(((char *)obj) + td->td_offset);
    if (res == NULL) {
        PyErr_Format(PyExc_AttributeError,
                     "'%s' object has no attribute '%U'",
                     Py_TYPE(obj)->tp_name,
                     td->td_name);
        return NULL;
    }
    Py_INCREF(res);
    return res;
}

static int
typed_descriptor_set(PyObject *self, PyObject *obj, PyObject *value)
{
    _PyTypedDescriptor *td = (_PyTypedDescriptor *)self;
    if (PyTuple_CheckExact(td->td_type)) {
        PyTypeObject *type =
            _PyClassLoader_ResolveType(td->td_type, &td->td_optional);
        if (type == NULL) {
            assert(PyErr_Occurred());
            if (value == Py_None && td->td_optional) {
                /* allow None assignment to optional values before the class is
                 * loaded */
                PyErr_Clear();
                PyObject **addr = (PyObject **)(((char *)obj) + td->td_offset);
                PyObject *prev = *addr;
                *addr = value;
                Py_INCREF(value);
                Py_XDECREF(prev);
                return 0;
            }
            return -1;
        }
        Py_DECREF(td->td_type);
        td->td_type = (PyObject *)type;
    }

    if (value == NULL ||
        (value == Py_None && td->td_optional) ||
        _PyObject_RealIsInstance(value, td->td_type)) {
        PyObject **addr = (PyObject **)(((char *)obj) + td->td_offset);
        PyObject *prev = *addr;
        *addr = value;
        Py_XINCREF(value);
        Py_XDECREF(prev);
        return 0;
    }

    PyErr_Format(PyExc_TypeError,
                 "expected '%s', got '%s' for attribute '%U'",
                 ((PyTypeObject *)td->td_type)->tp_name,
                 Py_TYPE(value)->tp_name,
                 td->td_name);

    return -1;
}

PyTypeObject _PyTypedDescriptor_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "typed_descriptor",
    .tp_basicsize = sizeof(_PyTypedDescriptor),
    .tp_dealloc = (destructor)typed_descriptor_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_traverse = (traverseproc)typed_descriptor_traverse,
    .tp_clear = (inquiry)typed_descriptor_clear,
    .tp_descr_get = typed_descriptor_get,
    .tp_descr_set = typed_descriptor_set,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};

PyObject *
_PyTypedDescriptor_New(PyObject *name, PyObject *type, Py_ssize_t offset)
{
    _PyTypedDescriptor *res =
        PyObject_GC_New(_PyTypedDescriptor, &_PyTypedDescriptor_Type);
    if (res == NULL) {
        return NULL;
    }

    res->td_name = name;
    res->td_type = type;
    res->td_offset = offset;
    res->td_optional = 0;
    Py_INCREF(name);
    Py_INCREF(type);
    PyObject_GC_Track(res);
    return (PyObject *)res;
}

PyObject *
gti_calc_name(PyObject *type, _PyGenericTypeInst *new_inst)
{
    Py_ssize_t nargs = new_inst->gti_size;
    const char *orig_name = ((PyTypeObject *)type)->tp_name;
    const char *dot;
    if ((dot = strchr(orig_name, '.')) != NULL) {
        orig_name = dot + 1;
    }
    char *start = strchr(orig_name, '[');
    assert(start != NULL);

    Py_ssize_t len = strlen(orig_name);
    for (int i = 0; i < nargs; i++) {
        PyTypeObject *type = new_inst->gti_inst[i].gtp_type;
        len += strlen(type->tp_name);
        if (new_inst->gti_inst[i].gtp_optional) {
            len += strlen("Optional[]");
        }
        len += 2;
    }

    char buf[len];
    strncpy(buf, orig_name, start - orig_name + 1);
    buf[start - orig_name + 1] = 0;
    for (int i = 0; i < nargs; i++) {
        PyTypeObject *type = new_inst->gti_inst[i].gtp_type;
        if (i != 0) {
            strcat(buf, ", ");
        }
        if (new_inst->gti_inst[i].gtp_optional) {
            strcat(buf, "Optional[");
        }
        strcat(buf, type->tp_name);
        if (new_inst->gti_inst[i].gtp_optional) {
            strcat(buf, "]");
        }
    }
    strcat(buf, "]");
    return PyUnicode_FromString(buf);
}

PyObject *
get_optional_type(PyObject *type)
{
    PyObject *res = NULL;
    PyObject *args = NULL;
    PyObject *origin = NULL;
    PyObject *name = NULL;

    if (!PyType_Check(type)) {
        _Py_IDENTIFIER(__args__);
        _Py_IDENTIFIER(__origin__);
        _Py_IDENTIFIER(_name);

        args = _PyObject_GetAttrId(type, &PyId___args__);
        if (args == NULL) {
            PyErr_Clear();
            goto done;
        } else if(!PyTuple_CheckExact(args) || PyTuple_GET_SIZE(args) != 2) {
            goto done;
        }

        if (Py_TYPE(type) != &_Py_UnionType) {
            origin = _PyObject_GetAttrId(type, &PyId___origin__);
            if (origin == NULL) {
                PyErr_Clear();
                goto done;
            } else if (strcmp(Py_TYPE(origin)->tp_name, "_SpecialForm")) {
                goto done;
            }

            name = _PyObject_GetAttrId(origin, &PyId__name);
            if (name == NULL) {
                PyErr_Clear();
                goto done;
            }
            if (!PyUnicode_CheckExact(name) || !_PyUnicode_EqualToASCIIString(name, "Union")) {
                goto done;
            }
        }

        PyObject *one = PyTuple_GET_ITEM(args, 0);
        PyObject *two = PyTuple_GET_ITEM(args, 1);
        if (PyType_Check(one) && (two == (PyObject *)Py_TYPE(Py_None) || two == Py_None)) {
            Py_INCREF(one);
            res = one;
        } else if (PyType_Check(two) &&
                    (one == (PyObject *)Py_TYPE(Py_None) || one == Py_None)) {
            Py_INCREF(two);
            res = two;
        }
    }

done:
    Py_XDECREF(args);
    Py_XDECREF(origin);
    Py_XDECREF(name);
    return res;
}

int
gtd_validate_type(PyObject *type, PyObject **args, Py_ssize_t nargs)
{
    /* We have no support for heap types as generic type definitions yet */
    assert(!(((PyTypeObject *)type)->tp_flags & Py_TPFLAGS_HEAPTYPE));
    /* We don't allow subclassing from generic classes yet */
    assert(!(((PyTypeObject *)type)->tp_flags & Py_TPFLAGS_BASETYPE));
    /* Can't create instances of generic types */
    assert(((PyTypeObject *)type)->tp_new == NULL);

    _PyGenericTypeDef *def = (_PyGenericTypeDef *)type;
    if (nargs != def->gtd_size) {
        PyErr_Format(PyExc_TypeError,
                     "%s expected %d generic arguments, got %d",
                     ((PyTypeObject *)type)->tp_name,
                     def->gtd_size,
                     nargs);
        return -1;
    }
    for (Py_ssize_t i = 0; i < nargs; i++) {
        if (!PyType_Check(args[i])) {
            PyObject *opt = get_optional_type(args[i]);
            if (opt == NULL) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "expected type or Optional[T] for generic argument");
                return -1;
            }
            Py_DECREF(opt);
        }
    }
    return 0;
}

PyObject *
gtd_make_key(PyObject *type, PyObject **args, Py_ssize_t nargs)
{
    PyObject *key = PyTuple_New(nargs + 1);
    if (key == NULL) {
        return NULL;
    }
    PyTuple_SET_ITEM(key, 0, type);
    Py_INCREF(type);
    for (Py_ssize_t i = 0; i < nargs; i++) {
        PyTuple_SET_ITEM(key, i + 1, args[i]);
        Py_INCREF(args[i]);
    }
    return key;
}

void
geninst_dealloc(PyObject *obj)
{
    /* these are heap types, so we need to decref their type.  We delegate
     * to the generic type definitions deallocator, and then dec ref the type
     * here */
    PyTypeObject *inst_type = Py_TYPE(obj);
    ((PyTypeObject *)((_PyGenericTypeInst *)inst_type)->gti_gtd)
        ->tp_dealloc(obj);
    Py_DECREF(inst_type);
}

PyObject *
gtd_new_inst(PyObject *type, PyObject **args, Py_ssize_t nargs)
{
    /* We have to allocate this in a very strange way, as we want the
     * extra space for a _PyGenericTypeInst, along with the generic
     * arguments.  But the type can't have a non-zero Py_SIZE (which would
     * be for PyHeapTypeObject's PyMemberDef's).  So we calculate the
     * size by hand.  This is currently fine as we don't support subclasses
     * of generic types. */
    Py_ssize_t size = _Py_SIZE_ROUND_UP(
        sizeof(_PyGenericTypeInst) + sizeof(_PyGenericTypeParam) * nargs,
        SIZEOF_VOID_P);

    _PyGenericTypeInst *new_inst =
        (_PyGenericTypeInst *)_PyObject_GC_Malloc(size);
    if (new_inst == NULL) {
        return NULL;
    }
    PyObject_INIT_VAR(new_inst, &PyType_Type, 0);

    /* We've allocated the heap on the type, mark it as a heap type. */

    /* Copy the generic def into the instantiation */
    memset(((char *)new_inst) + sizeof(PyVarObject),
           0,
           sizeof(PyHeapTypeObject) - sizeof(PyObject));
    PyTypeObject *new_type = (PyTypeObject *)new_inst;
#define COPY_DATA(name) new_type->name = ((PyTypeObject *)type)->name;
    COPY_DATA(tp_basicsize);
    COPY_DATA(tp_itemsize);
    new_type->tp_dealloc = geninst_dealloc;
    COPY_DATA(tp_vectorcall_offset);
    COPY_DATA(tp_getattr);
    COPY_DATA(tp_setattr);
    COPY_DATA(tp_as_async);
    COPY_DATA(tp_repr);
    COPY_DATA(tp_as_number);
    COPY_DATA(tp_as_sequence);
    COPY_DATA(tp_as_mapping);
    COPY_DATA(tp_hash);
    COPY_DATA(tp_call);
    COPY_DATA(tp_str);
    COPY_DATA(tp_getattro);
    COPY_DATA(tp_setattro);
    COPY_DATA(tp_as_buffer);
    COPY_DATA(tp_flags);
    COPY_DATA(tp_doc);
    COPY_DATA(tp_traverse);
    COPY_DATA(tp_clear);
    COPY_DATA(tp_richcompare);
    COPY_DATA(tp_weaklistoffset);
    COPY_DATA(tp_iter);
    COPY_DATA(tp_iternext);
    COPY_DATA(tp_methods);
    COPY_DATA(tp_members);
    COPY_DATA(tp_getset);
    COPY_DATA(tp_base);
    Py_XINCREF(new_type->tp_base);
    COPY_DATA(tp_descr_get);
    COPY_DATA(tp_descr_set);
    COPY_DATA(tp_dictoffset);
    COPY_DATA(tp_init);
    COPY_DATA(tp_alloc);
    COPY_DATA(tp_new);
    COPY_DATA(tp_free);
    new_type->tp_new = ((_PyGenericTypeDef *)type)->gtd_new;
#undef COPY_DATA

    new_inst->gti_type.ht_type.tp_flags |=
        Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_FROZEN | Py_TPFLAGS_GENERIC_TYPE_INST;
    new_inst->gti_type.ht_type.tp_flags &=
        ~(Py_TPFLAGS_READY | Py_TPFLAGS_GENERIC_TYPE_DEF);

    new_inst->gti_gtd = (_PyGenericTypeDef *)type;
    Py_INCREF(type);

    new_inst->gti_size = nargs;

    for (int i = 0; i < nargs; i++) {
        PyObject *opt_type = get_optional_type(args[i]);
        if (opt_type == NULL) {
            new_inst->gti_inst[i].gtp_type = (PyTypeObject *)args[i];
            Py_INCREF(args[i]);
            new_inst->gti_inst[i].gtp_optional = 0;
        } else {
            new_inst->gti_inst[i].gtp_type = (PyTypeObject *)opt_type;
            new_inst->gti_inst[i].gtp_optional = 1;
        }
    }

    PyObject *name = gti_calc_name(type, new_inst);
    if (name == NULL) {
        goto error;
    }

    new_inst->gti_type.ht_name = name;
    new_inst->gti_type.ht_qualname = name;
    Py_INCREF(name);
    Py_ssize_t name_size;
    new_inst->gti_type.ht_type.tp_name =
        PyUnicode_AsUTF8AndSize(name, &name_size);

    if (new_inst->gti_type.ht_type.tp_name == NULL ||
        PyType_Ready((PyTypeObject *)new_inst)) {
        goto error;
    }

    PyObject_GC_Track((PyObject *)new_inst);
    return (PyObject *)new_inst;
error:
    Py_DECREF(new_inst);
    return (PyObject *)new_inst;
}

PyObject *
_PyClassLoader_GetGenericInst(PyObject *type,
                              PyObject **args,
                              Py_ssize_t nargs)
{

    if (genericinst_cache == NULL) {
        genericinst_cache = PyDict_New();
        if (genericinst_cache == NULL) {
            return NULL;
        }
    }

    PyObject *key = gtd_make_key(type, args, nargs);
    if (key == NULL) {
        return NULL;
    }

    PyObject *inst = PyDict_GetItem(genericinst_cache, key);
    if (inst != NULL) {
        Py_DECREF(key);
        Py_INCREF(inst);
        return inst;
    }

    PyObject *res;
    if (!PyType_Check(type)) {
        Py_DECREF(key);
        PyErr_Format(
            PyExc_TypeError, "expected type, not %R", type);
        return NULL;
    } else if(((PyTypeObject *)type)->tp_flags & Py_TPFLAGS_GENERIC_TYPE_DEF) {
        if(gtd_validate_type(type, args, nargs)) {
            Py_DECREF(key);
            return NULL;
        }
        res = gtd_new_inst(type, args, nargs);
    } else {
        if (nargs == 1) {
            res = PyObject_GetItem(type, args[0]);
        } else {
            PyObject *argstuple = _PyTuple_FromArray(args, nargs);
            if (argstuple == NULL) {
                Py_DECREF(key);
                return NULL;
            }
            res = PyObject_GetItem(type, argstuple);
            Py_DECREF(argstuple);
        }
    }

    if (res == NULL || PyDict_SetItem(genericinst_cache, key, res)) {
        Py_XDECREF(res);
        Py_DECREF(key);
        return NULL;
    }
    Py_DECREF(key);
    return res;
}

PyObject *
_PyClassLoader_GtdGetItem(_PyGenericTypeDef *type, PyObject *args)
{
    assert(PyTuple_Check(args));
    if (PyTuple_GET_SIZE(args) != 1) {
        PyErr_SetString(PyExc_TypeError, "expected exactly one argument");
        return NULL;
    }
    args = PyTuple_GET_ITEM(args, 0);
    PyObject *res;
    if (PyTuple_Check(args)) {
        res = _PyClassLoader_GetGenericInst((PyObject *)type,
                                             ((PyTupleObject *)args)->ob_item,
                                             PyTuple_GET_SIZE(args));
    } else {
        res = _PyClassLoader_GetGenericInst((PyObject *)type, &args, 1);
    }
    if (res == NULL) {
        return NULL;
    }
    PyObject *mod;
    const char *base_name = ((PyTypeObject *)type)->tp_name;
    const char *s = strrchr(base_name, '.');
    _Py_IDENTIFIER(__module__);
    _Py_IDENTIFIER(builtins);

    if (s != NULL) {
        mod = PyUnicode_FromStringAndSize(
            base_name, (Py_ssize_t)(s - base_name));
        if (mod != NULL)
            PyUnicode_InternInPlace(&mod);
    }
    else {
        mod = _PyUnicode_FromId(&PyId_builtins);
        Py_XINCREF(mod);
    }
    if (mod == NULL) {
        Py_DECREF(res);
        return NULL;
    }
    if (_PyDict_SetItemId(((PyTypeObject *)res)->tp_dict, &PyId___module__, mod) == -1) {
        Py_DECREF(mod);
        Py_DECREF(res);
        return NULL;  // return NULL on errors
    }
    Py_DECREF(mod);

    return res;
}

#define GENINST_GET_PARAM(self, i)                                            \
    (((_PyGenericTypeInst *)Py_TYPE(self))->gti_inst[i].gtp_type)

void
_PyClassLoader_ArgError(const char *func_name,
                        int arg,
                        int type_param,
                        const _Py_SigElement *sig_elem,
                        PyObject *ctx)
{
    const char *expected = "?";
    int argtype = sig_elem->se_argtype;
    if (argtype & _Py_SIG_TYPE_PARAM) {
        expected = ((PyTypeObject *)GENINST_GET_PARAM(
                        ctx, _Py_SIG_TYPE_MASK(argtype)))
                       ->tp_name;

    } else {
        switch (_Py_SIG_TYPE_MASK(argtype)) {
        case _Py_SIG_OBJECT:
            PyErr_Format(PyExc_TypeError,
                         "%.200s() argument %d is missing",
                         func_name,
                         arg);
            return;
        case _Py_SIG_STRING:
            expected = "str";
            break;
        case _Py_SIG_SSIZE_T:
            expected = "int";
            break;
        }
    }

    PyErr_Format(PyExc_TypeError,
                 "%.200s() argument %d expected %s",
                 func_name,
                 arg,
                 expected);
}

const _Py_SigElement _Py_Sig_T0 = {_Py_SIG_TYPE_PARAM_IDX(0)};
const _Py_SigElement _Py_Sig_T1 = {_Py_SIG_TYPE_PARAM_IDX(1)};
const _Py_SigElement _Py_Sig_T0_Opt = {
    _Py_SIG_TYPE_PARAM_IDX(0) | _Py_SIG_OPTIONAL, Py_None};
const _Py_SigElement _Py_Sig_T1_Opt = {
    _Py_SIG_TYPE_PARAM_IDX(1) | _Py_SIG_OPTIONAL, Py_None};
const _Py_SigElement _Py_Sig_Object = {_Py_SIG_OBJECT};
const _Py_SigElement _Py_Sig_Object_Opt = {_Py_SIG_OBJECT | _Py_SIG_OPTIONAL,
                                           Py_None};
const _Py_SigElement _Py_Sig_String = {_Py_SIG_STRING};
const _Py_SigElement _Py_Sig_String_Opt = {_Py_SIG_STRING | _Py_SIG_OPTIONAL,
                                           Py_None};

const _Py_SigElement _Py_Sig_SSIZET = {_Py_SIG_SSIZE_T};
const _Py_SigElement _Py_Sig_SIZET = {_Py_SIG_SIZE_T};
const _Py_SigElement _Py_Sig_INT8 = {_Py_SIG_INT8};
const _Py_SigElement _Py_Sig_INT16 = {_Py_SIG_INT16};
const _Py_SigElement _Py_Sig_INT32 = {_Py_SIG_INT32};
const _Py_SigElement _Py_Sig_INT64 = {_Py_SIG_INT64};
const _Py_SigElement _Py_Sig_UINT8 = {_Py_SIG_UINT8};
const _Py_SigElement _Py_Sig_UINT16 = {_Py_SIG_UINT16};
const _Py_SigElement _Py_Sig_UINT32 = {_Py_SIG_UINT32};
const _Py_SigElement _Py_Sig_UINT64 = {_Py_SIG_UINT64};


static void
typedargsinfodealloc(_PyTypedArgsInfo *args_info)
{
    PyObject_GC_UnTrack((PyObject *)args_info);
    for (Py_ssize_t i = 0; i<Py_SIZE(args_info); i++) {
        Py_XDECREF(args_info->tai_args[i].tai_type);
    }
    PyObject_GC_Del((PyObject *)args_info);
}

static int
typedargsinfotraverse(_PyTypedArgsInfo *args_info, visitproc visit, void *arg)
{
    for (Py_ssize_t i = 0; i<Py_SIZE(args_info); i++) {
        Py_VISIT(args_info->tai_args[i].tai_type);
    }
    return 0;
}

static int
typedargsinfoclear(_PyTypedArgsInfo *args_info)
{
    for (Py_ssize_t i = 0; i<Py_SIZE(args_info); i++) {
        Py_CLEAR(args_info->tai_args[i].tai_type);
    }
    return 0;
}

PyTypeObject _PyTypedArgsInfo_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "typed_args_info",
    sizeof(_PyTypedArgsInfo),
    sizeof(_PyTypedArgsInfo),
    .tp_dealloc = (destructor)typedargsinfodealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
                Py_TPFLAGS_TUPLE_SUBCLASS, /* tp_flags */
    .tp_traverse = (traverseproc)typedargsinfotraverse,
    .tp_clear = (inquiry)typedargsinfoclear,
};

_PyTypedArgsInfo* _PyClassLoader_GetTypedArgsInfo(PyCodeObject *code, int only_primitives) {
    _Py_CODEUNIT* rawcode = code->co_rawcode;
    assert(
        _Py_OPCODE(rawcode[0]) == CHECK_ARGS);
    PyObject* checks = PyTuple_GET_ITEM(code->co_consts, _Py_OPARG(rawcode[0]));

    int count;
    if (only_primitives) {
        count = 0;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
            PyObject* type_descr = PyTuple_GET_ITEM(checks, i + 1);
            if (_PyClassLoader_ResolvePrimitiveType(type_descr) != TYPED_OBJECT) {
                count++;
            }
        }
    } else {
        count = PyTuple_GET_SIZE(checks) / 2;
    }

    _PyTypedArgsInfo *arg_checks = PyObject_GC_NewVar(_PyTypedArgsInfo, &_PyTypedArgsInfo_Type, count);
    if (arg_checks == NULL) {
        return NULL;
    }

    int checki = 0;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
        _PyTypedArgInfo* cur_check = &arg_checks->tai_args[checki];

        PyObject* type_descr = PyTuple_GET_ITEM(checks, i + 1);
        int optional;
        PyTypeObject* ref_type = _PyClassLoader_ResolveType(type_descr, &optional);
        if (ref_type == NULL) {
            return NULL;
        }
        int prim_type = _PyClassLoader_GetTypeCode(ref_type);

        if (prim_type == TYPED_BOOL) {
            cur_check->tai_type = &PyBool_Type;
            cur_check->tai_optional = 0;
            Py_INCREF(&PyBool_Type);
            Py_DECREF(ref_type);
        } else if (prim_type != TYPED_OBJECT) {
            assert(prim_type <= TYPED_INT64);
            cur_check->tai_type = &PyLong_Type;
            cur_check->tai_optional = 0;
            Py_INCREF(&PyLong_Type);
            Py_DECREF(ref_type);
        } else if (only_primitives) {
            Py_DECREF(ref_type);
            continue;
        } else {
            cur_check->tai_type = ref_type;
            cur_check->tai_optional = optional;
        }
        cur_check->tai_primitive_type = prim_type;
        cur_check->tai_argnum = PyLong_AsLong(PyTuple_GET_ITEM(checks, i));
        checki++;
    }
    return arg_checks;
}

int _PyClassLoader_HasPrimitiveArgs(PyCodeObject* code) {
  _Py_CODEUNIT* rawcode = code->co_rawcode;
  assert(_Py_OPCODE(rawcode[0]) == CHECK_ARGS);
  PyObject* checks = PyTuple_GET_ITEM(code->co_consts, _Py_OPARG(rawcode[0]));
  for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
    PyObject* type_descr = PyTuple_GET_ITEM(checks, i + 1);

    if (_PyClassLoader_ResolvePrimitiveType(type_descr) != TYPED_OBJECT) {
      return 1;
    }
  }
  return 0;
}
