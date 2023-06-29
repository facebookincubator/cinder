/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#include "Python.h"
#include "cinder/exports.h"
#include "classloader.h"
#include "descrobject.h"
#include "dictobject.h"
#include "object.h"
#include "opcode.h"
#include "pyerrors.h"
#include "pyport.h"
#include "structmember.h"
#include "Jit/pyjit.h"
#include "pycore_object.h"  // PyHeapType_CINDER_EXTRA
#include "pycore_tuple.h" // _PyTuple_FromArray
#include "pycore_unionobject.h"
#include <dlfcn.h>

static PyObject *classloader_cache;
static PyObject *classloader_cache_module_to_keys;
static PyObject *genericinst_cache;
static PyTypeObject *static_enum;

// This is a dict containing a mapping of lib name to "handle"
// as returned by `dlopen()`.
// Dict[str, int]
static PyObject *dlopen_cache;

// This is a dict containing a mapping of (lib_name, symbol_name) to
// the raw address as returned by `dlsym()`.
// Dict[Tuple[str, str], int]
static PyObject *dlsym_cache;


static void
vtabledealloc(_PyType_VTable *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->vt_slotmap);
    Py_XDECREF(op->vt_thunks);
    Py_XDECREF(op->vt_original);
    Py_XDECREF(op->vt_specials);

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
    Py_VISIT(op->vt_specials);
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
    Py_CLEAR(op->vt_specials);
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

static PyObject *
thunk_call(_Py_StaticThunk *thunk, PyObject *args, PyObject *kwds);

typedef struct {
    PyObject_HEAD;
    PyObject *propthunk_target;
    /* the vectorcall entry point for the thunk */
    vectorcallfunc propthunk_vectorcall;
} _Py_CachedPropertyThunk;


static int
cachedpropthunktraverse(_Py_CachedPropertyThunk *op, visitproc visit, void *arg)
{
    visit(op->propthunk_target, arg);
    return 0;
}

static int
cachedpropthunkclear(_Py_CachedPropertyThunk *op)
{
    Py_CLEAR(op->propthunk_target);
    return 0;
}

static void
cachedpropthunkdealloc(_Py_CachedPropertyThunk *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->propthunk_target);
    PyObject_GC_Del((PyObject *)op);
}

static PyObject *
cachedpropthunk_get(_Py_CachedPropertyThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames)
{
    size_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "cached property get expected 1 argument");
        return NULL;
    }

    descrgetfunc f = PyCachedPropertyWithDescr_Type.tp_descr_get;

    PyObject *res = f(thunk->propthunk_target, args[0], (PyObject *)Py_TYPE(args[0]));
    return res;
}


PyTypeObject _PyType_CachedPropertyThunk = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "cachedproperty_thunk",
    sizeof(_Py_CachedPropertyThunk),
    .tp_dealloc = (destructor)cachedpropthunkdealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
        _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_traverse = (traverseproc)cachedpropthunktraverse,
    .tp_clear = (inquiry)cachedpropthunkclear,
    .tp_vectorcall_offset = offsetof(_Py_CachedPropertyThunk, propthunk_vectorcall),
    .tp_call = (ternaryfunc)thunk_call,
};

static PyObject *
cachedpropthunk_get_func(PyObject *thunk) {
    assert(Py_TYPE(thunk) == &_PyType_CachedPropertyThunk);
    _Py_CachedPropertyThunk *t = (_Py_CachedPropertyThunk *)thunk;
    PyCachedPropertyDescrObject *descr = (PyCachedPropertyDescrObject *)t->propthunk_target;
    return descr->func;
}

typedef struct {
    PyObject_HEAD;
    PyObject *propthunk_target;
    /* the vectorcall entry point for the thunk */
    vectorcallfunc propthunk_vectorcall;
} _Py_AsyncCachedPropertyThunk;


static int
async_cachedpropthunktraverse(_Py_AsyncCachedPropertyThunk *op, visitproc visit, void *arg)
{
    visit(op->propthunk_target, arg);
    return 0;
}

static int
async_cachedpropthunkclear(_Py_AsyncCachedPropertyThunk *op)
{
    Py_CLEAR(op->propthunk_target);
    return 0;
}

static void
async_cachedpropthunkdealloc(_Py_AsyncCachedPropertyThunk *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->propthunk_target);
    PyObject_GC_Del((PyObject *)op);
}

static PyObject *
async_cachedpropthunk_get(_Py_AsyncCachedPropertyThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames)
{
    size_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "async cached property get expected 1 argument");
        return NULL;
    }

    descrgetfunc f = PyAsyncCachedPropertyWithDescr_Type.tp_descr_get;

    PyObject *res = f(thunk->propthunk_target, args[0], (PyObject *)Py_TYPE(args[0]));
    return res;
}


PyTypeObject _PyType_AsyncCachedPropertyThunk = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "async_cached_property_thunk",
    sizeof(_Py_AsyncCachedPropertyThunk),
    .tp_dealloc = (destructor)async_cachedpropthunkdealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
        _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_traverse = (traverseproc)async_cachedpropthunktraverse,
    .tp_clear = (inquiry)async_cachedpropthunkclear,
    .tp_vectorcall_offset = offsetof(_Py_AsyncCachedPropertyThunk, propthunk_vectorcall),
    .tp_call = (ternaryfunc)thunk_call,
};

static PyObject *
async_cachedpropthunk_get_func(PyObject *thunk) {
    assert(Py_TYPE(thunk) == &_PyType_AsyncCachedPropertyThunk);
    _Py_AsyncCachedPropertyThunk *t = (_Py_AsyncCachedPropertyThunk *)thunk;
    PyAsyncCachedPropertyDescrObject *descr = (PyAsyncCachedPropertyDescrObject *)t->propthunk_target;
    return descr->func;
}

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

static PyObject *
rettype_check(PyTypeObject *cls, PyObject *ret, _PyClassLoader_RetTypeInfo *rt_info);


int
used_in_vtable(PyObject *value);

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

PyObject *rettype_cb(_PyClassLoader_Awaitable *awaitable, PyObject *result) {
    if (result == NULL) {
        return NULL;
    }
    return rettype_check(Py_TYPE(awaitable), result, (_PyClassLoader_RetTypeInfo *)awaitable->state);
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

    if (overflow || !(_PyObject_TypeCheckOptional(ret, rt_info->rt_expected,
                                                  rt_info->rt_optional, rt_info->rt_exact))) {
        /* The override returned an incompatible value, report error */
        const char *msg;
        PyObject *exc_type = PyExc_TypeError;
        if (overflow) {
            exc_type = PyExc_OverflowError;
            msg = "unexpected return type from %s%s%U, expected %s, got out-of-range %s (%R)";
        } else if (rt_info->rt_optional) {
            msg = "unexpected return type from %s%s%U, expected Optional[%s], "
                  "got %s";
        } else {
            msg = "unexpected return type from %s%s%U, expected %s, got %s";
        }

        PyErr_Format(exc_type,
                     msg,
                     cls ? cls->tp_name : "",
                     cls ? "." : "",
                     classloader_get_func_name(rt_info->rt_name),
                     rt_info->rt_expected->tp_name,
                     Py_TYPE(ret)->tp_name,
                     ret);

        Py_DECREF(ret);
        return NULL;
    }
    return ret;
}

static _PyClassLoader_StaticCallReturn
return_to_native(PyObject *val, PyTypeObject *ret_type) {
    _PyClassLoader_StaticCallReturn ret;
    int type_code = _PyClassLoader_GetTypeCode(ret_type);
    if (val != NULL && type_code != TYPED_OBJECT) {
        ret.rax = (void *)_PyClassLoader_Unbox(val, type_code);
    } else {
        ret.rax = (void *)(uint64_t)val;
    }
    ret.rdx = (void *)(uint64_t)(val != NULL);
    return ret;
}

static const
_PyClassLoader_StaticCallReturn StaticError = {0, 0};

static int
hydrate_args(PyCodeObject *code, Py_ssize_t arg_count, void** args, PyObject **call_args, PyObject **free_args)
{
    _PyTypedArgsInfo *typed_arg_info = _PyClassLoader_GetTypedArgsInfo(code, 1);
    PyObject **extra_args = (PyObject **)args[5];
    for (Py_ssize_t i = 0, cur_arg = 0; i < arg_count; i++) {
        void *original;
        if (i < 5) {
            original = args[i]; // skip the v-table state
        } else {
            original = extra_args[i - 3];
        }

        if (cur_arg < Py_SIZE(typed_arg_info) &&
            typed_arg_info[cur_arg].tai_args[cur_arg].tai_argnum == i) {
            call_args[i] = _PyClassLoader_Box((uint64_t)original, typed_arg_info[cur_arg].tai_args[cur_arg].tai_primitive_type);
            if (call_args[i] == NULL) {
                for (Py_ssize_t free_arg = 0; free_arg < i; free_arg++) {
                    Py_CLEAR(free_args[free_arg]);
                }
                return -1;
            }
            free_args[i] = call_args[i];
            cur_arg++;
        } else {
            free_args[i] = NULL;
            call_args[i] = (PyObject *)original;
        }
    }
    return 0;
}

static void
free_hydrated_args(PyObject **free_args, Py_ssize_t arg_count) {
    for (Py_ssize_t i = 0; i < arg_count; i++) {
        Py_XDECREF(free_args[i]);
    }
}

static Py_ssize_t get_original_argcount(PyObject **callable);

PyTypeObject *
resolve_function_rettype(PyObject *funcobj,
                         int *optional,
                         int *exact,
                         int *coroutine);

_PyClassLoader_StaticCallReturn
invoke_from_native(PyObject *original, PyObject *func, void **args)
{
    PyCodeObject *code = (PyCodeObject *)((PyFunctionObject*)original)->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }

    PyObject *res = ((PyFunctionObject *)func)->vectorcall(func, call_args, arg_count, NULL);
    free_hydrated_args(free_args, arg_count);

    int optional, exact, coroutine;
    PyTypeObject *type = resolve_function_rettype(original, &optional, &exact, &coroutine);
    return return_to_native(res, type);

}

// Defines a helper thunk with the same layout as our JIT generated static
// entry points.  This has the static entry point at the start, and then 11
// instructions in we have the vectorcall entry point.  We install the static
// entry points into the v-table and can easily switch to the vector call
// form when we're invoking from the interpreter or somewhere that can't use
// the native calling convention.
#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#define VTABLE_THUNK(name)                                                          \
    __attribute__((naked)) void name##_dont_bolt(void) {  \
        __asm__(                                                                    \
                /* static_entry: */                                                 \
                /* we explicitly encode the jmp forward to static_entry_impl so */  \
                /* that we always get the 2 byte version.  The 0xEB is the jump, */ \
                /* the 14 is the length to jump, which is based upon the size of */ \
                /* jmp to the vectorcall entrypoint */                              \
                ".byte 0xEB\n"                                                      \
                ".byte 14\n"                                                        \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                "nop\n"                                                             \
                                                                                    \
                /* vector_entry: */                                                 \
                "jmp " #name "_vectorcall\n"                                        \
                                                                                    \
                /* static_entry_impl: */                                            \
                "push %rbp\n"                                                       \
                "mov %rsp, %rbp\n"                                                  \
                "push %rsp\n"                                                       \
                /* We want to push the arguments passed natively onto the stack */  \
                /* so that we can recover them in hydrate_args.  So we push them */ \
                /* onto the stack and then move the address of them into rsi */     \
                /* which will make them available as the 2nd argument.  Note we */  \
                /* don't need to push rdi as it's the state argument which we're */ \
                /* passing in anyway */                                             \
                "push %r9\n"                                                        \
                "push %r8\n"                                                        \
                "push %rcx\n"                                                       \
                "push %rdx\n"                                                       \
                "push %rsi\n"                                                       \
                "mov %rsp, %rsi\n"                                                  \
                "call " #name "_native\n"                                           \
                /* We don't know if we're returning a floating point value or not */\
                /* so we assume we are, and always populate the xmm registers */    \
                /* even if we don't need to */                                      \
                "movq %rax, %xmm0\n"                                                \
                "movq %rdx, %xmm1\n"                                                \
                "leave\n"                                                           \
                "ret\n"                                                             \
        );                                                                          \
    }
#else
#define VTABLE_THUNK(name)                                                          \
    PyObject *name##_dont_bolt(PyObject *state,                                     \
                    PyObject *const *args,                                          \
                    size_t nargsf) {                                                \
        return name##_vectorcall(state, args, nargsf);                              \
    }
#endif

__attribute__((__used__)) PyObject *
type_vtable_coroutine_property_vectorcall(_PyClassLoader_TypeCheckState *state,
                    PyObject *const*args,
                    size_t nargsf)
{

    PyObject *self = args[0];
    PyObject *descr = state->tcs_value;
    PyObject *name = state->tcs_rt.rt_name;
    PyObject *coro;
    int eager;

    /* we have to perform the descriptor checks at runtime because the
     * descriptor type can be modified preventing us from being able to have
     * more optimized fast paths */
    if (!PyDescr_IsData(descr)) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr != NULL) {
            PyObject *dict = *dictptr;
            if (dict != NULL) {
                coro = PyDict_GetItem(dict, PyTuple_GET_ITEM(name, 0));
                if (coro != NULL) {
                    Py_INCREF(coro);
                    eager = 0;
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

        coro =
            _PyObject_Vectorcall(get,
                                 args + 1,
                                 (nargs - 1),
                                 NULL);
        Py_DECREF(get);
    } else {
        coro = _PyObject_Vectorcall(descr, args, nargsf, NULL);
    }

    eager = Ci_PyWaitHandle_CheckExact(coro);
    if (eager) {
        Ci_PyWaitHandleObject *handle = (Ci_PyWaitHandleObject *)coro;
        if (handle->wh_waiter == NULL) {
            if (rettype_check(Py_TYPE(descr),
                    handle->wh_coro_or_result, (_PyClassLoader_RetTypeInfo *)state)) {
                return coro;
            }
            Ci_PyWaitHandle_Release(coro);
            return NULL;
        }
    }
done:
    return _PyClassLoader_NewAwaitableWrapper(coro, eager, (PyObject *)state, rettype_cb, NULL);
}

__attribute__((__used__)) PyObject *
type_vtable_classmethod_vectorcall(PyObject *state, PyObject *const*args, Py_ssize_t nargsf);


__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_coroutine_property_native(_PyClassLoader_TypeCheckState *state, void **args) {
    PyFunctionObject *original = (PyFunctionObject *)state->tcs_rt.rt_base.mt_original;

    PyCodeObject *code = (PyCodeObject *)original->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }
    _PyClassLoader_StaticCallReturn res;
    res.rax = type_vtable_coroutine_property_vectorcall(state, call_args, arg_count);
    res.rdx = (void *)(uint64_t)(res.rax != NULL);
    free_hydrated_args(free_args, arg_count);

    return res;
}

VTABLE_THUNK(type_vtable_coroutine_property)

__attribute__((__used__)) PyObject *
type_vtable_coroutine_classmethod_vectorcall(_PyClassLoader_TypeCheckState *state,
                       PyObject *const *args,
                       size_t nargsf)
{
    PyObject *callable = PyTuple_GET_ITEM(state->tcs_value, 0);
    PyObject *coro;
    Py_ssize_t awaited = nargsf & Ci_Py_AWAITED_CALL_MARKER;

    if (Py_TYPE(callable) == &PyClassMethod_Type) {
        // We need to do some special set up for class methods when invoking.
        coro = type_vtable_classmethod_vectorcall(state->tcs_value, args, nargsf);
    } else if (Py_TYPE(callable)->tp_descr_get != NULL) {
        PyObject *self = args[0];
        PyObject *get = Py_TYPE(callable)->tp_descr_get(
            callable, self, (PyObject *)Py_TYPE(self));
        if (get == NULL) {
            return NULL;
        }

        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

        coro =
            _PyObject_Vectorcall(get,
                                args + 1,
                                (nargs - 1),
                                NULL);
        Py_DECREF(get);
    } else {
        // In this case, we have a patched class method, and the self has been
        // handled via descriptors already.
        coro = _PyObject_Vectorcall(callable,
                                    args + 1,
                                    (PyVectorcall_NARGS(nargsf) - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET | awaited,
                                    NULL);
    }

    if (coro == NULL) {
        return NULL;
    }

    int eager = Ci_PyWaitHandle_CheckExact(coro);
    if (eager) {
        Ci_PyWaitHandleObject *handle = (Ci_PyWaitHandleObject *)coro;
        if (handle->wh_waiter == NULL) {
            if (rettype_check(Py_TYPE(callable),
                    handle->wh_coro_or_result, (_PyClassLoader_RetTypeInfo *)state)) {
                return coro;
            }
            Ci_PyWaitHandle_Release(coro);
            return NULL;
        }
    }

    return _PyClassLoader_NewAwaitableWrapper(coro, eager, (PyObject *)state, rettype_cb, NULL);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_coroutine_classmethod_native(_PyClassLoader_TypeCheckState *state, void **args, Py_ssize_t nargsf)
{
    PyObject *original = state->tcs_rt.rt_base.mt_original;
    assert(Py_TYPE(original) == &PyClassMethod_Type);
    PyFunctionObject *callable = (PyFunctionObject *)Ci_PyClassMethod_GetFunc(original);

    PyCodeObject *code = (PyCodeObject *)callable->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }

    int optional, exact, coroutine;
    PyTypeObject *type = resolve_function_rettype((PyObject *)callable, &optional, &exact, &coroutine);

    _PyClassLoader_StaticCallReturn res = return_to_native(type_vtable_coroutine_classmethod_vectorcall(state, call_args, arg_count), type);
    free_hydrated_args(free_args, arg_count);
    return res;
}

VTABLE_THUNK(type_vtable_coroutine_classmethod)

__attribute__((__used__)) PyObject *
type_vtable_coroutine_vectorcall(_PyClassLoader_TypeCheckState *state,
                       PyObject *const *args,
                       size_t nargsf)
{
    PyObject *coro;
    PyObject *callable = state->tcs_value;
    if (PyFunction_Check(callable)) {
        coro = _PyObject_Vectorcall(callable, args, nargsf, NULL);
    } else if (Py_TYPE(callable) == &PyClassMethod_Type) {
        // We need to do some special set up for class methods when invoking.
        callable = Ci_PyClassMethod_GetFunc(state->tcs_value);
        coro = _PyObject_Vectorcall(callable, args, nargsf, NULL);
    } else if (Py_TYPE(callable)->tp_descr_get != NULL) {
        PyObject *self = args[0];
        PyObject *get = Py_TYPE(callable)->tp_descr_get(
            callable, self, (PyObject *)Py_TYPE(self));
        if (get == NULL) {
            return NULL;
        }

        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

        coro =
            _PyObject_Vectorcall(get,
                                args + 1,
                                (nargs - 1),
                                NULL);
        Py_DECREF(get);
    } else {
        // self isn't passed if we're not a descriptor
        coro = _PyObject_Vectorcall(callable, args + 1, nargsf - 1, NULL);
    }
    if (coro == NULL) {
        return NULL;
    }

    int eager = Ci_PyWaitHandle_CheckExact(coro);
    if (eager) {
        Ci_PyWaitHandleObject *handle = (Ci_PyWaitHandleObject *)coro;
        if (handle->wh_waiter == NULL) {
            if (rettype_check(Py_TYPE(callable),
                    handle->wh_coro_or_result, (_PyClassLoader_RetTypeInfo *)state)) {
                return coro;
            }
            Ci_PyWaitHandle_Release(coro);
            return NULL;
        }
    }

    return _PyClassLoader_NewAwaitableWrapper(coro, eager, (PyObject *)state, rettype_cb, NULL);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_coroutine_native(_PyClassLoader_TypeCheckState *state, void **args) {
    PyFunctionObject *original = (PyFunctionObject *)state->tcs_rt.rt_base.mt_original;

    PyCodeObject *code = (PyCodeObject *)original->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }
    _PyClassLoader_StaticCallReturn res;
    res.rax = type_vtable_coroutine_vectorcall(state, call_args, arg_count);
    res.rdx = (void *)(uint64_t)(res.rax != NULL);
    free_hydrated_args(free_args, arg_count);
    return res;
}

VTABLE_THUNK(type_vtable_coroutine)


__attribute__((__used__)) PyObject *
type_vtable_nonfunc_property_vectorcall(_PyClassLoader_TypeCheckState *state,
                             PyObject **args,
                             size_t nargsf)
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
                res = PyDict_GetItem(dict, PyTuple_GET_ITEM(name, 0));
                if (res != NULL) {
                    Py_INCREF(res);
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
                                 NULL);
        Py_DECREF(get);
        goto done;
    }
    res = _PyObject_Vectorcall(descr, args, nargsf, NULL);
done:
    return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_nonfunc_property_native(_PyClassLoader_TypeCheckState *state, void **args) {
    PyFunctionObject *original = (PyFunctionObject *)state->tcs_rt.rt_base.mt_original;

    PyCodeObject *code = (PyCodeObject *)original->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }
    PyObject *obj = type_vtable_nonfunc_property_vectorcall(state, call_args, arg_count);
    free_hydrated_args(free_args, arg_count);
    return return_to_native(obj, ((_PyClassLoader_RetTypeInfo *)state)->rt_expected);
}

VTABLE_THUNK(type_vtable_nonfunc_property)

__attribute__((__used__)) PyObject *
type_vtable_nonfunc_vectorcall(_PyClassLoader_TypeCheckState *state,
                             PyObject **args,
                             size_t nargsf)
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
                    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
                    res = _PyObject_Vectorcall(value, args + 1, nargs - 1, NULL);
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
                                 NULL);
        Py_DECREF(get);
        goto done;
    }
    res = _PyObject_Vectorcall(descr, args + 1, nargsf - 1, NULL);
done:
    return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_nonfunc_native(_PyClassLoader_TypeCheckState *state, void **args)
{
    PyObject *original = state->tcs_rt.rt_base.mt_original;
    Py_ssize_t arg_count = get_original_argcount(&original);
    if (arg_count < 0) {
        return StaticError;
    }

    PyCodeObject *code = (PyCodeObject *)((PyFunctionObject *)original)->func_code;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }
    PyObject *obj = type_vtable_nonfunc_vectorcall(state, call_args, arg_count);
    free_hydrated_args(free_args, arg_count);
    return return_to_native(obj, ((_PyClassLoader_RetTypeInfo *)state)->rt_expected);
}

VTABLE_THUNK(type_vtable_nonfunc)

__attribute__((__used__)) PyObject *
vtable_static_function_vectorcall(PyObject *state, PyObject **args, Py_ssize_t nargsf)
{
    return _PyFunction_CallStatic((PyFunctionObject *)state, args, nargsf, NULL);
}


__attribute__((__used__)) _PyClassLoader_StaticCallReturn
vtable_static_function_native(PyObject *state, void **args) {
    return invoke_from_native(state, state, args);
}

VTABLE_THUNK(vtable_static_function)

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
vtable_arg_thunk_ret_primitive_non_jitted_native(PyObject *state, void **args) {
    PyFunctionObject *func = (PyFunctionObject *)PyTuple_GET_ITEM(state, 0);
    PyTypeObject *ret_type = (PyTypeObject *)PyTuple_GET_ITEM(state, 1);
    Py_ssize_t arg_count = ((PyCodeObject *)func->func_code)->co_argcount;

    _PyClassLoader_StaticCallReturn res;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args((PyCodeObject *)func->func_code, arg_count, args, call_args, free_args)) {
        res.rdx = NULL;
        return res;
    }

    PyObject *obj = func->vectorcall((PyObject *)func, call_args, arg_count, NULL);
    free_hydrated_args(free_args, arg_count);
    if (obj != NULL) {
        res.rax = (void *)_PyClassLoader_Unbox(obj, _PyClassLoader_GetTypeCode(ret_type));
    }
    res.rdx = (void *)(uint64_t)(obj != NULL);
    return res;
}

__attribute__((__used__)) PyObject *
vtable_arg_thunk_ret_primitive_non_jitted_vectorcall(PyObject *state, PyObject **args, Py_ssize_t nargsf)
{
    PyFunctionObject *func = (PyFunctionObject *)PyTuple_GET_ITEM(state, 0);

    return func->vectorcall((PyObject *)func, args, nargsf, NULL);
}

VTABLE_THUNK(vtable_arg_thunk_ret_primitive_non_jitted)

__attribute__((__used__)) void *
vtable_arg_thunk_vectorcall_only_vectorcall(PyObject *state, PyObject **args, Py_ssize_t nargsf) {
    return PyObject_Vectorcall(state, args, nargsf, NULL);
}

__attribute__((__used__)) void *
vtable_arg_thunk_vectorcall_only_native(PyObject *state, void **args) {
    PyErr_SetString(PyExc_RuntimeError, "unsupported native call");
    return NULL;
}

VTABLE_THUNK(vtable_arg_thunk_vectorcall_only)

PyObject *
_PyClassLoader_InvokeMethod(_PyType_VTable *vtable, Py_ssize_t slot, PyObject ** args, Py_ssize_t nargsf)
{
    vectorcallfunc func = JITRT_GET_NORMAL_ENTRY_FROM_STATIC(vtable->vt_entries[slot].vte_entry);
    PyObject *state = vtable->vt_entries[slot].vte_state;
    return func(state, args, nargsf, NULL);
}


__attribute__((__used__)) PyObject *
type_vtable_func_overridable_vectorcall(_PyClassLoader_TypeCheckState *state,
                             PyObject **args,
                             size_t nargsf)
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
                                       NULL);
            goto done;
        }
    }

    res = _PyObject_Vectorcall(state->tcs_value, (PyObject **)args, nargsf, NULL);

done:
    return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_func_overridable_native(_PyClassLoader_TypeCheckState *state, void **args) {
    PyFunctionObject *func = (PyFunctionObject *)((_PyClassLoader_MethodThunk *)state)->mt_original;
    Py_ssize_t arg_count = ((PyCodeObject *)func->func_code)->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args((PyCodeObject *)func->func_code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }

    PyObject *obj = type_vtable_func_overridable_vectorcall(state, call_args, ((PyCodeObject *)func->func_code)->co_argcount);
    free_hydrated_args(free_args, ((PyCodeObject *)func->func_code)->co_argcount);
    return return_to_native(obj, ((_PyClassLoader_RetTypeInfo *)state)->rt_expected);
}

VTABLE_THUNK(type_vtable_func_overridable)

static inline int
is_static_entry(vectorcallfunc func)
{
    return func == (vectorcallfunc)_PyFunction_Vectorcall;
}

void set_entry_from_func(_PyType_VTableEntry *entry, PyFunctionObject *func) {
    assert(_PyClassLoader_IsStaticFunction((PyObject *)func));
    if (is_static_entry(func->vectorcall)) {
        /* this will always be invoked statically via the v-table */
        entry->vte_entry = (vectorcallfunc)vtable_static_function_dont_bolt;
    } else {
        assert(_PyJIT_IsCompiled((PyObject *)func));
        entry->vte_entry = JITRT_GET_STATIC_ENTRY(func->vectorcall);
    }
}

PyObject *
_PyClassLoader_Box(uint64_t value, int primitive_type)
{
    PyObject* new_val;
    double dbl;
    switch (primitive_type) {
        case TYPED_BOOL:
            new_val = value ? Py_True : Py_False;
            Py_INCREF(new_val);
            break;
        case TYPED_INT8:
            new_val = PyLong_FromLong((int8_t)value);
            break;
        case TYPED_INT16:
            new_val = PyLong_FromLong((int16_t)value);
            break;
        case TYPED_INT32:
            new_val = PyLong_FromLong((int32_t)value);
            break;
        case TYPED_INT64:
            new_val = PyLong_FromSsize_t((Py_ssize_t)value);
            break;
        case TYPED_UINT8:
            new_val = PyLong_FromUnsignedLong((uint8_t)value);
            break;
        case TYPED_UINT16:
            new_val = PyLong_FromUnsignedLong((uint16_t)value);
            break;
        case TYPED_UINT32:
            new_val = PyLong_FromUnsignedLong((uint32_t)value);
            break;
        case TYPED_UINT64:
            new_val = PyLong_FromSize_t((size_t)value);
            break;
        case TYPED_DOUBLE:
            memcpy(&dbl, &value, sizeof(double));
            new_val = PyFloat_FromDouble(dbl);
            break;
        default:
            assert(0);
            PyErr_SetString(PyExc_RuntimeError, "unsupported primitive type");
            new_val = NULL;
            break;
    }
    return new_val;
}

uint64_t
_PyClassLoader_Unbox(PyObject *value, int primitive_type)
{
    uint64_t new_val;
    double res;
    switch (primitive_type) {
        case TYPED_BOOL:
            new_val = value == Py_True ? 1 : 0;
            break;
        case TYPED_INT8:
        case TYPED_INT16:
        case TYPED_INT32:
        case TYPED_INT64:
            new_val = (uint64_t)PyLong_AsLong(value);
            break;
        case TYPED_UINT8:
        case TYPED_UINT16:
        case TYPED_UINT32:
        case TYPED_UINT64:
            new_val = (uint64_t)PyLong_AsUnsignedLong(value);
            break;
        case TYPED_DOUBLE:
            res = PyFloat_AsDouble(value);
            memcpy(&new_val, &res, sizeof(double));
            break;
        default:
            assert(0);
            PyErr_SetString(PyExc_RuntimeError, "unsupported primitive type");
            new_val = 0;
            break;
    }
    return new_val;
}


/**
    This vectorcall entrypoint pulls out the function, slot index and replaces
    its own entrypoint in the v-table with optimized static vectorcall. (It also
    calls the underlying function and returns the value while doing so).
*/
__attribute__((__used__)) PyObject *
type_vtable_func_lazyinit_vectorcall(PyObject *state, PyObject **args, Py_ssize_t nargsf)
{
    /* state is (vtable, index, function) */
    _PyType_VTable *vtable = (_PyType_VTable *)PyTuple_GET_ITEM(state, 0);
    long index = PyLong_AS_LONG(PyTuple_GET_ITEM(state, 1));
    PyFunctionObject *func = (PyFunctionObject *)PyTuple_GET_ITEM(state, 2);

    PyObject *res = func->vectorcall((PyObject *)func, (PyObject **)args, nargsf, NULL);
    if (vtable->vt_entries[index].vte_state == state) {
        vtable->vt_entries[index].vte_state = (PyObject *)func;
        set_entry_from_func(&vtable->vt_entries[index], func);
        Py_INCREF(func);
        Py_DECREF(state);
    }
    return res;
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_func_lazyinit_native(PyObject *state, void **args)
{
    PyFunctionObject *func = (PyFunctionObject *)PyTuple_GET_ITEM(state, 2);
    PyCodeObject *code = (PyCodeObject *)((PyFunctionObject*)func)->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }

    PyObject *res = type_vtable_func_lazyinit_vectorcall(state, call_args, arg_count);
    free_hydrated_args(free_args, arg_count);
    int optional, exact, coroutine;
    PyTypeObject *type = resolve_function_rettype((PyObject *)func, &optional, &exact, &coroutine);
    return return_to_native(res, type);
}


VTABLE_THUNK(type_vtable_func_lazyinit)

__attribute__((__used__)) PyObject *
type_vtable_staticmethod_vectorcall(PyObject *method, PyObject **args, Py_ssize_t nargsf) {
    PyObject *func = Ci_PyStaticMethod_GetFunc(method);

    return _PyObject_Vectorcall(func, ((PyObject **)args) + 1, nargsf - 1, NULL);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_staticmethod_native(PyObject *method, void **args) {

    PyObject *func = Ci_PyStaticMethod_GetFunc(method);
    PyCodeObject *code = (PyCodeObject *)((PyFunctionObject*)func)->func_code;
    Py_ssize_t arg_count = code->co_argcount + 1; // hydrate self and then we'll drop it
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }

    PyObject *res = type_vtable_staticmethod_vectorcall(method, call_args, arg_count);
    free_hydrated_args(free_args, arg_count);
    int optional, exact, coroutine;
    PyTypeObject *type = resolve_function_rettype((PyObject *)func, &optional, &exact, &coroutine);
    return return_to_native(res, type);
}

VTABLE_THUNK(type_vtable_staticmethod)


__attribute__((__used__)) PyObject *
type_vtable_staticmethod_overridable_vectorcall(PyObject *thunk, PyObject **args, Py_ssize_t nargsf) {
    PyObject *method = ((_PyClassLoader_TypeCheckState *)thunk)->tcs_value;
    PyObject *func = Ci_PyStaticMethod_GetFunc(method);

    return _PyObject_Vectorcall(func, ((PyObject **)args) + 1, nargsf - 1, NULL);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_staticmethod_overridable_native(PyObject *thunk, void **args) {

    PyObject *original = Ci_PyStaticMethod_GetFunc(((_PyClassLoader_MethodThunk *)thunk)->mt_original);
    PyObject *method = ((_PyClassLoader_TypeCheckState *)thunk)->tcs_value;
    PyObject *func = Ci_PyStaticMethod_GetFunc(method);
    return invoke_from_native(original, func, args);
}


VTABLE_THUNK(type_vtable_staticmethod_overridable)

#define _PyClassMethod_Check(op) (Py_TYPE(op) == &PyClassMethod_Type)

__attribute__((__used__)) PyObject *
type_vtable_classmethod_vectorcall(PyObject *state, PyObject *const*args, Py_ssize_t nargsf)
{
    PyObject *classmethod = PyTuple_GET_ITEM(state, 0);
    PyTypeObject *decltype = (PyTypeObject *)PyTuple_GET_ITEM(state, 1);
    PyObject *func = Ci_PyClassMethod_GetFunc(classmethod);
    if (!PyObject_TypeCheck(args[0], decltype)) {
       return _PyObject_Vectorcall(func, args, nargsf, NULL);
    }

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    PyObject *stack[nargs];
    stack[0] = (PyObject *)Py_TYPE(args[0]);
    for(Py_ssize_t i = 1; i<nargs; i++) {
        stack[i] = args[i];
    }
    return _PyObject_Vectorcall(func, stack, nargsf, NULL);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_classmethod_native(PyObject *state, void **args)
{
    PyObject *classmethod = PyTuple_GET_ITEM(state, 0);
    PyTypeObject *decltype = (PyTypeObject *)PyTuple_GET_ITEM(state, 1);
    PyObject *func = Ci_PyClassMethod_GetFunc(classmethod);
    PyCodeObject *code = (PyCodeObject *)((PyFunctionObject*)func)->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }
    if (PyObject_TypeCheck(call_args[0], decltype)) {
        call_args[0] = (PyObject *)Py_TYPE(call_args[0]);
    }

    PyObject *res = ((PyFunctionObject *)func)->vectorcall(func, call_args, arg_count, NULL);
    free_hydrated_args(free_args, arg_count);

    int optional, exact, coroutine;
    PyTypeObject *type = resolve_function_rettype(func, &optional, &exact, &coroutine);
    return return_to_native(res, type);
}


VTABLE_THUNK(type_vtable_classmethod)


__attribute__((__used__)) PyObject *
type_vtable_classmethod_overridable_vectorcall(_PyClassLoader_TypeCheckState *state, PyObject **args, size_t nargsf)
{
    PyObject *clsmethod = PyTuple_GET_ITEM(state->tcs_value, 0);
    if (_PyClassMethod_Check(clsmethod)) {
        return type_vtable_classmethod_vectorcall(state->tcs_value, args, nargsf);
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
                                       NULL);
            return rettype_check(Py_TYPE(self), res, (_PyClassLoader_RetTypeInfo *)state);
        }
    }

    return _PyObject_Vectorcall(clsmethod, args, nargsf, NULL);
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_classmethod_overridable_native(_PyClassLoader_TypeCheckState *state, void **args) {

    PyObject *original = state->tcs_rt.rt_base.mt_original;
    PyFunctionObject *func = (PyFunctionObject *)Ci_PyClassMethod_GetFunc(original);

    PyCodeObject *code = (PyCodeObject *)func->func_code;
    Py_ssize_t arg_count = code->co_argcount;
    PyObject *call_args[arg_count];
    PyObject *free_args[arg_count];

    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
        return StaticError;
    }
    PyObject *obj = type_vtable_classmethod_overridable_vectorcall(state, call_args, arg_count);
    free_hydrated_args(free_args, arg_count);
    return return_to_native(obj, ((_PyClassLoader_RetTypeInfo *)state)->rt_expected);
}

VTABLE_THUNK(type_vtable_classmethod_overridable)

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_func_missing_native(PyObject *state, void **args) {
    PyFunctionObject *original = (PyFunctionObject *)PyTuple_GET_ITEM(state, 3);
    PyCodeObject *code = (PyCodeObject *)original->func_code;
    PyObject *call_args[code->co_argcount];
    PyObject *free_args[code->co_argcount];

    if (hydrate_args(code, code->co_argcount, args, call_args, free_args)) {
        return StaticError;
    }

    PyObject *self = call_args[0];
    PyObject *name = PyTuple_GET_ITEM(state, 0);
    PyErr_Format(PyExc_AttributeError,
                 "'%s' object has no attribute %R",
                 Py_TYPE(self)->tp_name,
                 name);
    free_hydrated_args(free_args, code->co_argcount);
    return StaticError;
}

__attribute__((__used__)) void *
type_vtable_func_missing_vectorcall(PyObject *state, PyObject **args, Py_ssize_t nargsf) {
    PyObject *self = args[0];
    PyObject *name = PyTuple_GET_ITEM(state, 0);
    PyErr_Format(PyExc_AttributeError,
                 "'%s' object has no attribute %R",
                 Py_TYPE(self)->tp_name,
                 name);
    return NULL;
}

VTABLE_THUNK(type_vtable_func_missing)

/**
    This does the initialization of the vectorcall entrypoint for the v-table for
    static functions. It'll set the entrypoint to type_vtable_func_lazyinit if
    the functions entry point hasn't yet been initialized.

    If it has been initialized and is being handled by the interpreter loop it'll go
    through the single _PyFunction_CallStatic entry point. Otherwise it'll just use
    the function entry point, which should be JITed.
*/
// TODO: Drop name argument
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
        int optional, exact, coroutine, classmethod;
        PyTypeObject *ret_type = (PyTypeObject *)_PyClassLoader_ResolveReturnType(value, &optional, &exact, &coroutine, &classmethod);
        int type_code = _PyClassLoader_GetTypeCode(ret_type);

        PyObject *state = PyTuple_New(type_code != TYPED_OBJECT ? 4 : 3);
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
        if (type_code != TYPED_OBJECT) {
            PyTuple_SET_ITEM(state, 3, (PyObject *)ret_type);
            Py_INCREF(ret_type);
        }
        Py_INCREF(value);
        Py_XDECREF(vtable->vt_entries[slot].vte_state);
        vtable->vt_entries[slot].vte_state = state;
        vtable->vt_entries[slot].vte_entry = (vectorcallfunc)type_vtable_func_lazyinit_dont_bolt;
    } else if(entry == (vectorcallfunc)_PyFunction_Vectorcall) {
        // non-JITed function, it could return a primitive in which case we need a stub to unbox the value.
        int optional, exact, coroutine, classmethod;
        PyTypeObject *ret_type = (PyTypeObject *)_PyClassLoader_ResolveReturnType(value, &optional, &exact, &coroutine, &classmethod);
        int type_code = _PyClassLoader_GetTypeCode(ret_type);

        if (type_code != TYPED_OBJECT) {
            PyObject *tuple = PyTuple_New(2);
            if (tuple == NULL) {
                return -1;
            }
            PyTuple_SET_ITEM(tuple, 0, value);
            Py_INCREF(value);
            PyTuple_SET_ITEM(tuple, 1, (PyObject *)ret_type);
            Py_INCREF(ret_type);
            vtable->vt_entries[slot].vte_state = tuple;
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)vtable_arg_thunk_ret_primitive_non_jitted_dont_bolt;
        } else {
            Py_XDECREF(vtable->vt_entries[slot].vte_state);
            vtable->vt_entries[slot].vte_state = value;
            set_entry_from_func(&vtable->vt_entries[slot], (PyFunctionObject *)value);
            Py_INCREF(value);
        }
    } else {
        Py_XDECREF(vtable->vt_entries[slot].vte_state);
        vtable->vt_entries[slot].vte_state = value;
        set_entry_from_func(&vtable->vt_entries[slot], (PyFunctionObject *)value);
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

typedef struct {
    PyObject_HEAD;
    PyObject *typed_descriptor_thunk_target;
    /* the vectorcall entry point for the thunk */
    vectorcallfunc typed_descriptor_thunk_vectorcall;
    int is_setter;
} _Py_TypedDescriptorThunk;


static int
typed_descriptor_thunk_traverse(_Py_TypedDescriptorThunk *op, visitproc visit, void *arg)
{
    visit(op->typed_descriptor_thunk_target, arg);
    return 0;
}

static int
typed_descriptor_thunk_clear(_Py_TypedDescriptorThunk *op)
{
    Py_CLEAR(op->typed_descriptor_thunk_target);
    return 0;
}

static void
typed_descriptor_thunk_dealloc(_Py_TypedDescriptorThunk *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->typed_descriptor_thunk_target);
    PyObject_GC_Del((PyObject *)op);
}

static PyObject *
typed_descriptor_thunk_get(_Py_TypedDescriptorThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames)
{
    size_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "typed descriptor get expected 1 argument");
        return NULL;
    }
    descrgetfunc f = _PyTypedDescriptorWithDefaultValue_Type.tp_descr_get;
    return f(thunk->typed_descriptor_thunk_target, args[0], (PyObject *)Py_TYPE(args[0]));
}

static PyObject *
typed_descriptor_thunk_set(_Py_TypedDescriptorThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames)
{
    size_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "typed descriptor set expected 2 arguments");
        return NULL;
    }

    descrsetfunc f = _PyTypedDescriptorWithDefaultValue_Type.tp_descr_set;

    int res = f(thunk->typed_descriptor_thunk_target, args[0], args[1]);
    if (res != 0) {
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

PyTypeObject _PyType_TypedDescriptorThunk = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "typed_descriptor_with_default_value_thunk",
    sizeof(_Py_TypedDescriptorThunk),
    .tp_dealloc = (destructor)typed_descriptor_thunk_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE |
        _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_traverse = (traverseproc)typed_descriptor_thunk_traverse,
    .tp_clear = (inquiry)typed_descriptor_thunk_clear,
    .tp_vectorcall_offset = offsetof(_Py_TypedDescriptorThunk, typed_descriptor_thunk_vectorcall),
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

static PyObject*
classloader_maybe_unwrap_callable(PyObject *func) {
    if (func != NULL) {
        PyObject *res;
        if (Py_TYPE(func) == &PyStaticMethod_Type) {
            res = Ci_PyStaticMethod_GetFunc(func);
            Py_INCREF(res);
            return res;
        }
        else if (Py_TYPE(func) == &PyClassMethod_Type) {
            res = Ci_PyClassMethod_GetFunc(func);
            Py_INCREF(res);
            return res;
        } else if (Py_TYPE(func) == &PyProperty_Type) {
            Ci_propertyobject* prop = (Ci_propertyobject*)func;
            // A "callable" usually refers to the read path
            res = prop->prop_get;
            Py_INCREF(res);
            return res;
        }
    }
    return NULL;
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

static
PyObject *
classloader_ensure_specials_cache(PyTypeObject *type) {
    _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(type, 0);
    if (vtable == NULL) {
        return NULL;
    }
    PyObject *specials = vtable->vt_specials;
    if (specials == NULL) {
        specials = vtable->vt_specials = PyDict_New();
        if (specials == NULL) {
            return NULL;
        }
    }

    return specials;
}

/* Stores a newly created special thunk in the special thunk cache.  If it fails
 * to store decref the thunk and return NULL */
static PyObject *
classloader_cache_new_special(PyTypeObject *type, PyObject *name, PyObject *special) {
    if (type == NULL) {
        return special;
    }
    PyObject *specials = classloader_ensure_specials_cache(type);
    if (specials == NULL) {
        return NULL;
    }

    if (PyDict_SetItem(specials, name, special)) {
        Py_DECREF(special);
        return NULL;
    }
    return special;
}

static PyObject *
classloader_get_property_fget(PyTypeObject *type, PyObject *name, PyObject *property) {
    if (Py_TYPE(property) == &PyProperty_Type) {
        PyObject *func = ((Ci_propertyobject *)property)->prop_get;
        if (func == NULL) {
            func = classloader_get_property_missing_fget();
        }
        Py_XINCREF(func);
        return func;
    } else if (Py_TYPE(property) == &PyCachedPropertyWithDescr_Type) {
        _Py_CachedPropertyThunk *thunk = PyObject_GC_New(_Py_CachedPropertyThunk, &_PyType_CachedPropertyThunk);
        if (thunk == NULL) {
            return NULL;
        }
        thunk->propthunk_vectorcall = (vectorcallfunc)cachedpropthunk_get;
        thunk->propthunk_target = property;
        Py_INCREF(property);
        return classloader_cache_new_special(type, name, (PyObject *)thunk);
    } else if (Py_TYPE(property) == &PyAsyncCachedPropertyWithDescr_Type) {
        _Py_AsyncCachedPropertyThunk *thunk = PyObject_GC_New(_Py_AsyncCachedPropertyThunk, &_PyType_AsyncCachedPropertyThunk);
        if (thunk == NULL) {
            return NULL;
        }
        thunk->propthunk_vectorcall = (vectorcallfunc)async_cachedpropthunk_get;
        thunk->propthunk_target = property;
        Py_INCREF(property);
        return classloader_cache_new_special(type, name, (PyObject *)thunk);
    } else if (Py_TYPE(property) == &_PyTypedDescriptorWithDefaultValue_Type) {
        _Py_TypedDescriptorThunk *thunk = PyObject_GC_New(_Py_TypedDescriptorThunk,
                                                          &_PyType_TypedDescriptorThunk);
        if (thunk == NULL) {
            return NULL;
        }
        Py_INCREF(property);
        thunk->typed_descriptor_thunk_target = property;
        thunk->typed_descriptor_thunk_vectorcall = (vectorcallfunc) typed_descriptor_thunk_get;
        thunk->is_setter = 0;
        return classloader_cache_new_special(type, name, (PyObject *)thunk);
    } else {
        _Py_PropertyThunk *thunk = PyObject_GC_New(_Py_PropertyThunk, &_PyType_PropertyThunk);
        if (thunk == NULL) {
            return NULL;
        }
        thunk->propthunk_vectorcall = (vectorcallfunc)propthunk_get;
        thunk->propthunk_target = property;
        Py_INCREF(property);
        return classloader_cache_new_special(type, name, (PyObject *)thunk);
    }
}

static PyObject *
classloader_get_property_fset(PyTypeObject *type, PyObject *name, PyObject *property) {
    if (Py_TYPE(property) == &PyProperty_Type) {
        PyObject *func = ((Ci_propertyobject *)property)->prop_set;
        if (func == NULL) {
            func = classloader_get_property_missing_fset();
        }
        Py_XINCREF(func);
        return func;
    } else if (Py_TYPE(property) == &PyCachedPropertyWithDescr_Type ||
               Py_TYPE(property) == &PyAsyncCachedPropertyWithDescr_Type) {
        PyObject *func = classloader_get_property_missing_fset();
        Py_XINCREF(func);
        return func;
    } else if (Py_TYPE(property) == &_PyTypedDescriptorWithDefaultValue_Type) {
        _Py_TypedDescriptorThunk *thunk = PyObject_GC_New(_Py_TypedDescriptorThunk,
                                                          &_PyType_TypedDescriptorThunk);
        if (thunk == NULL) {
            return NULL;
        }
        Py_INCREF(property);
        thunk->typed_descriptor_thunk_target = property;
        thunk->typed_descriptor_thunk_vectorcall = (vectorcallfunc) typed_descriptor_thunk_set;
        thunk->is_setter = 1;
        return classloader_cache_new_special(type, name, (PyObject *)thunk);
    } else {
        _Py_PropertyThunk *thunk = PyObject_GC_New(_Py_PropertyThunk, &_PyType_PropertyThunk);
        if (thunk == NULL) {
            return NULL;
        }
        thunk->propthunk_vectorcall = (vectorcallfunc)propthunk_set;
        thunk->propthunk_target = property;
        Py_INCREF(property);
        return classloader_cache_new_special(type, name, (PyObject *)thunk);
    }
}

static PyObject *
classloader_get_property_method(PyTypeObject *type, PyObject *property, PyTupleObject *name)
{
    PyObject *fname = PyTuple_GET_ITEM(name, 1);
    if (_PyUnicode_EqualToASCIIString(fname, "fget")) {
        return classloader_get_property_fget(type, (PyObject *)name, property);
    } else if (_PyUnicode_EqualToASCIIString(fname, "fset")) {
        return classloader_get_property_fset(type, (PyObject *)name, property);
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
                         int *exact,
                         int *coroutine) {
    assert(PyFunction_Check(funcobj));
    PyFunctionObject *func = (PyFunctionObject *)funcobj;
    if (((PyCodeObject *)func->func_code)->co_flags & CO_COROUTINE) {
        *coroutine = 1;
    }
    return _PyClassLoader_ResolveType(_PyClassLoader_GetReturnTypeDescr(func),
                                      optional, exact);
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
    visit(op->tcs_rt.rt_base.mt_original, arg);
    return 0;
}

static int
_PyClassLoader_TypeCheckState_clear(_PyClassLoader_TypeCheckState *op)
{
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_CLEAR(op->tcs_value);
    Py_CLEAR(op->tcs_rt.rt_base.mt_original);
    return 0;
}

static void
_PyClassLoader_TypeCheckState_dealloc(_PyClassLoader_TypeCheckState *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    rettype_check_clear((_PyClassLoader_RetTypeInfo *)op);
    Py_XDECREF(op->tcs_value);
    Py_XDECREF(op->tcs_rt.rt_base.mt_original);
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

static void
_PyClassLoader_MethodThunk_dealloc(_PyClassLoader_MethodThunk *op)
{
    PyObject_GC_UnTrack((PyObject *)op);
    Py_XDECREF(op->mt_original);
    PyObject_GC_Del((PyObject *)op);
}

static int
_PyClassLoader_MethodThunk_traverse(_PyClassLoader_MethodThunk *op, visitproc visit, void *arg)
{
    visit(op->mt_original, arg);
    return 0;
}

static int
_PyClassLoader_MethodThunk_clear(_PyClassLoader_MethodThunk *op)
{
    Py_CLEAR(op->mt_original);
    return 0;
}

PyTypeObject _PyType_MethodThunk = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "vtable_method_thunk",
    sizeof(_PyClassLoader_MethodThunk),
    .tp_dealloc = (destructor)_PyClassLoader_MethodThunk_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_traverse = (traverseproc)_PyClassLoader_MethodThunk_traverse,
    .tp_clear = (inquiry)_PyClassLoader_MethodThunk_clear,
};

static int
type_vtable_setslot_typecheck(PyTypeObject *decltype,
                              PyObject *ret_type,
                              int optional,
                              int exact,
                              int coroutine,
                              int classmethod,
                              PyObject *name,
                              _PyType_VTable *vtable,
                              Py_ssize_t slot,
                              PyObject *value,
                              PyObject *original)
{
    _PyClassLoader_TypeCheckState *state = PyObject_GC_New(
        _PyClassLoader_TypeCheckState, &_PyType_TypeCheckState);
    if (state == NULL) {
        return -1;
    }
    state->tcs_value = value;
    Py_INCREF(value);
    state->tcs_rt.rt_name = name;
    Py_INCREF(name);
    state->tcs_rt.rt_expected = (PyTypeObject *)ret_type;
    Py_INCREF(ret_type);
    state->tcs_rt.rt_optional = optional;
    state->tcs_rt.rt_exact = exact;
    state->tcs_rt.rt_base.mt_original = original;
    Py_INCREF(original);

    Py_XDECREF(vtable->vt_entries[slot].vte_state);
    vtable->vt_entries[slot].vte_state = (PyObject *)state;
    if (coroutine) {
        if (classmethod) {
            PyObject *tuple = PyTuple_New(2);
            if (tuple == NULL) {
                Py_DECREF(state);
                return -1;
            }
            PyTuple_SET_ITEM(tuple, 0, value);
            PyTuple_SET_ITEM(tuple, 1, (PyObject *)decltype);
            Py_INCREF(decltype);
            state->tcs_value = tuple;
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)type_vtable_coroutine_classmethod_dont_bolt;
        } else if (PyTuple_Check(name) && classloader_is_property_tuple((PyTupleObject *)name)) {
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)type_vtable_coroutine_property_dont_bolt;
        } else {
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)type_vtable_coroutine_dont_bolt;
        }
    } else if (PyFunction_Check(value)) {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_func_overridable_dont_bolt;
    } else if (PyTuple_Check(name) && classloader_is_property_tuple((PyTupleObject *)name)) {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_nonfunc_property_dont_bolt;
    } else if (classmethod) {
        PyObject *tuple = PyTuple_New(2);
        if (tuple == NULL) {
            Py_DECREF(state);
            return -1;
        }
        PyTuple_SET_ITEM(tuple, 0, value);
        PyTuple_SET_ITEM(tuple, 1, (PyObject *)decltype);
        Py_INCREF(decltype);
        state->tcs_value = tuple;
        vtable->vt_entries[slot].vte_entry = (vectorcallfunc)type_vtable_classmethod_overridable_dont_bolt;
    } else {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_nonfunc_dont_bolt;
    }
    return 0;
}

/**
    As the name suggests, this creates v-tables for all subclasses of the given type
    (recursively).
*/
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
    Py_VISIT(op->thunk_tcs.tcs_value);
    Py_VISIT((PyObject *)op->thunk_cls);
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


static void
set_thunk_type_error(_Py_StaticThunk *thunk, const char *msg) {
    PyObject *name = thunk->thunk_tcs.tcs_rt.rt_name;
    if (thunk->thunk_cls != NULL) {
        name = PyUnicode_FromFormat("%s.%U", thunk->thunk_cls->tp_name, name);
    }
    PyErr_Format(PyExc_TypeError, msg, name);
    if (thunk->thunk_cls != NULL) {
        Py_DECREF(name);
    }
}


PyObject *
thunk_vectorcall(_Py_StaticThunk *thunk, PyObject *const *args,
                                    size_t nargsf, PyObject *kwnames) {
    if (thunk->thunk_tcs.tcs_value == NULL) {
        set_thunk_type_error(thunk, "%U has been deleted");
        return NULL;
    }
    if (thunk->thunk_classmethod) {
        Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
        if (nargs == 0) {
            set_thunk_type_error(thunk, "%U must be invoked with >= 1 arguments");
            return NULL;
        }

        if (thunk->thunk_coroutine) {
          return type_vtable_coroutine_vectorcall((_PyClassLoader_TypeCheckState *)thunk, args,
                                       nargs);
        }
        PyObject *res = _PyObject_Vectorcall(thunk->thunk_tcs.tcs_value, args + 1, nargs - 1, kwnames);
        return rettype_check(thunk->thunk_cls, res, (_PyClassLoader_RetTypeInfo *)thunk);
    }

    if (thunk->thunk_coroutine) {
        PyObject *coro = _PyObject_Vectorcall(thunk->thunk_tcs.tcs_value, args, nargsf & ~Ci_Py_AWAITED_CALL_MARKER, kwnames);

        return _PyClassLoader_NewAwaitableWrapper(coro, 0, (PyObject *)thunk, rettype_cb, NULL);
    }

    PyObject *res = _PyObject_Vectorcall(thunk->thunk_tcs.tcs_value, args, nargsf & ~Ci_Py_AWAITED_CALL_MARKER, kwnames);
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
get_func_or_special_callable(PyTypeObject *type, PyObject *name, PyObject **result);

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
        if (get_func_or_special_callable(type, name, &clsitem)) {
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

static PyObject *
classloader_get_static_type(const char* name) {
    PyObject *mod = PyImport_ImportModule("__static__");
    if (mod == NULL) { return NULL; }
    PyObject *type = PyObject_GetAttrString(mod, name);
    Py_DECREF(mod);
    return type;
}

PyObject *
_PyClassLoader_ResolveReturnType(PyObject *func, int *optional, int *exact,
                                 int *coroutine, int *classmethod) {
    *coroutine = *optional = *classmethod = *exact = 0;
    PyTypeObject *res = NULL;
    if (PyFunction_Check(func)) {
        if (_PyClassLoader_IsStaticFunction(func)) {
            res = resolve_function_rettype(func, optional, exact, coroutine);
        } else {
            *optional = *exact = *coroutine = *classmethod = 0;
            res = &PyBaseObject_Type;
        }
    } else if (Py_TYPE(func) == &PyStaticMethod_Type) {
        PyObject *static_func = Ci_PyStaticMethod_GetFunc(func);
        if (_PyClassLoader_IsStaticFunction(static_func)) {
            res = resolve_function_rettype(static_func, optional, exact, coroutine);
        }
    } else if (Py_TYPE(func) == &PyClassMethod_Type) {
        PyObject *static_func = Ci_PyClassMethod_GetFunc(func);
        if (_PyClassLoader_IsStaticFunction(static_func)) {
            res = resolve_function_rettype(static_func, optional, exact, coroutine);
        }
        *classmethod = 1;
    } else if (Py_TYPE(func) == &PyProperty_Type) {
        Ci_propertyobject *property = (Ci_propertyobject *)func;
        PyObject *fget = property->prop_get;
        if (_PyClassLoader_IsStaticFunction(fget)) {
            res = resolve_function_rettype(fget, optional, exact, coroutine);
        }
    } else if (Py_TYPE(func) == &_PyType_CachedPropertyThunk) {
        PyObject *target = cachedpropthunk_get_func(func);
        if (_PyClassLoader_IsStaticFunction(target)) {
            res = resolve_function_rettype(target, optional, exact, coroutine);
        }
    }  else if (Py_TYPE(func) == &_PyType_AsyncCachedPropertyThunk) {
        PyObject *target = async_cachedpropthunk_get_func(func);
        if (_PyClassLoader_IsStaticFunction(target)) {
            res = resolve_function_rettype(target, optional, exact, coroutine);
        }
    } else if (Py_TYPE(func) == &PyCachedPropertyWithDescr_Type) {
        PyCachedPropertyDescrObject *property = (PyCachedPropertyDescrObject *)func;
        if (_PyClassLoader_IsStaticFunction(property->func)) {
            res = resolve_function_rettype(property->func, optional, exact, coroutine);
        }
    } else if (Py_TYPE(func) == &PyAsyncCachedPropertyWithDescr_Type) {
        PyAsyncCachedPropertyDescrObject *property = (PyAsyncCachedPropertyDescrObject *)func;
        if (_PyClassLoader_IsStaticFunction(property->func)) {
            res = resolve_function_rettype(property->func, optional, exact, coroutine);
        }
    } else if (Py_TYPE(func) == &_PyType_TypedDescriptorThunk) {
        _Py_TypedDescriptorThunk *thunk = (_Py_TypedDescriptorThunk *)func;
        if (thunk->is_setter) {
            res = &_PyNone_Type;
            Py_INCREF(res);
        } else {
            _PyTypedDescriptorWithDefaultValue *td = (_PyTypedDescriptorWithDefaultValue *)
                thunk->typed_descriptor_thunk_target;
            if (PyTuple_CheckExact(td->td_type)) {
                res = _PyClassLoader_ResolveType(td->td_type, &td->td_optional, &td->td_exact);
                *optional = td->td_optional;
                *exact = td->td_exact;
            } else { // Already resolved.
                assert(PyType_CheckExact(td->td_type));
                res = (PyTypeObject *) td->td_type;
                *optional = td->td_optional;
            }
            if (res == NULL) {
                return NULL;
            }
        }
    } else if (Py_TYPE(func) == &_PyTypedDescriptorWithDefaultValue_Type) {
        _PyTypedDescriptorWithDefaultValue *td = (_PyTypedDescriptorWithDefaultValue *) func;
        if (PyTuple_CheckExact(td->td_type)) {
            res = _PyClassLoader_ResolveType(td->td_type, &td->td_optional, &td->td_exact);
            *optional = td->td_optional;
            *exact = td->td_exact;
        } else { // Already resolved.
            assert(PyType_CheckExact(td->td_type));
            res = (PyTypeObject *) td->td_type;
            *optional = td->td_optional;
            *exact = td->td_exact;
        }
        if (res == NULL) {
            return NULL;
        }
    } else if (Py_TYPE(func) == &_PyType_StaticThunk) {
        _Py_StaticThunk* sthunk = (_Py_StaticThunk*)func;
        res = sthunk->thunk_tcs.tcs_rt.rt_expected;
        *optional = sthunk->thunk_tcs.tcs_rt.rt_optional;
        *exact = sthunk->thunk_tcs.tcs_rt.rt_exact;
        Py_INCREF(res);
    } else {
        Ci_PyTypedMethodDef *tmd = _PyClassLoader_GetTypedMethodDef(func);
        *optional = 0;
        if (tmd != NULL) {
            switch(tmd->tmd_ret) {
                case Ci_Py_SIG_VOID:
                case Ci_Py_SIG_ERROR: {
                    // The underlying C implementations of these functions don't
                    // produce a Python object at all, but we ensure (in
                    // _PyClassLoader_ConvertRet and in JIT HIR builder) that
                    // when we call them we produce a None.
                    *exact = 0;
                    res = (PyTypeObject *)&_PyNone_Type;
                    break;
                }
                case Ci_Py_SIG_STRING: {
                    *exact = 0;
                    res = &PyUnicode_Type;
                    break;
                }
                case Ci_Py_SIG_INT8: {
                    *exact = 1;
                    return classloader_get_static_type("int8");
                }
                case Ci_Py_SIG_INT16: {
                    *exact = 1;
                    return classloader_get_static_type("int16");
                }
                case Ci_Py_SIG_INT32: {
                    *exact = 1;
                    return classloader_get_static_type("int32");
                }
                case Ci_Py_SIG_INT64: {
                    *exact = 1;
                    return classloader_get_static_type("int64");
                }
                case Ci_Py_SIG_UINT8: {
                    *exact = 1;
                    return classloader_get_static_type("uint8");
                }
                case Ci_Py_SIG_UINT16: {
                    *exact = 1;
                    return classloader_get_static_type("uint16");
                }
                case Ci_Py_SIG_UINT32: {
                    *exact = 1;
                    return classloader_get_static_type("uint32");
                }
                case Ci_Py_SIG_UINT64: {
                    *exact = 1;
                    return classloader_get_static_type("uint64");
                }
                default: {
                    *exact = 0;
                    res = &PyBaseObject_Type;
                }
            }
            Py_INCREF(res);
        } else if (Py_TYPE(func) == &PyMethodDescr_Type) {
            // We emit invokes to untyped builtin methods; just assume they
            // return object.
            *exact = 0;
            res = &PyBaseObject_Type;
            Py_INCREF(res);
        }
    }
    return (PyObject *)res;
}

int
get_func_or_special_callable(PyTypeObject *type, PyObject *name, PyObject **result) {
    PyObject *dict = type->tp_dict;
    if (PyTuple_CheckExact(name)) {
        if (classloader_is_property_tuple((PyTupleObject *) name)) {
            _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
            if (vtable != NULL) {
                PyObject *specials = vtable->vt_specials;
                if (specials != NULL) {
                    *result = PyDict_GetItem(specials, name);
                    if (*result != NULL) {
                        Py_INCREF(*result);
                        return 0;
                    }
                }
            }

            PyObject *property = PyDict_GetItem(dict, PyTuple_GET_ITEM(name, 0));
            if (property == NULL) {
                *result = NULL;
                return 0;
            }
            *result = classloader_get_property_method(type, property, (PyTupleObject *) name);
            if (*result == NULL) {
                return -1;
            }
            return 0;
        }
    }
    *result = PyDict_GetItem(dict, name);
    Py_XINCREF(*result);
    return 0;
}

int
_PyClassLoader_IsPatchedThunk(PyObject *obj)
{
    if (obj != NULL && Py_TYPE(obj) == &_PyType_StaticThunk) {
        return 1;
    }
    return 0;
}


int
is_static_type(PyTypeObject *type);


/*
    Looks up through parent classes to find a member specified by the name. If a parent class attribute
    has been patched, that is ignored, i.e it goes through the originally defined members.
*/
int
_PyClassLoader_GetStaticallyInheritedMember(PyTypeObject *type, PyObject *name, PyObject **result) {
    PyObject *mro = type->tp_mro, *base;

    for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(mro); i++) {
        PyTypeObject *next = (PyTypeObject *)PyTuple_GET_ITEM(type->tp_mro, i);
        if (!is_static_type(next)) {
            continue;
        }
        if (next->tp_cache != NULL &&
                ((_PyType_VTable *)next->tp_cache)->vt_original != NULL) {
            /* if we've initialized originals it contains all of our possible slot values
             * including special callables. */
            base = PyDict_GetItem(((_PyType_VTable *)next->tp_cache)->vt_original, name);
            if (base == NULL) {
                continue;
            }
            assert(used_in_vtable(base));
            Py_INCREF(base);
            *result = base;
            return 0;
        } else if (next->tp_dict == NULL) {
            continue;
        } else if (get_func_or_special_callable(next, name, &base)) {
            return -1;
        }

        if (base != NULL) {
            *result = base;
            return 0;
        }
    }
    *result = NULL;
    return 0;
}

static PyObject *g_fget = NULL;
static PyObject *g_fset = NULL;

PyObject *get_descr_tuple(PyObject *name, PyObject *accessor)
{
    PyObject *getter_tuple = PyTuple_New(2);
    Py_INCREF(name);
    PyTuple_SET_ITEM(getter_tuple, 0, name);
    Py_INCREF(accessor);
    PyTuple_SET_ITEM(getter_tuple, 1, accessor);
    return getter_tuple;
}

PyObject *
get_property_getter_descr_tuple(PyObject *name)
{
    if (g_fget == NULL) {
        g_fget = PyUnicode_FromStringAndSize("fget", 4);
    }
    return get_descr_tuple(name, g_fget);
}

PyObject *
get_property_setter_descr_tuple(PyObject *name)
{
    if (g_fset == NULL) {
        g_fset = PyUnicode_FromStringAndSize("fset", 4);
    }
    return get_descr_tuple(name, g_fset);
}

static void
update_thunk(_Py_StaticThunk *thunk, PyObject *previous, PyObject *new_value)
{
    Py_CLEAR(thunk->thunk_tcs.tcs_value);
    if (new_value != NULL) {
        thunk->thunk_tcs.tcs_value = new_value;
        Py_INCREF(new_value);
    }
    PyObject *funcref;
    if (new_value == previous) {
        funcref = previous;
    } else {
        funcref = (PyObject *)thunk;
    }
    PyObject *unwrapped = classloader_maybe_unwrap_callable(funcref);
    if (unwrapped != NULL) {
        thunk->thunk_funcref = unwrapped;
        Py_DECREF(unwrapped);
    } else {
        thunk->thunk_funcref = funcref;
    }
}


/* Static types have a slot containing all final methods in their inheritance chain. This function
   returns the contents of that slot by looking up the MRO, if it exists.
 */
static PyObject *
get_final_method_names(PyTypeObject *type)
{
    PyObject *mro = type->tp_mro;
    if (mro == NULL) {
        return NULL;
    }
    Py_ssize_t n = PyTuple_GET_SIZE(mro);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *mro_type = PyTuple_GET_ITEM(mro, i);
        if (((PyTypeObject *)mro_type)->tp_flags & Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED) {
            _Py_IDENTIFIER(__final_method_names__);
            PyObject *final_method_names_string = _PyUnicode_FromId(&PyId___final_method_names__);
            PyObject *final_method_names = _PyObject_GenericGetAttrWithDict(mro_type,
                                                                            final_method_names_string,
                                                                            /*dict=*/NULL,
                                                                            /*suppress=*/1);
            return final_method_names;
        }
    }
    return NULL;
}

int _PyClassLoader_IsFinalMethodOverridden(PyTypeObject *base_type, PyObject *members_dict)
{
    PyObject *final_method_names = get_final_method_names(base_type);
    if (final_method_names == NULL) {
        return 0;
    }
    if (!PyTuple_Check(final_method_names)) {
        PyErr_Format(PyExc_TypeError,
                     "The __final_method_names__ slot for type %R is not a tuple.",
                     final_method_names);
        Py_DECREF(final_method_names);
        return -1;
    }
    Py_ssize_t member_pos = 0;
    PyObject *key, *value;
    while (PyDict_Next(members_dict, &member_pos, &key, &value)) {
        for (Py_ssize_t final_method_index = 0;
             final_method_index < PyTuple_GET_SIZE(final_method_names);
             final_method_index++) {
            PyObject *current_final_method_name = PyTuple_GET_ITEM(final_method_names,
                                                                   final_method_index);
            int compare_result = PyUnicode_Compare(key, current_final_method_name);
            if (compare_result == 0) {
                PyErr_Format(PyExc_TypeError,
                             "%R overrides a final method in the static base class %R",
                             key,
                             base_type);
                Py_DECREF(final_method_names);
                return -1;
            } else if (compare_result == -1 && PyErr_Occurred()) {
                return -1;
            }
        }
    }
    Py_DECREF(final_method_names);
    return 0;
}

static int
check_if_final_method_overridden(PyTypeObject *type, PyObject *name)
{

    PyTypeObject *base_type = type->tp_base;
    if (base_type == NULL) {
        return 0;
    }
    PyObject *final_method_names = get_final_method_names(base_type);
    if (final_method_names == NULL) {
        return 0;
    }
    if (!PyTuple_Check(final_method_names)) {
        PyErr_Format(PyExc_TypeError,
                     "The __final_method_names__ slot for type %R is not a tuple.",
                     final_method_names);
        Py_DECREF(final_method_names);
        return -1;
    }
    for (Py_ssize_t final_method_index = 0;
         final_method_index < PyTuple_GET_SIZE(final_method_names);
         final_method_index++) {
        PyObject *current_final_method_name = PyTuple_GET_ITEM(final_method_names,
                                                               final_method_index);
        int compare_result = PyUnicode_Compare(name, current_final_method_name);
        if (compare_result == 0) {
            PyErr_Format(PyExc_TypeError,
                         "%R overrides a final method in the static base class %R",
                         name,
                         base_type);
            Py_DECREF(final_method_names);
            return -1;
        } else if (compare_result == -1 && PyErr_Occurred()) {
            Py_DECREF(final_method_names);
            return -1;
        }
    }
    Py_DECREF(final_method_names);
    return 0;
}

/* UpdateModuleName will be called on any patching of a name in a StrictModule. */
int
_PyClassLoader_UpdateModuleName(PyStrictModuleObject *mod,
                                PyObject *name,
                                PyObject *new_value)
{
    if (mod->static_thunks != NULL) {
        _Py_StaticThunk *thunk = (_Py_StaticThunk *)PyDict_GetItem(mod->static_thunks, name);
        if (thunk != NULL) {
            PyObject *previous = PyDict_GetItem(mod->originals, name);
            update_thunk(thunk, previous, new_value);
        }
    }
    return 0;
}

int populate_getter_and_setter(PyTypeObject *type,
                               PyObject *name,
                               PyObject *new_value)
{
    PyObject *getter_value = new_value == NULL ? NULL : classloader_get_property_fget(type, name, new_value);
    PyObject *setter_value = new_value == NULL ? NULL : classloader_get_property_fset(type, name, new_value);

    PyObject *getter_tuple = get_property_getter_descr_tuple(name);
    PyObject *setter_tuple = get_property_setter_descr_tuple(name);

    int result = 0;
    if (_PyClassLoader_UpdateSlot(type, (PyObject *)getter_tuple, getter_value)) {
        result = -1;
    }
    Py_DECREF(getter_tuple);
    Py_XDECREF(getter_value);

    if (_PyClassLoader_UpdateSlot(type, (PyObject *)setter_tuple, setter_value)) {
        result = -1;
    }
    Py_DECREF(setter_tuple);
    Py_XDECREF(setter_value);

    return result;
}

static int
classloader_get_original_static_def(PyTypeObject *tp, PyObject *name, PyObject **original)
{
    _PyType_VTable *vtable = (_PyType_VTable *)tp->tp_cache;
    *original = NULL;
    if (is_static_type(tp)) {
        if (vtable->vt_original != NULL) {
            *original = PyDict_GetItem(vtable->vt_original, name);
            if (*original != NULL) {
                Py_INCREF(*original);
                return 0;
            }
        } else if (get_func_or_special_callable(tp, name, original)) {
            return -1;
        }
        // If a static type has a non-static member (for instance, due to having a decorated method)
        // we need to keep looking up the MRO for a static base.
        if (*original == NULL || !used_in_vtable(*original)) {
            Py_CLEAR(*original);
        }
    }

    if (*original == NULL) {
        // The member was actually defined in one of the parent classes, so try to look it up from there.
        // TODO: It might be possible to avoid the type-check in this situation, because while `tp` was patched,
        // the parent Static classes may not be.
        if (_PyClassLoader_GetStaticallyInheritedMember(tp, name, original)) {
            return -1;
        }
    }
    return 0;
}

static int
type_vtable_setslot(PyTypeObject *tp,
                    PyObject *name,
                    Py_ssize_t slot,
                    PyObject *value,
                    PyObject *original);

/* The UpdateSlot method will always get called by `tp_setattro` when one of a type's attribute
   gets changed, and serves as an entry point for handling modifications to vtables. */
int
_PyClassLoader_UpdateSlot(PyTypeObject *type,
                          PyObject *name,
                          PyObject *new_value)
{
    /* This check needs to be happen before we look into the vtable, as non-static subclasses of
       static classes won't necessarily have vtables already constructed. */
    if (check_if_final_method_overridden(type, name)) {
        return -1;
    }
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    if (vtable == NULL) {
        return 0;
    }

    PyObject *slotmap = vtable->vt_slotmap;
    PyObject *slot = PyDict_GetItem(slotmap, name);
    if (slot == NULL) {
        return 0;
    }

    PyObject *original;
    if (classloader_get_original_static_def(type, name, &original)) {
        return -1;
    }

    /* we need to search in the MRO if we don't contain the
     * item directly or we're currently deleting the current value */
    if (new_value == NULL) {
        /* We need to look for an item explicitly declared in our parent if we're inheriting.
         * Note we don't care about static vs non-static, and we don't want to look at the
         * original values either.  The new value is simply whatever the currently inherited value
         * is. */
        PyObject *mro = type->tp_mro;

        for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(mro); i++) {
            PyTypeObject *next = (PyTypeObject *)PyTuple_GET_ITEM(type->tp_mro, i);
            if (next->tp_dict == NULL) {
                continue;
            }
            new_value = PyDict_GetItem(next->tp_dict, name);
            if (new_value != NULL) {
                break;
            }
        }
    }

    /* update the value that exists in our thunks for performing indirections
     * necessary for patched INVOKE_FUNCTION calls */
    if (vtable->vt_thunks != NULL) {
        _Py_StaticThunk *thunk = (_Py_StaticThunk *)PyDict_GetItem(vtable->vt_thunks, name);
        if (thunk != NULL) {
            update_thunk(thunk, original, new_value);
        }
    }

    assert(original != NULL);

    int cur_optional = 0, cur_exact = 0, cur_coroutine = 0, cur_classmethod = 0;
    PyObject *cur_type = _PyClassLoader_ResolveReturnType(original, &cur_optional, &cur_exact,
                                                          &cur_coroutine, &cur_classmethod);
    assert(cur_type != NULL);

    // if this is a property slot, also update the getter and setter slots
    if (Py_TYPE(original) == &PyProperty_Type ||
        Py_TYPE(original) == &PyCachedPropertyWithDescr_Type ||
        Py_TYPE(original) == &PyAsyncCachedPropertyWithDescr_Type ||
        Py_TYPE(original) == &_PyTypedDescriptorWithDefaultValue_Type) {
        if (new_value) {
            // If we have a new value, and it's not a descriptor, we can type-check it
            // at the time of assignment.
            PyTypeObject *new_value_type = Py_TYPE(new_value);
            if (new_value_type->tp_descr_get == NULL &&
                !_PyObject_TypeCheckOptional(new_value, (PyTypeObject *) cur_type, cur_optional, cur_exact)) {
                PyErr_Format(
                        PyExc_TypeError, "Cannot assign a %s, because %s.%U is expected to be a %s",
                        Py_TYPE(new_value)->tp_name,
                        type->tp_name, name,
                        ((PyTypeObject*) cur_type)->tp_name
                );
                Py_DECREF(cur_type);
                Py_DECREF(original);
                return -1;
            }
        }
        if (populate_getter_and_setter(type, name, new_value) < 0) {
            Py_DECREF(original);
            return -1;
        }
    }
    Py_DECREF(cur_type);

    Py_ssize_t index = PyLong_AsSsize_t(slot);

    if (type_vtable_setslot(type, name, index, new_value, original)) {
        Py_DECREF(original);
        return -1;
    }

    Py_DECREF(original);

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

/**
    Sets the vtable slot entry for the given method name to the correct type of vectorcall.
    We specialize where possible, but also have a generic fallback which checks whether the
    actual return type matches the declared one (if any).
*/
static int
type_vtable_setslot(PyTypeObject *tp,
                    PyObject *name,
                    Py_ssize_t slot,
                    PyObject *value,
                    PyObject *original)
{
    _PyType_VTable *vtable = (_PyType_VTable *)tp->tp_cache;
    assert(original != NULL);

    if (original == value) {
        if (tp->tp_dictoffset == 0) {
            // These cases mean that the type instances don't have a __dict__ slot,
            // meaning our compile time type-checks are valid (nothing's been patched)
            // meaning we can omit return type checks at runtime.
            if (_PyClassLoader_IsStaticFunction(value)) {
                return type_vtable_set_opt_slot(tp, name, vtable, slot, value);
            } else if (Py_TYPE(value) == &PyStaticMethod_Type &&
                    _PyClassLoader_IsStaticFunction(Ci_PyStaticMethod_GetFunc(value))) {
                Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
                vtable->vt_entries[slot].vte_entry = (vectorcallfunc)&type_vtable_staticmethod_dont_bolt;
                Py_INCREF(value);
                return 0;
            } else if (Py_TYPE(value) == &PyClassMethod_Type &&
                    _PyClassLoader_IsStaticFunction(Ci_PyClassMethod_GetFunc(value))) {
                PyObject *tuple = PyTuple_New(2);
                if (tuple == NULL) {
                    return -1;
                }
                PyTuple_SET_ITEM(tuple, 0, value);
                PyTuple_SET_ITEM(tuple, 1, (PyObject *)tp);
                Py_INCREF(tp);
                Py_XSETREF(vtable->vt_entries[slot].vte_state, tuple);
                vtable->vt_entries[slot].vte_entry = (vectorcallfunc)type_vtable_classmethod_dont_bolt;
                Py_INCREF(value);
                return 0;
            } else if (Py_TYPE(value) == &PyMethodDescr_Type) {
                Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
                vtable->vt_entries[slot].vte_entry =
                    (vectorcallfunc)&vtable_arg_thunk_vectorcall_only_dont_bolt;
                Py_INCREF(value);
                return 0;
            }
        }

        if (Py_TYPE(value) == &_PyType_CachedPropertyThunk) {
            Py_XSETREF(vtable->vt_entries[slot].vte_state, value); // cachedpropthunk_get
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)vtable_arg_thunk_vectorcall_only_dont_bolt;
            Py_INCREF(value);
            return 0;
        } else if (Py_TYPE(value) == &_PyType_TypedDescriptorThunk) {
            Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
            vtable->vt_entries[slot].vte_entry =
                (vectorcallfunc)vtable_arg_thunk_vectorcall_only_dont_bolt;
            Py_INCREF(value);
            return 0;
        }
    }

    int optional = 0, exact = 0, coroutine = 0, classmethod = 0;
    PyObject *ret_type = _PyClassLoader_ResolveReturnType(original, &optional, &exact, &coroutine, &classmethod);

    if (ret_type == NULL) {
        PyErr_Format(PyExc_RuntimeError,
                    "missing type annotation on static compiled method %R of %s",
                    name, tp->tp_name);
        return -1;
    }

    if (value == NULL) {
        PyObject *missing_state = PyTuple_New(4);
        if (missing_state == NULL) {
            Py_DECREF(ret_type);
            return -1;
        }

        PyObject *func_name = classloader_get_func_name(name);
        PyTuple_SET_ITEM(missing_state, 0, func_name);
        PyTuple_SET_ITEM(missing_state, 1, (PyObject *)tp);
        PyObject *optional_obj = optional ? Py_True : Py_False;
        PyTuple_SET_ITEM(missing_state, 2, optional_obj);
        PyTuple_SET_ITEM(missing_state, 3, original);
        Py_INCREF(func_name);
        Py_INCREF(tp);
        Py_INCREF(optional_obj);
        Py_INCREF(original);

        Py_XDECREF(vtable->vt_entries[slot].vte_state);
        vtable->vt_entries[slot].vte_state = missing_state;
        vtable->vt_entries[slot].vte_entry = (vectorcallfunc)type_vtable_func_missing_dont_bolt;
        Py_DECREF(ret_type);
        return 0;
    }

    int res = type_vtable_setslot_typecheck(
         tp, ret_type, optional, exact, coroutine, classmethod, name, vtable, slot, value, original);
    Py_DECREF(ret_type);
    return res;
}

static Py_ssize_t
get_original_argcount(PyObject **callable)
{
    Py_ssize_t arg_count;
    PyObject *original = *callable;
    if (!PyFunction_Check(original)) {
        if (_PyClassMethod_Check(original)) {
            *callable = Ci_PyClassMethod_GetFunc(original);
            if (!PyFunction_Check(*callable)) {
                PyErr_SetString(PyExc_RuntimeError, "Not a function in a class method");
                return -1;
            }
            arg_count = ((PyCodeObject *)((PyFunctionObject*)*callable)->func_code)->co_argcount;
        } else if(Py_TYPE(original) == &PyStaticMethod_Type) {
            *callable = Ci_PyStaticMethod_GetFunc(original);
            if (!PyFunction_Check(*callable)) {
                PyErr_SetString(PyExc_RuntimeError, "Not a function in a class method");
                return -1;
            }
            // static method doesn't take self, but it's passed as an argument in an
            // INVOKE_METHOD.
            arg_count = ((PyCodeObject *)((PyFunctionObject*)*callable)->func_code)->co_argcount + 1;
        } else if (Py_TYPE(original) == &_PyType_TypedDescriptorThunk) {
            // TODO: Test setter case?
            if (((_Py_TypedDescriptorThunk *)original)->is_setter) {
                arg_count = 2;
            } else {
                arg_count = 1;
            }
            *callable = ((_Py_TypedDescriptorThunk *)original)->typed_descriptor_thunk_target;
        } else {
            PyErr_Format(PyExc_RuntimeError, "Not a function: %R", original);
            return -1;
        }
    } else {
        arg_count = ((PyCodeObject *)((PyFunctionObject*)*callable)->func_code)->co_argcount;
    }
    return arg_count;
}

/**
    This is usually what we use as the initial entrypoint in v-tables. Then,
    when a method is called, this traverses the MRO, finds the correct callable,
    and updates the vtable entry with the correct one (and then calls the
    callable). All following method invokes directly hit the actual callable,
    because the v-table has been updated.
*/
static _PyClassLoader_StaticCallReturn
type_vtable_lazyinit_impl(PyObject *info,
                     void **args, Py_ssize_t nargsf, int is_native) {
    PyTypeObject *type = (PyTypeObject *)PyTuple_GET_ITEM(info, 1);
    PyObject *name = PyTuple_GET_ITEM(info, 0);
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    PyObject *mro = type->tp_mro;
    Py_ssize_t slot =
        PyLong_AsSsize_t(PyDict_GetItem(vtable->vt_slotmap, name));

    assert(vtable != NULL);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        PyObject *value = NULL;
        PyTypeObject *cur_type = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        if (get_func_or_special_callable(cur_type, name, &value)) {
            return StaticError;
        }
        if (value != NULL) {
            PyObject *original = NULL;
            if (classloader_get_original_static_def(type, name, &original)) {
                Py_DECREF(value);
                return StaticError;
            }
            if (type_vtable_setslot(type, name, slot, value, original)) {
                Py_XDECREF(original);
                Py_DECREF(value);
                return StaticError;
            }

            _PyClassLoader_StaticCallReturn res;
            if (is_native) {
                PyObject *callable = original;
                Py_ssize_t arg_count = get_original_argcount(&callable);
                if (arg_count < 0) {
                    return StaticError;
                }

                PyObject *obj_res;
                if (PyFunction_Check(callable)) {
                    PyCodeObject *code = (PyCodeObject *)((PyFunctionObject*)callable)->func_code;
                    PyObject *call_args[arg_count];
                    PyObject *free_args[arg_count];

                    if (hydrate_args(code, arg_count, args, call_args, free_args) < 0) {
                        return StaticError;
                    }

                    obj_res = _PyClassLoader_InvokeMethod(vtable, slot, call_args, arg_count);
                    free_hydrated_args(free_args, arg_count);
                    if (obj_res != NULL) {
                        int optional = 0, exact = 0, coroutine = 0, type_code;
                        PyTypeObject *type = resolve_function_rettype(callable, &optional, &exact, &coroutine);
                        if (type != NULL && (type_code = _PyClassLoader_GetTypeCode(type)) != TYPED_OBJECT) {
                            res.rax = (void *)_PyClassLoader_Unbox(obj_res, type_code);
                        } else {
                            res.rax = obj_res;
                        }
                    } else {
                        res.rax = NULL;
                    }
                    res.rdx = (void *)(uint64_t)(obj_res != NULL);
                } else {
                    assert(arg_count < 5);
                    res.rax = _PyClassLoader_InvokeMethod(vtable, slot, (PyObject **)args, arg_count);
                    res.rdx = (void *)(uint64_t)(res.rax != NULL);
                }

            } else {
                res.rax = _PyClassLoader_InvokeMethod(vtable, slot, (PyObject **)args, nargsf);
                res.rdx = (void *)(uint64_t)(res.rax != NULL);
            }

            Py_XDECREF(original);
            Py_DECREF(value);
            return res;
        }
    }

    PyErr_Format(
        PyExc_TypeError, "'%s' has no attribute %U", type->tp_name, name);
    return StaticError;
}

__attribute__((__used__)) PyObject *
type_vtable_lazyinit_vectorcall(PyObject *thunk, PyObject **args, Py_ssize_t nargsf) {
    return (PyObject *)type_vtable_lazyinit_impl(thunk, (void **)args, nargsf, 0).rax;
}

__attribute__((__used__)) _PyClassLoader_StaticCallReturn
type_vtable_lazyinit_native(PyObject *thunk, void **args) {
    return type_vtable_lazyinit_impl(thunk, args, 0, 1);
}


VTABLE_THUNK(type_vtable_lazyinit)

void
_PyClassLoader_ClearCache()
{
    Py_CLEAR(classloader_cache);
    Py_CLEAR(classloader_cache_module_to_keys);
    Py_CLEAR(static_enum);
}

void
_PyClassLoader_ClearGenericTypes()
{
    Py_CLEAR(genericinst_cache);
}

/**
    For every slot in the vtable slotmap, this sets the vectorcall entrypoint
    to `type_vtable_lazyinit`.
*/
int
_PyClassLoader_ReinitVtable(PyTypeObject *type, _PyType_VTable *vtable)
{
    PyObject *name, *slot;
    PyObject *slotmap = vtable->vt_slotmap;
    Py_ssize_t i = 0;
    while (PyDict_Next(slotmap, &i, &name, &slot)) {
        Py_ssize_t index = PyLong_AsSsize_t(slot);
        PyObject *tuple = PyTuple_New(2);
        if (tuple == NULL) {
            return -1;
        }

        PyTuple_SET_ITEM(tuple, 0, name);
        Py_INCREF(name);
        PyTuple_SET_ITEM(tuple, 1, (PyObject *)type);
        Py_INCREF(type);
        vtable->vt_entries[index].vte_state = tuple;
        vtable->vt_entries[index].vte_entry = (vectorcallfunc) type_vtable_lazyinit_dont_bolt;
    }
    return 0;
}

int
used_in_vtable_worker(PyObject *value) {
    // we'll emit invokes to untyped builtin methods
    if (Py_TYPE(value) == &PyMethodDescr_Type) {
        return 1;
    } else if (Py_TYPE(value) == &_PyType_CachedPropertyThunk) {
        return used_in_vtable_worker(cachedpropthunk_get_func(value));
    } else if (Py_TYPE(value) == &_PyType_AsyncCachedPropertyThunk) {
        return used_in_vtable_worker(async_cachedpropthunk_get_func(value));
    }
    if (Py_TYPE(value) == &_PyTypedDescriptorWithDefaultValue_Type) {
       return 1;
    }
    if (Py_TYPE(value) == &_PyType_TypedDescriptorThunk) {
       return 1;
    }
    return _PyClassLoader_IsStaticCallable(value);
}

int
used_in_vtable(PyObject *value)
{
    if (used_in_vtable_worker(value)) {
        return 1;
    } else if (Py_TYPE(value) == &PyStaticMethod_Type &&
               used_in_vtable_worker(Ci_PyStaticMethod_GetFunc(value))) {
        return 1;
    } else if (Py_TYPE(value) == &PyClassMethod_Type &&
               used_in_vtable_worker(Ci_PyClassMethod_GetFunc(value))) {
        return 1;
    } else if (Py_TYPE(value) == &PyProperty_Type) {
        PyObject *func = ((Ci_propertyobject *)value)->prop_get;
        if (func != NULL && used_in_vtable_worker(func)) {
            return 1;
        }
        func = ((Ci_propertyobject *)value)->prop_set;
        if (func != NULL && used_in_vtable_worker(func)) {
            return 1;
        }
    } else if (Py_TYPE(value) == &PyCachedPropertyWithDescr_Type) {
        PyObject *func = ((PyCachedPropertyDescrObject *)value)->func;
        if (used_in_vtable_worker(func)) {
            return 1;
        }
    } else if (Py_TYPE(value) == &PyAsyncCachedPropertyWithDescr_Type) {
        PyObject *func = ((PyAsyncCachedPropertyDescrObject *)value)->func;
        if (used_in_vtable_worker(func)) {
            return 1;
        }
    }

    return 0;
}

// Steals a reference to the `getter_tuple` and `setter_tuple` objects.
int update_property_slot(PyObject *slotmap, int *slot_index, PyObject *getter_tuple, PyObject *setter_tuple)
{
    PyObject *getter_index = PyLong_FromLong((*slot_index)++);
    int err = PyDict_SetItem(slotmap, getter_tuple, getter_index);
    Py_DECREF(getter_index);
    Py_DECREF(getter_tuple);
    if (err) {
        Py_DECREF(setter_tuple);
        return -1;
    }
    PyObject *setter_index = PyLong_FromLong((*slot_index)++);
    err = PyDict_SetItem(slotmap, setter_tuple, setter_index);
    Py_DECREF(setter_index);
    Py_DECREF(setter_tuple);
    if (err) {
        return -1;
    }
    return 0;
}
/**
    Merges the slot map of our bases with our own members, initializing the
    map with the members which are defined in the current type but not the
    base type. Also, skips non-static callables that exist in tp_dict,
    because we cannot invoke against those anyway.
*/
int
_PyClassLoader_UpdateSlotMap(PyTypeObject *self, PyObject *slotmap) {
    PyObject *key, *value;
    Py_ssize_t i;

    /* Add indexes for anything that is new in our class */
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
        PyTypeObject *val_type = Py_TYPE(value);
        if (val_type == &PyProperty_Type ||
                val_type == &PyCachedPropertyWithDescr_Type ||
                val_type == &PyAsyncCachedPropertyWithDescr_Type) {
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
        else if (Py_TYPE(value) == &_PyTypedDescriptorWithDefaultValue_Type) {
            PyObject *getter_tuple = get_property_getter_descr_tuple(key);
            PyObject *setter_tuple = get_property_setter_descr_tuple(key);
            if (update_property_slot(slotmap, &slot_index, getter_tuple, setter_tuple) < 0) {
              return -1;
            }
        }
    }
    return 0;
}

int is_static_type(PyTypeObject *type) {
    return (type->tp_flags & (Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED|Ci_Py_TPFLAGS_GENERIC_TYPE_INST)) ||
        !(type->tp_flags & Py_TPFLAGS_HEAPTYPE);
}

/**
    Creates a vtable for a type. Goes through the MRO, and recursively creates v-tables for
    any static base classes if needed.
*/
_PyType_VTable *
_PyClassLoader_EnsureVtable(PyTypeObject *self, int init_subclasses)
{
    _PyType_VTable *vtable = (_PyType_VTable *)self->tp_cache;
    PyObject *slotmap = NULL;
    PyObject *mro;

    if (self == &PyBaseObject_Type) {
        // We don't create a vtable for `object`. If we try to do that, all subclasses of
        // `object` (which is all classes), will need to have a v-table of their own, and that's
        // too much memory usage for almost no benefit (since most classes are not Static).
        // Also, none of the attributes on `object` are interesting enough to invoke against.
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
    vtable->vt_specials = NULL;
    vtable->vt_slotmap = slotmap;
    vtable->vt_typecode = TYPED_OBJECT;
    self->tp_cache = (PyObject *)vtable;
    memset(&vtable->vt_entries[0], 0, sizeof(_PyType_VTableEntry) * slot_count);

    if (_PyClassLoader_ReinitVtable(self, vtable)) {
        self->tp_cache = NULL;
        Py_DECREF(vtable);
        return NULL;
    }


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
    if (type->tp_cache != NULL) {
        // If the type has a type code we need to preserve it, but we'll clear everything else
        int type_code = ((_PyType_VTable *)type->tp_cache)->vt_typecode;
        Py_CLEAR(type->tp_cache);
        if (type_code != TYPED_OBJECT) {
            _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(type, 0);
            if (vtable != NULL) {
                vtable ->vt_typecode = type_code;
            }
        }
    }
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

/**
    Makes sure the given type is a PyTypeObject (raises an error if not)
*/
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
                        "%R from %s",
                        path,
                        name,
                        gtd->ob_type->tp_name);
        return NULL;
    }
    PyObject *tmp_tuple = PyTuple_New(PyTuple_GET_SIZE(name));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(name); i++) {
        int optional, exact;
        PyObject *param = (PyObject *)_PyClassLoader_ResolveType(
            PyTuple_GET_ITEM(name, i), &optional, &exact);
        if (param == NULL) {
            Py_DECREF(tmp_tuple);
            return NULL;
        }
        if (optional) {
            PyObject *union_obj = _Py_union_type_or(param, Py_None);
            if (union_obj == NULL) {
                Py_DECREF(tmp_tuple);
                return NULL;
            }
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

/*
    Fetches the member held at the path defined by a type descriptor.
    e.g: ("mymod", "MyClass", "my_member")

    When container is not NULL, populates it with the `PyTypeObject` of the container.
    When containerkey is not NULL, populates it with the member name. This could be
    a tuple in the case of properties, such as ("my_member", "fget").

    The lookup is done from `sys.modules` (tstate->interp->modules), and if a module
    is not found, this function will import it.
*/
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

        // If we are getting a member from an exact or an optional type, simply skip these markers.
        if (PyUnicode_Check(name) &&
              (PyUnicode_CompareWithASCIIString(name, "?") == 0 ||
               PyUnicode_CompareWithASCIIString(name, "#") == 0 ||
               PyUnicode_CompareWithASCIIString(name, "!") == 0)) {
          continue;
        }

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

        if (containerkey != NULL) {
            *containerkey = name;
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
        if (PyType_Check(cur)) {
            if (get_func_or_special_callable((PyTypeObject *)cur, name, &next)) {
                return NULL;
            }
        } else {
            next = PyDict_GetItem(d, name);
            Py_XINCREF(next);
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
    if (type->tp_cache == NULL) {
        return TYPED_OBJECT;
    }

    return ((_PyType_VTable *)type->tp_cache)->vt_typecode;
}


/* Resolve a tuple type descr to a `prim_type` integer (`TYPED_*`); return -1
 * and set an error if the type cannot be resolved. */
int
_PyClassLoader_ResolvePrimitiveType(PyObject *descr) {
    if (!PyTuple_Check(descr) || PyTuple_GET_SIZE(descr) < 2) {
        PyErr_Format(PyExc_TypeError, "unknown type %R", descr);
        return -1;
    }

    PyObject *last_elem = PyTuple_GetItem(descr, PyTuple_GET_SIZE(descr) - 1);
    if (PyUnicode_CheckExact(last_elem) &&
        PyUnicode_CompareWithASCIIString(last_elem, "#") == 0) {
        int optional, exact;
        PyTypeObject *type = _PyClassLoader_ResolveType(descr, &optional, &exact);
        if (type == NULL) {
            return -1;
        }
        int res = _PyClassLoader_GetTypeCode(type);
        Py_DECREF(type);
        return res;
    }
    return TYPED_OBJECT;
}

/* Resolve a tuple type descr in the form ("module", "submodule", "Type") to a
 * PyTypeObject*` and `optional` integer out param.
 */
PyTypeObject *
_PyClassLoader_ResolveType(PyObject *descr, int *optional, int *exact)
{
    if (!PyTuple_Check(descr) || PyTuple_GET_SIZE(descr) < 2) {
        PyErr_Format(PyExc_TypeError, "unknown type %R", descr);
        return NULL;
    }

    Py_ssize_t items = PyTuple_GET_SIZE(descr);
    PyObject *last = PyTuple_GET_ITEM(descr, items - 1);

    *optional = 0;
    *exact = 0;

    while (PyUnicode_Check(last)) {
        if (PyUnicode_CompareWithASCIIString(last, "?") == 0) {
            *optional = 1;
        } else if (PyUnicode_CompareWithASCIIString(last, "!") == 0) {
            *exact = 1;
        } else if (PyUnicode_CompareWithASCIIString(last, "#") != 0) {
            break;
        } else {
            *exact = 1;
        }
        items--;
        last = PyTuple_GET_ITEM(descr, items - 1);
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

    if (classloader_cache_module_to_keys == NULL) {
        classloader_cache_module_to_keys = PyDict_New();
        if (classloader_cache_module_to_keys == NULL) {
            Py_DECREF(res);
            return NULL;
        }
    }

    if (PyDict_SetItem(classloader_cache, descr, res)) {
        Py_DECREF(res);
        return NULL;
    }
    PyObject *module_key = PyTuple_GET_ITEM(descr, 0);
    PyObject *existing_modules_to_keys = PyDict_GetItem(classloader_cache_module_to_keys,
                                                        module_key);
    if (existing_modules_to_keys == NULL) {
        existing_modules_to_keys = PyList_New(0);
        if (existing_modules_to_keys == NULL) {
            Py_DECREF(res);
            return NULL;
        }
        if (PyDict_SetItem(classloader_cache_module_to_keys, module_key, existing_modules_to_keys) < 0) {
            Py_DECREF(res);
            return NULL;
        }
        Py_DECREF(existing_modules_to_keys);
    }
    if (PyList_Append(existing_modules_to_keys, descr) < 0) {
        Py_DECREF(res);
        return NULL;
    }

    return (PyTypeObject *)res;
}

/**
    This function is called when a member on a previously unseen
    class is encountered.

    Given a type descriptor to a callable, this function:
    - Ensures that the containing class has a v-table.
    - Adds an entry to the global `classloader_cache`
      (so future slot index lookups are faster)
    - Initializes v-tables for all subclasses of the containing class
*/
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
    if (new_index == NULL) {
        PyErr_Format(
            PyExc_RuntimeError, "unable to resolve v-table slot '%R' in %s",
                slot_name, target_type->tp_name);
        Py_DECREF(target_type);
        Py_DECREF(cur);
        return -1;
    }
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

/**
    Returns a slot index given a "path" (type descr tuple) to a method.
    e.g ("my_mod", "MyClass", "my_method")
*/
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
    if (slot_index_obj == NULL) {
      if (classloader_init_slot(path)) {
        return -1;
      }
      slot_index_obj = PyDict_GetItem(classloader_cache, path);
    }
    return PyLong_AS_LONG(slot_index_obj);
}

_Py_StaticThunk *
get_or_make_thunk(PyObject *func, PyObject *original, PyObject* container, PyObject *name) {
    PyObject *thunks = NULL;
    PyTypeObject *type = NULL;
    if (PyType_Check(container)) {
        type = (PyTypeObject *)container;
        _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
        if (vtable->vt_thunks == NULL) {
            vtable->vt_thunks = PyDict_New();
            if (vtable->vt_thunks == NULL) {
                return NULL;
            }
        }
        thunks = vtable->vt_thunks;
    } else if (PyStrictModule_Check(container)) {
        PyStrictModuleObject *mod = (PyStrictModuleObject *)container;
        if (mod->static_thunks == NULL) {
            mod->static_thunks = PyDict_New();
            if (mod->static_thunks == NULL) {
                return NULL;
            }
        }
        thunks = mod->static_thunks;
    }
    _Py_StaticThunk *thunk = (_Py_StaticThunk *)PyDict_GetItem(thunks, name);
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
    Py_XINCREF(type);
    thunk->thunk_vectorcall = (vectorcallfunc)&thunk_vectorcall;

    PyObject *funcref;
    if (func == original) {
        funcref = original;
    } else {
        funcref = (PyObject *)thunk;
    }
    PyObject *unwrapped = classloader_maybe_unwrap_callable(funcref);
    if (unwrapped != NULL) {
        thunk->thunk_funcref = unwrapped;
        Py_DECREF(unwrapped);
    } else {
        thunk->thunk_funcref = funcref;
    }

    thunk->thunk_tcs.tcs_rt.rt_expected = (PyTypeObject *)_PyClassLoader_ResolveReturnType(
                                                               original,
                                                               &thunk->thunk_tcs.tcs_rt.rt_optional,
                                                               &thunk->thunk_tcs.tcs_rt.rt_exact,
                                                               &thunk->thunk_coroutine,
                                                               &thunk->thunk_classmethod);
    if (thunk->thunk_tcs.tcs_rt.rt_expected == NULL) {
        Py_DECREF(thunk);
        return NULL;
    }
    if (PyDict_SetItem(thunks, name, (PyObject *)thunk)) {
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
    if (container != NULL && *container != NULL) {
        assert(containerkey != NULL);
        if (PyType_Check(*container)) {
            PyTypeObject *type = (PyTypeObject *)*container;
            if (type->tp_cache != NULL) {
                PyObject *originals = ((_PyType_VTable *)type->tp_cache)->vt_original;
                if (originals != NULL) {
                    original = PyDict_GetItem(originals, containerkey);
                }
            }
        } else if (PyStrictModule_Check(*container)) {
            original = PyStrictModule_GetOriginal(*container, containerkey);
        }
    }
    if (original == func) {
        original = NULL;
    }

    if (func != NULL) {
        if (Py_TYPE(func) == &PyStaticMethod_Type) {
            PyObject *res = Ci_PyStaticMethod_GetFunc(func);
            Py_INCREF(res);
            Py_DECREF(func);
            func = res;
        }
        else if (Py_TYPE(func) == &PyClassMethod_Type) {
            PyObject *res = Ci_PyClassMethod_GetFunc(func);
            Py_INCREF(res);
            Py_DECREF(func);
            func = res;
        }
    }

    if (original != NULL) {
        PyObject *res = (PyObject *)get_or_make_thunk(func, original, *container, containerkey);
        Py_DECREF(func);
        assert(res != NULL);
        return res;
    }
    return func;
}

PyObject **
_PyClassLoader_GetIndirectPtr(PyObject *path, PyObject *func, PyObject *container) {
    PyObject **cache = NULL;
    if (_PyVectorcall_Function(func) == NULL) {
        goto done;
    }
    PyObject *name = PyTuple_GET_ITEM(path, PyTuple_GET_SIZE(path) - 1);
    int use_thunk = 0;
    if (PyType_Check(container)) {
        _PyType_VTable *vtable = _PyClassLoader_EnsureVtable((PyTypeObject *)container, 1);
        if (vtable == NULL) {
            return NULL;
        }
        use_thunk = 1;
    } else if (PyStrictModule_Check(container)) {
        use_thunk = 1;
    } else if (PyModule_Check(container)) {
        /* modules have no special translation on things we invoke, so
         * we just rely upon the normal JIT dict watchers */
        PyObject *dict = PyModule_Dict(container);
        if (dict != NULL) {
            cache = _PyJIT_GetDictCache(dict, name);
        }
    }
    if (use_thunk) {
        /* we pass func in for original here.  Either the thunk will already exist
         * in which case the value has been patched, or it won't yet exist in which
         * case func is the original function in the type. */
        _Py_StaticThunk *thunk = get_or_make_thunk(func, func, container, name);
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
        if (type->tp_flags & Ci_Py_TPFLAGS_FROZEN ||
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
                    PyExc_ValueError, "unknown static type: %S", path);
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
    } else if (Py_TYPE(cur) == &_PyTypedDescriptorWithDefaultValue_Type) {
        if (field_type != NULL) {
            *field_type = TYPED_OBJECT;
            assert(((_PyTypedDescriptorWithDefaultValue *)cur)->td_offset %
                       sizeof(Py_ssize_t) ==
                   0);
        }
        Py_DECREF(cur);
        return ((_PyTypedDescriptorWithDefaultValue *)cur)->td_offset;
    }

    Py_DECREF(cur);
    PyErr_Format(PyExc_TypeError, "bad field for class loader %R", path);
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
            _PyClassLoader_ResolveType(td->td_type, &td->td_optional, &td->td_exact);
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
        _PyObject_TypeCheckOptional(value, (PyTypeObject *) td->td_type, td->td_optional, td->td_exact)) {
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
    if (PyType_Ready(&_PyTypedDescriptor_Type) < 0) {
        return NULL;
    }
    _PyTypedDescriptor *res =
        PyObject_GC_New(_PyTypedDescriptor, &_PyTypedDescriptor_Type);
    if (res == NULL) {
        return NULL;
    }

    res->td_name = name;
    res->td_type = type;
    res->td_offset = offset;
    res->td_optional = 0;
    res->td_exact = 0;
    Py_INCREF(name);
    Py_INCREF(type);
    PyObject_GC_Track(res);
    return (PyObject *)res;
}

static void
typed_descriptor_with_default_value_dealloc(_PyTypedDescriptorWithDefaultValue *self)
{
    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->td_name);
    Py_XDECREF(self->td_type);
    Py_XDECREF(self->td_default);
    Py_TYPE(self)->tp_free(self);
}

static int
typed_descriptor_with_default_value_traverse(_PyTypedDescriptorWithDefaultValue *self, visitproc visit, void *arg)
{
    Py_VISIT(self->td_name);
    Py_VISIT(self->td_type);
    Py_VISIT(self->td_default);
    return 0;
}

static int
typed_descriptor_with_default_value_clear(_PyTypedDescriptorWithDefaultValue *self)
{
    Py_CLEAR(self->td_name);
    Py_CLEAR(self->td_type);
    Py_CLEAR(self->td_default);
    return 0;
}

static PyObject *
typed_descriptor_with_default_value_get(PyObject *self, PyObject *obj, PyObject *cls)
{
    _PyTypedDescriptorWithDefaultValue *td = (_PyTypedDescriptorWithDefaultValue *)self;
    if (obj == NULL) {
        /* Since we don't have any APIs supporting the modification of the default, it should
           always be set. */
        assert(td->td_default != NULL);
        Py_INCREF(td->td_default);
        return td->td_default;
    }

    PyObject *res = *(PyObject **)(((char *)obj) + td->td_offset);
    if (res == NULL) {
        res = td->td_default;
    }
    if (res == NULL) {
        PyErr_Format(PyExc_AttributeError,
                     "'%s' object has no attribute '%U'",
                     ((PyTypeObject *) cls)->tp_name,
                     td->td_name);
    }
    Py_XINCREF(res);
    return res;
}

static int
typed_descriptor_with_default_value_set(PyObject *self, PyObject *obj, PyObject *value)
{
    _PyTypedDescriptorWithDefaultValue *td = (_PyTypedDescriptorWithDefaultValue *)self;
     if (PyTuple_CheckExact(td->td_type)) {
        PyTypeObject *type =
            _PyClassLoader_ResolveType(td->td_type, &td->td_optional, &td->td_exact);
        if (type == NULL) {
            assert(PyErr_Occurred());
            if (value == Py_None && td->td_optional) {
                /* allow None assignment to optional values before the class is
                 * loaded */
                PyErr_Clear();
                PyObject **addr = (PyObject **)(((char *)obj) + td->td_offset);
                PyObject *prev = *addr;
                *addr = value;
                Py_XINCREF(value);
                Py_XDECREF(prev);
                return 0;
            }
            return -1;
        }
        Py_DECREF(td->td_type);
        td->td_type = (PyObject *)type;
    }

    if (value == NULL ||
        _PyObject_TypeCheckOptional(value, (PyTypeObject *) td->td_type, td->td_optional, td->td_exact)) {
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

PyTypeObject _PyTypedDescriptorWithDefaultValue_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "typed_descriptor_with_default_value",
    .tp_basicsize = sizeof(_PyTypedDescriptorWithDefaultValue),
    .tp_dealloc = (destructor)typed_descriptor_with_default_value_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_traverse = (traverseproc)typed_descriptor_with_default_value_traverse,
    .tp_clear = (inquiry)typed_descriptor_with_default_value_clear,
    .tp_descr_get = typed_descriptor_with_default_value_get,
    .tp_descr_set = typed_descriptor_with_default_value_set,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};

PyObject *
_PyTypedDescriptorWithDefaultValue_New(PyObject *name,
                                       PyObject *type,
                                       Py_ssize_t offset,
                                       PyObject *default_value)
{
    if (PyType_Ready(&_PyTypedDescriptorWithDefaultValue_Type) < 0) {
        return NULL;
    }
    _PyTypedDescriptorWithDefaultValue *res =
        PyObject_GC_New(_PyTypedDescriptorWithDefaultValue, &_PyTypedDescriptorWithDefaultValue_Type);
    if (res == NULL) {
        return NULL;
    }

    res->td_name = name;
    res->td_type = type;
    res->td_offset = offset;

    res->td_optional = 0;
    res->td_exact = 0;
    res->td_default = default_value;
    Py_INCREF(name);
    Py_INCREF(type);
    Py_INCREF(default_value);
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

        if (Py_TYPE(type) != &_PyUnion_Type) {
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
        Py_TPFLAGS_HEAPTYPE | Ci_Py_TPFLAGS_FROZEN | Ci_Py_TPFLAGS_GENERIC_TYPE_INST;
    new_inst->gti_type.ht_type.tp_flags &=
        ~(Py_TPFLAGS_READY | Ci_Py_TPFLAGS_GENERIC_TYPE_DEF);

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
    if (new_type->tp_base != NULL) {
      new_type->tp_new = new_type->tp_base->tp_new;
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
    } else if(((PyTypeObject *)type)->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_DEF) {
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
_PyClassLoader_ArgError(PyObject *func_name,
                        int arg,
                        int type_param,
                        const Ci_Py_SigElement *sig_elem,
                        PyObject *ctx)
{
    const char *expected = "?";
    int argtype = sig_elem->se_argtype;
    if (argtype & Ci_Py_SIG_TYPE_PARAM) {
        expected = ((PyTypeObject *)GENINST_GET_PARAM(
                        ctx, Ci_Py_SIG_TYPE_MASK(argtype)))
                       ->tp_name;

    } else {
        switch (Ci_Py_SIG_TYPE_MASK(argtype)) {
        case Ci_Py_SIG_OBJECT:
            PyErr_Format(PyExc_TypeError,
                         "%U() argument %d is missing",
                         func_name,
                         arg);
            return;
        case Ci_Py_SIG_STRING:
            expected = "str";
            break;
        case Ci_Py_SIG_SSIZE_T:
            expected = "int";
            break;
        }
    }

    PyErr_Format(PyExc_TypeError,
                 "%U() argument %d expected %s",
                 func_name,
                 arg,
                 expected);
}

const Ci_Py_SigElement Ci_Py_Sig_T0 = {Ci_Py_SIG_TYPE_PARAM_IDX(0)};
const Ci_Py_SigElement Ci_Py_Sig_T1 = {Ci_Py_SIG_TYPE_PARAM_IDX(1)};
const Ci_Py_SigElement Ci_Py_Sig_T0_Opt = {
    Ci_Py_SIG_TYPE_PARAM_IDX(0) | Ci_Py_SIG_OPTIONAL, Py_None};
const Ci_Py_SigElement Ci_Py_Sig_T1_Opt = {
    Ci_Py_SIG_TYPE_PARAM_IDX(1) | Ci_Py_SIG_OPTIONAL, Py_None};
const Ci_Py_SigElement Ci_Py_Sig_Object = {Ci_Py_SIG_OBJECT};
const Ci_Py_SigElement Ci_Py_Sig_Object_Opt = {Ci_Py_SIG_OBJECT | Ci_Py_SIG_OPTIONAL,
                                           Py_None};
const Ci_Py_SigElement Ci_Py_Sig_String = {Ci_Py_SIG_STRING};
const Ci_Py_SigElement Ci_Py_Sig_String_Opt = {Ci_Py_SIG_STRING | Ci_Py_SIG_OPTIONAL,
                                           Py_None};

const Ci_Py_SigElement Ci_Py_Sig_SSIZET = {Ci_Py_SIG_SSIZE_T};
const Ci_Py_SigElement Ci_Py_Sig_SIZET = {Ci_Py_SIG_SIZE_T};
const Ci_Py_SigElement Ci_Py_Sig_INT8 = {Ci_Py_SIG_INT8};
const Ci_Py_SigElement Ci_Py_Sig_INT16 = {Ci_Py_SIG_INT16};
const Ci_Py_SigElement Ci_Py_Sig_INT32 = {Ci_Py_SIG_INT32};
const Ci_Py_SigElement Ci_Py_Sig_INT64 = {Ci_Py_SIG_INT64};
const Ci_Py_SigElement Ci_Py_Sig_UINT8 = {Ci_Py_SIG_UINT8};
const Ci_Py_SigElement Ci_Py_Sig_UINT16 = {Ci_Py_SIG_UINT16};
const Ci_Py_SigElement Ci_Py_Sig_UINT32 = {Ci_Py_SIG_UINT32};
const Ci_Py_SigElement Ci_Py_Sig_UINT64 = {Ci_Py_SIG_UINT64};


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
    _Py_CODEUNIT* rawcode = (_Py_CODEUNIT *)PyBytes_AS_STRING(code->co_code);
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
        int optional, exact;
        PyTypeObject* ref_type = _PyClassLoader_ResolveType(type_descr, &optional, &exact);
        if (ref_type == NULL) {
            return NULL;
        }

        int prim_type = _PyClassLoader_GetTypeCode(ref_type);
        if (prim_type == TYPED_BOOL) {
            cur_check->tai_type = &PyBool_Type;
            cur_check->tai_optional = 0;
            cur_check->tai_exact = 1;
            Py_INCREF(&PyBool_Type);
            Py_DECREF(ref_type);
        } else if (prim_type == TYPED_DOUBLE) {
            cur_check->tai_type = &PyFloat_Type;
            cur_check->tai_optional = 0;
            cur_check->tai_exact = 1;
            Py_INCREF(&PyFloat_Type);
            Py_DECREF(ref_type);
        } else if (prim_type != TYPED_OBJECT) {
            assert(prim_type <= TYPED_INT64);
            cur_check->tai_type = &PyLong_Type;
            cur_check->tai_optional = 0;
            cur_check->tai_exact = 1;
            Py_INCREF(&PyLong_Type);
            Py_DECREF(ref_type);
        } else if (only_primitives) {
            Py_DECREF(ref_type);
            continue;
        } else {
            cur_check->tai_type = ref_type;
            cur_check->tai_optional = optional;
            cur_check->tai_exact = exact;
        }
        cur_check->tai_primitive_type = prim_type;
        cur_check->tai_argnum = PyLong_AsLong(PyTuple_GET_ITEM(checks, i));
        checki++;
    }
    return arg_checks;
}

_PyTypedArgsInfo* _PyClassLoader_GetTypedArgsInfoFromThunk(PyObject *thunk, PyObject *container, int only_primitives) {
    if (!_PyClassLoader_IsPatchedThunk(thunk)) {
        return NULL;
    }
    PyObject *originals = NULL;
    if (PyType_Check(container)) {
        PyObject *vtable = ((PyTypeObject*)container)->tp_cache;
        originals = ((_PyType_VTable*)vtable)->vt_original;
    } else if (PyStrictModule_Check(container)) {
        originals = ((PyStrictModuleObject*)container)->originals;
    }
    if (!originals) {
        return NULL;
    }
    PyObject *original =
        PyDict_GetItem(originals, ((_Py_StaticThunk*)thunk)->thunk_tcs.tcs_rt.rt_name);
    if (original == NULL) {
        return NULL;
    }
    PyObject *unwrapped = classloader_maybe_unwrap_callable(original);
    if (unwrapped != NULL) {
        original = unwrapped;
    }
    PyObject *code = PyFunction_GetCode(original);
    if (code == NULL) {
        return NULL;
    }
    return _PyClassLoader_GetTypedArgsInfo((PyCodeObject*)code, only_primitives);
}

int _PyClassLoader_HasPrimitiveArgs(PyCodeObject* code) {
  _Py_CODEUNIT* rawcode = (_Py_CODEUNIT *)PyBytes_AS_STRING(code->co_code);
  PyObject* checks = PyTuple_GET_ITEM(code->co_consts, _Py_OPARG(rawcode[0]));
  for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
    PyObject* type_descr = PyTuple_GET_ITEM(checks, i + 1);

    if (_PyClassLoader_ResolvePrimitiveType(type_descr) != TYPED_OBJECT) {
      return 1;
    }
  }
  return 0;
}

int _PyClassLoader_NotifyDictChange(PyDictObject *dict, PyObject *key) {
  PyThreadState *tstate = PyThreadState_GET();
  PyObject *modules_dict = tstate->interp->modules;
  if (((PyObject *)dict) != modules_dict) {
    return 0;
  }
  if (classloader_cache_module_to_keys == NULL) {
      return 0;
  }
  PyObject *keys_to_invalidate = PyDict_GetItem(classloader_cache_module_to_keys, key);
  if (keys_to_invalidate == NULL) {
      return 0;
  }
  for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys_to_invalidate); i++) {
      PyObject* key_to_invalidate = PyList_GET_ITEM(keys_to_invalidate, i);
      if (PyDict_DelItem(classloader_cache, key_to_invalidate) < 0) {
          return 0;
      }
  }
  PyDict_DelItem(classloader_cache_module_to_keys, key);
  return 0;
}

static PyObject *invoke_native_helper = NULL;

static inline int import_invoke_native() {
  if (_Py_UNLIKELY(invoke_native_helper == NULL)) {
    PyObject *native_utils = PyImport_ImportModule("__static__.native_utils");
    if (native_utils == NULL) {
      return -1;
    }
    invoke_native_helper =
        PyObject_GetAttrString(native_utils, "invoke_native");
    Py_DECREF(native_utils);
    if (invoke_native_helper == NULL) {
      return -1;
    }
  }
  return 0;
}

PyObject *_PyClassloader_InvokeNativeFunction(PyObject *lib_name,
                                              PyObject *symbol_name,
                                              PyObject *signature,
                                              PyObject **args, Py_ssize_t nargs) {
  if (!PyUnicode_CheckExact(lib_name)) {
    PyErr_Format(PyExc_RuntimeError, "'lib_name' must be a str, got '%s'",
                 Py_TYPE(lib_name)->tp_name);
    return NULL;
  }
  if (!PyUnicode_CheckExact(lib_name)) {
    PyErr_Format(PyExc_RuntimeError, "'symbol_name' must be a str, got '%s'",
                 Py_TYPE(lib_name)->tp_name);
    return NULL;
  }
  if (!PyTuple_CheckExact(signature)) {
    PyErr_Format(PyExc_RuntimeError,
                 "'signature' must be a tuple of type descriptors",
                 Py_TYPE(lib_name)->tp_name);
    return NULL;
  }

  int return_typecode =
      _PyClassLoader_ResolvePrimitiveType(PyTuple_GET_ITEM(signature, nargs));
  if (return_typecode == -1) {
    // exception must be set already
    assert(PyErr_Occurred());
    return NULL;
  }

  // build arg tuple.. this is kinda wasteful, but we're not optimizing for the
  // interpreter here
  PyObject *arguments = PyTuple_New(nargs);
  if (arguments == NULL) {
    return NULL;
  }
  for (int i = 0; i < nargs; i++) {
    PyTuple_SET_ITEM(arguments, i, args[i]);
    Py_INCREF(args[i]);
  }

  if (import_invoke_native() < 0) {
    return NULL;
  }
  PyObject *res = PyObject_CallFunction(invoke_native_helper, "OOOO", lib_name, symbol_name,
                               signature, arguments);

  Py_DECREF(arguments);
  return res;
}

// Returns the size of the dlsym_cache dict (0 if uninitialized)
PyObject *_PyClassloader_SizeOf_DlSym_Cache() {
  if (dlsym_cache == NULL) {
    return PyLong_FromLong(0);
  }
  Py_ssize_t size = PyDict_Size(dlsym_cache);
  return PyLong_FromSsize_t(size);
}

// Returns the size of the dlopen_cache dict (0 if uninitialized)
PyObject *_PyClassloader_SizeOf_DlOpen_Cache() {
  if (dlopen_cache == NULL) {
    return PyLong_FromLong(0);
  }
  Py_ssize_t size = PyDict_Size(dlopen_cache);
  return PyLong_FromSsize_t(size);
}

// Clears the dlsym_cache dict
void _PyClassloader_Clear_DlSym_Cache() {
  if (dlsym_cache != NULL) {
    PyDict_Clear(dlsym_cache);
  }
}

// Clears the dlopen_cache dict
void _PyClassloader_Clear_DlOpen_Cache() {
  if (dlopen_cache != NULL) {
    PyObject *name, *handle;
    Py_ssize_t i = 0;
    while (PyDict_Next(dlopen_cache, &i, &name, &handle)) {
      void *raw_handle = PyLong_AsVoidPtr(handle);
      // Ignore errors - we can't do much even if they occur
      dlclose(raw_handle);
    }

    PyDict_Clear(dlopen_cache);
  }
}

// Thin wrapper over dlopen, returns the handle of the opened lib
static void *classloader_dlopen(PyObject *lib_name) {
  assert(PyUnicode_CheckExact(lib_name));
  const char *raw_lib_name = PyUnicode_AsUTF8(lib_name);
  if (raw_lib_name == NULL) {
    return NULL;
  }
  void *handle = dlopen(raw_lib_name, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    PyErr_Format(PyExc_RuntimeError,
                 "classloader: Could not load library '%s': %s", raw_lib_name,
                 dlerror());
    return NULL;
  }
  return handle;
}

// Looks up the cached handle to the shared lib of given name. If not found,
// proceeds to load it and populates the cache.
static void *classloader_lookup_sharedlib(PyObject *lib_name) {
  assert(PyUnicode_CheckExact(lib_name));
  PyObject *val = NULL;

  // Ensure cache exists
  if (dlopen_cache == NULL) {
    dlopen_cache = PyDict_New();
    if (dlopen_cache == NULL) {
      return NULL;
    }
  }

  val = PyDict_GetItem(dlopen_cache, lib_name);
  if (val != NULL) {
    // Cache hit
    return PyLong_AsVoidPtr(val);
  }

  // Lookup the lib
  void *handle = classloader_dlopen(lib_name);
  if (handle == NULL) {
    return NULL;
  }

  // Populate the cache with the handle
  val = PyLong_FromVoidPtr(handle);
  if (val == NULL) {
    return NULL;
  }
  int res = PyDict_SetItem(dlopen_cache, lib_name, val);
  Py_DECREF(val);
  if (res < 0) {
    return NULL;
  }
  return handle;
}

// Wrapper over `dlsym`.
static PyObject *classloader_lookup_symbol(PyObject *lib_name, PyObject *symbol_name) {
  void *handle = classloader_lookup_sharedlib(lib_name);
  if (handle == NULL) {
    assert(PyErr_Occurred());
    return NULL;
  }

  const char *raw_symbol_name = PyUnicode_AsUTF8(symbol_name);
  if (raw_symbol_name == NULL) {
    return NULL;
  }

  void *res = dlsym(handle, raw_symbol_name);
  if (res == NULL) {
    // Technically, `res` could actually have the value `NULL`, but we're
    // in the business of looking up callables, so we raise an exception
    // (NULL cannot be called anyway).
    //
    // To be 100% correct, we could clear existing errors with `dlerror`,
    // call `dlsym` and then call `dlerror` again, to check whether an
    // error occured, but that'll be more work than we need.
    PyErr_Format(PyExc_RuntimeError,
                 "classloader: unable to lookup '%U' in '%U': %s", symbol_name,
                 lib_name, dlerror());
    return NULL;
  }

  PyObject *symbol = PyLong_FromVoidPtr(res);
  if (symbol == NULL) {
    return NULL;
  }
  return symbol;
}

// Looks up the raw symbol address from the given lib, and returns
// a boxed value of it.
void *_PyClassloader_LookupSymbol(PyObject *lib_name,
                                      PyObject *symbol_name) {
  if (!PyUnicode_CheckExact(lib_name)) {
    PyErr_Format(PyExc_TypeError,
                 "classloader: 'lib_name' must be a str, got '%s'",
                 Py_TYPE(lib_name)->tp_name);
    return NULL;
  }
  if (!PyUnicode_CheckExact(symbol_name)) {
    PyErr_Format(PyExc_TypeError,
                 "classloader: 'symbol_name' must be a str, got '%s'",
                 Py_TYPE(symbol_name)->tp_name);
    return NULL;
  }

  // Ensure cache exists
  if (dlsym_cache == NULL) {
    dlsym_cache = PyDict_New();
    if (dlsym_cache == NULL) {
      return NULL;
    }
  }

  PyObject *key = PyTuple_Pack(2, lib_name, symbol_name);
  if (key == NULL) {
    return NULL;
  }

  PyObject *res = PyDict_GetItem(dlsym_cache, key);

  if (res != NULL) {
    Py_DECREF(key);
    return PyLong_AsVoidPtr(res);
  }

  res = classloader_lookup_symbol(lib_name, symbol_name);
  if (res == NULL) {
    Py_DECREF(key);
    return NULL;
  }

  if (PyDict_SetItem(dlsym_cache, key, res) < 0) {
    Py_DECREF(key);
    Py_DECREF(res);
    return NULL;
  }

  void* addr = PyLong_AsVoidPtr(res);
  Py_DECREF(key);
  Py_DECREF(res);
  return addr;
}


/**
    Lightweight implementation of Static Arrays.
*/

#define PyStaticArray_CheckExact(op) Py_IS_TYPE(op, &PyStaticArray_Type)
#define ArrayItemType int64_t

static void
staticarray_dealloc(PyStaticArrayObject *op)
{
    PyObject_GC_UnTrack(op);
    Py_TYPE(op)->tp_free((PyObject *)op);
}

static PyStaticArrayObject* staticarray_alloc(Py_ssize_t size) {
    PyStaticArrayObject *op = PyObject_GC_NewVar(PyStaticArrayObject, &PyStaticArray_Type, size);
    return op;
}

static inline void staticarray_zeroinitialize(PyStaticArrayObject* sa, Py_ssize_t size) {
    memset(sa->ob_item, 0, size * PyStaticArray_Type.tp_itemsize);
}

static PyObject *
staticarray_vectorcall(PyObject *type, PyObject * const*args,
                size_t nargsf, PyObject *kwnames)
{
    if (!_PyArg_NoKwnames("staticarray", kwnames)) {
        return NULL;
    }

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (!_PyArg_CheckPositional("staticarray", nargs, 1, 1)) {
        return NULL;
    }

    PyObject* length = args[0];
    Py_ssize_t size = PyLong_AsSize_t(length);
    if (size == -1 && PyErr_Occurred()) {
        return NULL;
    }
    PyStaticArrayObject *new = staticarray_alloc(size);
    staticarray_zeroinitialize(new, size);
    return (PyObject*)new;
}

static PyObject *
staticarray_to_list(PyObject* sa) {
    PyStaticArrayObject* array = (PyStaticArrayObject*)sa;
    PyObject *list = PyList_New(Py_SIZE(sa));

    if (list == NULL) {
        return NULL;
    }

    for (Py_ssize_t i = 0; i < Py_SIZE(sa); i++) {
        ArrayItemType val = array->ob_item[i];
        PyObject *boxed_val = PyLong_FromLong(val);
        if (boxed_val == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, boxed_val);
    }
    return list;
}

static PyObject *
staticarray_repr(PyObject *sa)
{
    return PyUnicode_FromFormat(
        "staticarray[%d](%R)",
        Py_SIZE(sa),
        staticarray_to_list(sa));
}

static Py_ssize_t
staticarray_length(PyStaticArrayObject *a)
{
    return Py_SIZE(a);
}

static int staticarray_traverse(PyObject *self, visitproc visit, void *arg) {
    return 0;
}

static PyObject *
staticarray_concat(PyStaticArrayObject *first, PyObject *other)
{
    Py_ssize_t size;
    PyStaticArrayObject *np;
    if (!PyStaticArray_CheckExact(other)) {
        PyErr_Format(PyExc_TypeError,
             "can only append staticarray (not \"%.200s\") to staticarray",
                 Py_TYPE(other)->tp_name);
        return NULL;
    }
    PyStaticArrayObject *second = (PyStaticArrayObject*)other;
    if (Py_SIZE(first) > PY_SSIZE_T_MAX - Py_SIZE(second)) {
        return PyErr_NoMemory();
    }
    size = Py_SIZE(first) + Py_SIZE(second);
    np = staticarray_alloc(size);
    if (np == NULL) {
        return NULL;
    }
    Py_ssize_t itemsize = PyStaticArray_Type.tp_itemsize;
    if (Py_SIZE(first) > 0) {
        memcpy(np->ob_item, first->ob_item, Py_SIZE(first) * itemsize);
    }
    if (Py_SIZE(second) > 0) {
        memcpy(np->ob_item + Py_SIZE(first),
               second->ob_item, Py_SIZE(second) * itemsize);
    }
    return (PyObject *)np;
}

static PyObject *
staticarray_repeat(PyStaticArrayObject *array, Py_ssize_t n)
{
    Py_ssize_t size;
    PyStaticArrayObject *np;
    if (n < 0) {
        return (PyObject*)staticarray_alloc(0);
    }
    if ((Py_SIZE(array) != 0) && (n > PY_SSIZE_T_MAX / Py_SIZE(array))) {
        return PyErr_NoMemory();
    }
    size = Py_SIZE(array) * n;
    np = (PyStaticArrayObject *)staticarray_alloc(size);
    if (np == NULL)
        return NULL;
    if (size == 0)
        return (PyObject *)np;

    Py_ssize_t oldsize = Py_SIZE(array);
    Py_ssize_t newsize = oldsize * n;
    Py_ssize_t itemsize = PyStaticArray_Type.tp_itemsize;

    Py_ssize_t done = oldsize;
    memcpy(np->ob_item, array->ob_item, oldsize * itemsize);
    while (done < newsize) {
        Py_ssize_t ncopy = (done <= newsize-done) ? done : newsize-done;
        memcpy(np->ob_item + done, np->ob_item, ncopy * itemsize);
        done += ncopy;
    }

    return (PyObject *)np;
}

static PyObject *
staticarray_getitem(PyStaticArrayObject *array, Py_ssize_t index)
{
    index = index < 0 ? index + Py_SIZE(array): index;
    if (index < 0 || index >= Py_SIZE(array)) {
        PyErr_SetString(PyExc_IndexError, "array index out of range");
        return NULL;
    }
    assert (PyStaticArray_Type.tp_itemsize == sizeof(long));
    return PyLong_FromLong(array->ob_item[index]);
}

static int
staticarray_setitem(PyStaticArrayObject *array, Py_ssize_t index, PyObject* value)
{
    index = index < 0 ? index + Py_SIZE(array): index;
    if (index < 0 || index >= Py_SIZE(array)) {
        PyErr_SetString(PyExc_IndexError, "array index out of range");
        return -1;
    }
    assert (PyStaticArray_Type.tp_itemsize == sizeof(long));
    ArrayItemType val = PyLong_AsLong(value);
    if (val == -1 && PyErr_Occurred()) {
        return -1;
    }
    array->ob_item[index] = val;
    return 0;
}

PyObject *
staticarray___class_getitem__(PyObject *origin, PyObject *args)
{
    Py_INCREF(origin);
    return origin;
}

PyObject *staticarray_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    if (!_PyArg_NoKeywords("staticarray", kwds)) {
        return NULL;
    }

    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    if (!_PyArg_CheckPositional("staticarray", nargs, 1, 1)) {
        return NULL;
    }

    PyObject* length = PyTuple_GET_ITEM(args, 0);
    Py_ssize_t size = PyLong_AsSize_t(length);
    if (size == -1 && PyErr_Occurred()) {
        return NULL;
    }
    PyStaticArrayObject *new = (PyStaticArrayObject *)type->tp_alloc(type, size);
    staticarray_zeroinitialize(new, size);
    return (PyObject*)new;
}

static PySequenceMethods staticarray_as_sequence = {
    .sq_length = (lenfunc)staticarray_length,
    .sq_concat = (binaryfunc)staticarray_concat,
    .sq_repeat = (ssizeargfunc)staticarray_repeat,
    .sq_item = (ssizeargfunc)staticarray_getitem,
    .sq_ass_item = (ssizeobjargproc)staticarray_setitem,
};

static PyMethodDef staticarray_methods[] = {
    {"__class_getitem__", (PyCFunction)staticarray___class_getitem__, METH_O|METH_CLASS, PyDoc_STR("")},
    {NULL,              NULL}   /* sentinel */
};

PyTypeObject PyStaticArray_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "staticarray",
    .tp_alloc = PyType_GenericAlloc,
    .tp_basicsize = sizeof(PyStaticArrayObject) - sizeof(PyObject *),
    .tp_itemsize = sizeof(ArrayItemType),
    .tp_dealloc = (destructor)staticarray_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT |
        Py_TPFLAGS_HAVE_GC,
    .tp_free = PyObject_GC_Del,
    .tp_vectorcall = staticarray_vectorcall,
    .tp_repr = staticarray_repr,
    .tp_methods = staticarray_methods,
    .tp_new = staticarray_new,
    .tp_as_sequence = &staticarray_as_sequence,
    .tp_traverse = staticarray_traverse,
};

/** StaticArray internal C-API **/

int _Ci_StaticArray_Set(PyObject *array, Py_ssize_t index, PyObject *value) {
    PyStaticArrayObject *sa = (PyStaticArrayObject *)array;
    return staticarray_setitem(sa, index, value);
}

PyObject* _Ci_StaticArray_Get(PyObject *array, Py_ssize_t index) {
    PyStaticArrayObject *sa = (PyStaticArrayObject *)array;
    return staticarray_getitem(sa, index);
}
