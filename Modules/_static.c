/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */

#include "Python.h"
#include "boolobject.h"
#include "cellobject.h"
#include "dictobject.h"
#include "frameobject.h"
#include "funcobject.h"
#include "import.h"
#include "methodobject.h"
#include "object.h"
#include "pyerrors.h"
#include "pyport.h"
#include "pycore_tuple.h"
#include "structmember.h"
#include "pycore_object.h"
#include "classloader.h"

PyDoc_STRVAR(_static__doc__,
             "_static contains types related to static Python\n");

extern PyTypeObject Ci_CheckedDict_Type;
extern PyTypeObject Ci_CheckedList_Type;

static int
_static_exec(PyObject *m)
{
    if (PyType_Ready((PyTypeObject *)&Ci_CheckedDict_Type) < 0)
        return -1;

    if (PyType_Ready((PyTypeObject *)&Ci_CheckedList_Type) < 0)
        return -1;

    PyObject *globals = ((PyStrictModuleObject *)m)->globals;
    if (PyDict_SetItemString(globals, "chkdict", (PyObject *)&Ci_CheckedDict_Type) < 0)
        return -1;

    if (PyDict_SetItemString(globals, "chklist", (PyObject *)&Ci_CheckedList_Type) < 0)
        return -1;

    PyObject *type_code;
#define SET_TYPE_CODE(name)                                           \
    type_code = PyLong_FromLong(name);                                \
    if (type_code == NULL) {                                          \
        return -1;                                                    \
    }                                                                 \
    if (PyDict_SetItemString(globals, #name, type_code) < 0) {        \
        Py_DECREF(type_code);                                         \
        return -1;                                                    \
    }                                                                 \
    Py_DECREF(type_code);

    SET_TYPE_CODE(TYPED_INT_UNSIGNED)
    SET_TYPE_CODE(TYPED_INT_SIGNED)
    SET_TYPE_CODE(TYPED_INT_8BIT)
    SET_TYPE_CODE(TYPED_INT_16BIT)
    SET_TYPE_CODE(TYPED_INT_32BIT)
    SET_TYPE_CODE(TYPED_INT_64BIT)
    SET_TYPE_CODE(TYPED_OBJECT)
    SET_TYPE_CODE(TYPED_INT8)
    SET_TYPE_CODE(TYPED_INT16)
    SET_TYPE_CODE(TYPED_INT32)
    SET_TYPE_CODE(TYPED_INT64)
    SET_TYPE_CODE(TYPED_UINT8)
    SET_TYPE_CODE(TYPED_UINT16)
    SET_TYPE_CODE(TYPED_UINT32)
    SET_TYPE_CODE(TYPED_UINT64)
    SET_TYPE_CODE(TYPED_SINGLE)
    SET_TYPE_CODE(TYPED_DOUBLE)
    SET_TYPE_CODE(TYPED_BOOL)
    SET_TYPE_CODE(TYPED_CHAR)
    SET_TYPE_CODE(TYPED_ARRAY)


    SET_TYPE_CODE(SEQ_LIST)
    SET_TYPE_CODE(SEQ_TUPLE)
    SET_TYPE_CODE(SEQ_LIST_INEXACT)
    SET_TYPE_CODE(SEQ_ARRAY_INT64)
    SET_TYPE_CODE(SEQ_SUBSCR_UNCHECKED)

    SET_TYPE_CODE(SEQ_REPEAT_INEXACT_SEQ)
    SET_TYPE_CODE(SEQ_REPEAT_INEXACT_NUM)
    SET_TYPE_CODE(SEQ_REPEAT_REVERSED)
    SET_TYPE_CODE(SEQ_REPEAT_PRIMITIVE_NUM)

    SET_TYPE_CODE(SEQ_CHECKED_LIST)

    SET_TYPE_CODE(PRIM_OP_EQ_INT)
    SET_TYPE_CODE(PRIM_OP_NE_INT)
    SET_TYPE_CODE(PRIM_OP_LT_INT)
    SET_TYPE_CODE(PRIM_OP_LE_INT)
    SET_TYPE_CODE(PRIM_OP_GT_INT)
    SET_TYPE_CODE(PRIM_OP_GE_INT)
    SET_TYPE_CODE(PRIM_OP_LT_UN_INT)
    SET_TYPE_CODE(PRIM_OP_LE_UN_INT)
    SET_TYPE_CODE(PRIM_OP_GT_UN_INT)
    SET_TYPE_CODE(PRIM_OP_GE_UN_INT)
    SET_TYPE_CODE(PRIM_OP_EQ_DBL)
    SET_TYPE_CODE(PRIM_OP_NE_DBL)
    SET_TYPE_CODE(PRIM_OP_LT_DBL)
    SET_TYPE_CODE(PRIM_OP_LE_DBL)
    SET_TYPE_CODE(PRIM_OP_GT_DBL)
    SET_TYPE_CODE(PRIM_OP_GE_DBL)

    SET_TYPE_CODE(PRIM_OP_ADD_INT)
    SET_TYPE_CODE(PRIM_OP_SUB_INT)
    SET_TYPE_CODE(PRIM_OP_MUL_INT)
    SET_TYPE_CODE(PRIM_OP_DIV_INT)
    SET_TYPE_CODE(PRIM_OP_DIV_UN_INT)
    SET_TYPE_CODE(PRIM_OP_MOD_INT)
    SET_TYPE_CODE(PRIM_OP_MOD_UN_INT)
    SET_TYPE_CODE(PRIM_OP_POW_INT)
    SET_TYPE_CODE(PRIM_OP_POW_UN_INT)
    SET_TYPE_CODE(PRIM_OP_LSHIFT_INT)
    SET_TYPE_CODE(PRIM_OP_RSHIFT_INT)
    SET_TYPE_CODE(PRIM_OP_RSHIFT_UN_INT)
    SET_TYPE_CODE(PRIM_OP_XOR_INT)
    SET_TYPE_CODE(PRIM_OP_OR_INT)
    SET_TYPE_CODE(PRIM_OP_AND_INT)

    SET_TYPE_CODE(PRIM_OP_ADD_DBL)
    SET_TYPE_CODE(PRIM_OP_SUB_DBL)
    SET_TYPE_CODE(PRIM_OP_MUL_DBL)
    SET_TYPE_CODE(PRIM_OP_DIV_DBL)
    SET_TYPE_CODE(PRIM_OP_MOD_DBL)
    SET_TYPE_CODE(PRIM_OP_POW_DBL)

    SET_TYPE_CODE(PRIM_OP_NEG_INT)
    SET_TYPE_CODE(PRIM_OP_INV_INT)
    SET_TYPE_CODE(PRIM_OP_NEG_DBL)
    SET_TYPE_CODE(PRIM_OP_NOT_INT)

    SET_TYPE_CODE(FAST_LEN_INEXACT)
    SET_TYPE_CODE(FAST_LEN_LIST)
    SET_TYPE_CODE(FAST_LEN_DICT)
    SET_TYPE_CODE(FAST_LEN_SET)
    SET_TYPE_CODE(FAST_LEN_TUPLE)
    SET_TYPE_CODE(FAST_LEN_ARRAY);
    SET_TYPE_CODE(FAST_LEN_STR)

    /* Not actually a type code, but still an int */
    SET_TYPE_CODE(RAND_MAX);

    return 0;
}

static PyObject* _static_create(PyObject *spec, PyModuleDef *def) {

    PyObject *mod_dict = PyDict_New();
    if (mod_dict == NULL) {
        return NULL;
    }
    PyObject *args = PyTuple_New(1);
    if (args == NULL) {
        Py_DECREF(mod_dict);
        return NULL;
    }

    PyObject *loader = PyObject_GetAttrString(spec, "loader");
    if (loader == NULL) {
        Py_DECREF(mod_dict);
        return NULL;
    }

    if (PyDict_SetItemString(mod_dict, "__spec__", spec) ||
        PyDict_SetItemString(mod_dict, "__loader__", loader)) {
        Py_DECREF(mod_dict);
        Py_DECREF(loader);
        return NULL;
    }
    Py_DECREF(loader);

    PyTuple_SET_ITEM(args, 0, mod_dict);


    PyObject *res = PyStrictModule_New(&PyStrictModule_Type, args, NULL);
    Py_DECREF(args);
    if (res == NULL) {
        return NULL;
    }

    PyObject *name = PyUnicode_FromString("_static");
    if (name == NULL) {
        Py_DECREF(res);
        return NULL;
    }

    PyObject *base_dict = PyDict_New();
    if(base_dict == NULL) {
        Py_DECREF(res);
        Py_DECREF(name);
        return NULL;
    }

    ((PyModuleObject*)res)->md_dict = base_dict;
    if (PyDict_SetItemString(mod_dict, "__name__", name) ||
       PyModule_AddObject(res, "__name__", name)) {
        Py_DECREF(res);
        Py_DECREF(name);
        return NULL;
    }

    if (PyType_Ready(&PyStaticArray_Type) < 0) {
        Py_DECREF(res);
        Py_DECREF(name);
        return NULL;
    }

    if (PyDict_SetItemString(mod_dict, "staticarray", (PyObject*)&PyStaticArray_Type)) {
        Py_DECREF(res);
        Py_DECREF(name);
        return NULL;
    }

    return res;
}

static struct PyModuleDef_Slot _static_slots[] = {
    {Py_mod_create, _static_create},
    {Py_mod_exec, _static_exec},
    {0, NULL},
};

PyObject *set_type_code(PyObject *mod, PyObject *const *args, Py_ssize_t nargs) {
    PyTypeObject *type;
    Py_ssize_t code;
    if (!_PyArg_ParseStack(args, nargs, "O!n", &PyType_Type, &type, &code)) {
        return NULL;
    } else if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_SetString(PyExc_TypeError, "expected heap type");
        return NULL;
    }

    _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(type, 0);
    if (vtable == NULL) {
        return NULL;
    }

    vtable->vt_typecode = code;
    Py_RETURN_NONE;
}

PyObject *is_type_static(PyObject *mod, PyObject *type) {
  PyTypeObject *pytype;
  if (!PyType_Check(type)) {
    Py_RETURN_FALSE;
  }
  pytype = (PyTypeObject *)type;
  if (pytype->tp_flags & Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

PyObject *_set_type_static_impl(PyObject *type, int final) {
  PyTypeObject *pytype;
  if (!PyType_Check(type)) {
    PyErr_Format(PyExc_TypeError, "Expected a type object, not %.100s",
                 Py_TYPE(type)->tp_name);
    return NULL;
  }
  pytype = (PyTypeObject *)type;
  pytype->tp_flags |= Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED;

  /* Inheriting a non-static type which inherits a static type is not sound, and
   * we can only catch it at runtime. The compiler can't see the static base
   * through the nonstatic type (which is opaque to it) and thus a) can't verify
   * validity of method and attribute overrides, and b) also can't check
   * statically if this case has occurred. */
  PyObject *mro = pytype->tp_mro;
  PyTypeObject *nonstatic_base = NULL;

  for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(mro); i++) {
    PyTypeObject *next = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
    if (next->tp_flags & Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED) {
      if (nonstatic_base) {
        PyErr_Format(
          PyExc_TypeError,
          "Static compiler cannot verify that static type '%s' is a valid "
          "override of static base '%s' because intervening base '%s' is non-static.",
          pytype->tp_name,
          next->tp_name,
          nonstatic_base->tp_name);
        return NULL;
      }
    } else if (nonstatic_base == NULL) {
      nonstatic_base = next;
    }
  }


  if (pytype->tp_cache != NULL) {
    /* If the v-table was inited because our base class was
     * already inited, it is no longer valid...  we need to include
     * statically defined methods (we'd be better off having custom
     * static class building which knows we're building a static type
     * from the get-go) */
    Py_CLEAR(pytype->tp_cache);
    if (_PyClassLoader_EnsureVtable(pytype, 0) == NULL) {
        return NULL;
    }
  }

  if (final) {
    pytype->tp_flags &= ~Py_TPFLAGS_BASETYPE;
  }
  Py_INCREF(type);
  return type;
}

PyObject *set_type_static(PyObject *mod, PyObject *type) {
    return _set_type_static_impl(type, 0);
}

PyObject *set_type_static_final(PyObject *mod, PyObject *type) {
    return _set_type_static_impl(type, 1);
}

PyObject *set_type_final(PyObject *mod, PyObject *type) {
  if (!PyType_Check(type)) {
    PyErr_Format(PyExc_TypeError, "Expected a type object, not %.100s",
                 Py_TYPE(type)->tp_name);
    return NULL;
  }
  PyTypeObject *pytype = (PyTypeObject *)type;
  pytype->tp_flags &= ~Py_TPFLAGS_BASETYPE;
  Py_INCREF(type);
  return type;
}

static PyObject *
_recreate_cm(PyObject *self) {
    Py_INCREF(self);
    return self;
}

PyObject *make_recreate_cm(PyObject *mod, PyObject *type) {
    static PyMethodDef def = {"_recreate_cm",
        (PyCFunction)&_recreate_cm,
        METH_NOARGS};

     if (!PyType_Check(type)) {
        PyErr_Format(PyExc_TypeError, "Expected a type object, not %.100s",
                    Py_TYPE(type)->tp_name);
         return NULL;
     }


    return PyDescr_NewMethod((PyTypeObject *)type, &def);
}

typedef struct {
    PyWeakReference weakref; /* base weak ref */
    PyObject *func;     /* function that's being wrapped */
    PyObject *ctxdec;   /* the instance of the ContextDecorator class */
    PyObject *enter;    /* borrowed ref to __enter__, valid on cache_version */
    PyObject *exit;     /* borrowed ref to __exit__, valid on cache_version */
    PyObject *recreate_cm; /* borrowed ref to recreate_cm, valid on recreate_cache_version */
    Py_ssize_t cache_version;
    Py_ssize_t recreate_cache_version;
    int is_coroutine;
} _Py_ContextManagerWrapper;

static PyObject *_return_none;

int
ctxmgrwrp_import_value(const char *module, const char *name, PyObject **dest) {
    PyObject *mod = PyImport_ImportModule(module);
    if (mod == NULL) {
        return -1;
    }
    if (*dest == NULL) {
        PyObject *value = PyObject_GetAttrString(mod, name);
        if (value == NULL) {
            return -1;
        }
        *dest = value;
    }
    Py_DECREF(mod);
    return 0;
}


static PyObject *
ctxmgrwrp_exit(int is_coroutine, PyObject *ctxmgr,
               PyObject *result, PyObject *exit)
{
    if (result == NULL) {
        // exception
        PyObject *ret;
        PyObject *exc, *val, *tb;
        PyFrameObject* f = PyEval_GetFrame();
        PyTraceBack_Here(f);
        PyErr_Fetch(&exc, &val, &tb);
        PyErr_NormalizeException(&exc, &val, &tb);
        if (tb == NULL) {
            tb = Py_None;
            Py_INCREF(tb);
        }
        PyException_SetTraceback(val, tb);

        if (ctxmgr != NULL) {
            assert(Py_TYPE(exit)->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR);
            PyObject* stack[] = {(PyObject *)ctxmgr, exc, val, tb};
            ret =  _PyObject_Vectorcall(exit, stack, 4 | Ci_Py_VECTORCALL_INVOKED_METHOD, NULL);
        } else {
            PyObject* stack[] = {exc, val, tb};
            ret =  _PyObject_Vectorcall(exit, stack, 3 | Ci_Py_VECTORCALL_INVOKED_METHOD, NULL);
        }
        if (ret == NULL) {
            Py_DECREF(exc);
            Py_DECREF(val);
            Py_DECREF(tb);
            return NULL;
        }

        int err = PyObject_IsTrue(ret);
        Py_DECREF(ret);
        if (!err) {
            PyErr_Restore(exc, val, tb);
            goto error;
        }

        Py_DECREF(exc);
        Py_DECREF(val);
        Py_DECREF(tb);
        if (err < 0) {
            goto error;
        }

        if (is_coroutine) {
            /* The co-routine needs to yield None instead of raising the exception.  We
             * need to actually produce a co-routine which is going to return None to
             * do that, so we have a helper function which does just that. */
            if (_return_none == NULL &&
                ctxmgrwrp_import_value("__static__", "_return_none", &_return_none)) {
                return NULL;
            }

            PyObject *call_none = _PyObject_CallNoArg(_return_none);
            if (call_none == NULL) {
                return NULL;
            }
            return call_none;
        }
        Py_RETURN_NONE;
    } else {
        PyObject *ret;
        if (ctxmgr != NULL) {
            /* we picked up a method like object and have self for it */
            assert(Py_TYPE(exit)->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR);
            PyObject *stack[] = {(PyObject *) ctxmgr, Py_None, Py_None, Py_None};
            ret = _PyObject_Vectorcall(exit, stack, 4 | Ci_Py_VECTORCALL_INVOKED_METHOD, NULL);
        } else {
            PyObject *stack[] = {Py_None, Py_None, Py_None};
            ret = _PyObject_Vectorcall(exit, stack, 3 | Ci_Py_VECTORCALL_INVOKED_METHOD, NULL);
        }
        if (ret == NULL) {
            goto error;
        }
        Py_DECREF(ret);
    }

    return result;
error:
    Py_XDECREF(result);
    return NULL;
}

static PyObject *
ctxmgrwrp_cb(_PyClassLoader_Awaitable *awaitable, PyObject *result)
{
    /* In the error case our awaitable is done, and if we return a value
     * it'll turn into the returned value, so we don't want to pass iscoroutine
     * because we don't need a wrapper object. */
    if (awaitable->onsend != NULL) {
        /* Send has never happened, so we never called __enter__, so there's
         * no __exit__ to call. */
         return NULL;
    }
    return ctxmgrwrp_exit(result != NULL, NULL, result, awaitable->state);
}

extern int _PyObject_GetMethod(PyObject *, PyObject *, PyObject **);

static PyObject *
get_descr(PyObject *obj, PyObject *self)
{
    descrgetfunc f = Py_TYPE(obj)->tp_descr_get;
    if (f != NULL) {
        return f(obj, self, (PyObject *)Py_TYPE(self));
    }
    Py_INCREF(obj);
    return obj;
}

static PyObject *
call_with_self(PyThreadState *tstate, PyObject *func, PyObject *self)
{
    if (Py_TYPE(func)->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR) {
        PyObject *args[1] = { self };
        return _PyObject_VectorcallTstate(tstate, func, args, 1|Ci_Py_VECTORCALL_INVOKED_METHOD, NULL);
    } else {
        func = get_descr(func, self);
        if (func == NULL) {
            return NULL;
        }
        PyObject *ret = _PyObject_VectorcallTstate(tstate, func, NULL, 0|Ci_Py_VECTORCALL_INVOKED_METHOD, NULL);
        Py_DECREF(func);
        return ret;
    }
}

static PyObject *
ctxmgrwrp_enter(_Py_ContextManagerWrapper *self, PyObject **ctxmgr)
{
    _Py_IDENTIFIER(__exit__);
    _Py_IDENTIFIER(__enter__);
    _Py_IDENTIFIER(_recreate_cm);

    PyThreadState *tstate = _PyThreadState_GET();

    if (self->recreate_cache_version != Py_TYPE(self->ctxdec)->tp_version_tag) {
        self->recreate_cm = _PyType_LookupId(Py_TYPE(self->ctxdec), &PyId__recreate_cm);
        if (self->recreate_cm == NULL) {
            PyErr_Format(PyExc_TypeError, "failed to resolve _recreate_cm on %s",
                        Py_TYPE(self->ctxdec)->tp_name);
            return NULL;
        }

        self->recreate_cache_version = Py_TYPE(self->ctxdec)->tp_version_tag;
    }

    PyObject *ctx_mgr = call_with_self(tstate, self->recreate_cm, self->ctxdec);
    if (ctx_mgr == NULL) {
        return NULL;
    }

    if (self->cache_version != Py_TYPE(ctx_mgr)->tp_version_tag) {
        /* we probably get the same type back from _recreate_cm over and
         * over again, so we cache the lookups for enter and exit */
        self->enter = _PyType_LookupId(Py_TYPE(ctx_mgr), &PyId___enter__);
        self->exit = _PyType_LookupId(Py_TYPE(ctx_mgr), &PyId___exit__);
        if (self->enter == NULL || self->exit == NULL) {
            Py_DECREF(ctx_mgr);
            PyErr_Format(PyExc_TypeError, "failed to resolve context manager on %s",
                        Py_TYPE(ctx_mgr)->tp_name);
            return NULL;
        }

        self->cache_version = Py_TYPE(ctx_mgr)->tp_version_tag;
    }

    PyObject *enter = self->enter;
    PyObject *exit = self->exit;

    Py_INCREF(enter);
    if (!(Py_TYPE(exit)->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR)) {
        /* Descriptor protocol for exit needs to run before we call
         * user code */
        exit = get_descr(exit, ctx_mgr);
        Py_CLEAR(ctx_mgr);
        if (exit == NULL) {
            return NULL;
        }
    } else {
        Py_INCREF(exit);
    }

    PyObject *enter_res = call_with_self(tstate, enter, ctx_mgr);
    Py_DECREF(enter);

    if (enter_res == NULL) {
        goto error;
    }
    Py_DECREF(enter_res);

    *ctxmgr = ctx_mgr;
    return exit;
error:
    Py_DECREF(ctx_mgr);
    return NULL;
}

static int
ctxmgrwrp_first_send(_PyClassLoader_Awaitable *self) {
    /* Handles calling __enter__ on the first step of the co-routine when
     * we're not eagerly evaluated. We'll swap our state over to the exit
     * function from the _Py_ContextManagerWrapper once we're successful */
    _Py_ContextManagerWrapper *ctxmgrwrp = (_Py_ContextManagerWrapper *)self->state;
    PyObject *ctx_mgr;
    PyObject *exit = ctxmgrwrp_enter(ctxmgrwrp, &ctx_mgr);
    Py_DECREF(ctxmgrwrp);
    if (exit == NULL) {
        return -1;
    }
    if (ctx_mgr != NULL) {
        PyObject *bound_exit = get_descr(exit, ctx_mgr);
        if (bound_exit == NULL) {
            return -1;
        }
        Py_DECREF(exit);
        Py_DECREF(ctx_mgr);
        exit = bound_exit;
    }
    self->state = exit;
    return 0;
}

static PyObject *
ctxmgrwrp_make_awaitable(_Py_ContextManagerWrapper *ctxmgrwrp, PyObject *ctx_mgr,
                         PyObject *exit, PyObject *res, int eager)
{
    /* We won't have exit yet if we're not eagerly evaluated, and haven't called
     * __enter__ yet.  In that case we'll setup ctxmgrwrp_first_send to run on
     * the first iteration (with the wrapper as our state)) and then restore the
     * awaitable wrapper to our normal state of having exit as the state after
     * we've called __enter__ */
    if (ctx_mgr != NULL && exit != NULL) {
        PyObject *bound_exit = get_descr(exit, ctx_mgr);
        if (bound_exit == NULL) {
            return NULL;
        }
        Py_DECREF(exit);
        Py_DECREF(ctx_mgr);
        exit = bound_exit;
    }
    res = _PyClassLoader_NewAwaitableWrapper(res,
                                             eager,
                                             exit == NULL ? (PyObject *)ctxmgrwrp : exit,
                                             ctxmgrwrp_cb,
                                             exit == NULL ? ctxmgrwrp_first_send : NULL);
    Py_XDECREF(exit);
    return res;
}

PyTypeObject _PyContextDecoratorWrapper_Type;

static PyObject *
ctxmgrwrp_vectorcall(PyFunctionObject *func, PyObject *const *args,
                     Py_ssize_t nargsf, PyObject *kwargs)
{
    PyWeakReference *wr = (PyWeakReference *)func->func_weakreflist;
    while (wr != NULL && Py_TYPE(wr) != &_PyContextDecoratorWrapper_Type) {
        wr = wr->wr_next;
    }
    if (wr == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "missing weakref");
        return NULL;
    }
    _Py_ContextManagerWrapper *self = (_Py_ContextManagerWrapper *)wr;

    PyObject *ctx_mgr = NULL;
    PyObject *exit = NULL;

    /* If this is a co-routine, and we're not being eagerly evaluated, we cannot
     * start calling __enter__ just yet.  We'll delay that until the first step
     * of the coroutine.  Otherwise we're not a co-routine or we're eagerly
     * awaited in which case we'll call __enter__ now and capture __exit__
     * before any possible side effects to match the normal eval loop */
    if (!self->is_coroutine || nargsf & Ci_Py_AWAITED_CALL_MARKER) {
        exit = ctxmgrwrp_enter(self, &ctx_mgr);
        if (exit == NULL) {
            return NULL;
        }
    }

    /* Call the wrapped function */
    PyObject *res = _PyObject_Vectorcall(self->func, args, nargsf, kwargs);
    /* TODO(T128335015): Enable this when we have async/await support. */
    if (self->is_coroutine && res != NULL) {
        /* If it's a co-routine either pass up the eagerly awaited value or
         * pass out a wrapping awaitable */
        int eager = Ci_PyWaitHandle_CheckExact(res);
        if (eager) {
            Ci_PyWaitHandleObject *handle = (Ci_PyWaitHandleObject *)res;
            if (handle->wh_waiter == NULL) {
                assert(nargsf & Ci_Py_AWAITED_CALL_MARKER && exit != NULL);
                // pass in unwrapped result into exit so it could be released in error case
                PyObject *result = ctxmgrwrp_exit(1, ctx_mgr, handle->wh_coro_or_result, exit);
                Py_DECREF(exit);
                Py_XDECREF(ctx_mgr);
                if (result == NULL) {
                    // wrapped result is released in ctxmgrwrp_exit, now release the waithandle itself
                    Ci_PyWaitHandle_Release((PyObject *)handle);
                    return NULL;
                }
                return res;
            }
        }
        return ctxmgrwrp_make_awaitable(self, ctx_mgr, exit, res, eager);
    }

    if (exit == NULL) {
        assert(self->is_coroutine && res == NULL);
        /* We must have failed producing the coroutine object for the
         * wrapped function, we haven't called __enter__, just report
         * out the error from creating the co-routine */
        return NULL;
    }

    /* Call __exit__ */
    res = ctxmgrwrp_exit(self->is_coroutine, ctx_mgr, res, exit);
    Py_XDECREF(ctx_mgr);
    Py_DECREF(exit);
    return res;
}

static int
ctxmgrwrp_traverse(_Py_ContextManagerWrapper *self, visitproc visit, void *arg)
{
    _PyWeakref_RefType.tp_traverse((PyObject *)self, visit, arg);
    Py_VISIT(self->ctxdec);
    return 0;
}

static int
ctxmgrwrp_clear(_Py_ContextManagerWrapper *self)
{
    _PyWeakref_RefType.tp_clear((PyObject *)self);
    Py_CLEAR(self->ctxdec);
    return 0;
}

static void
ctxmgrwrp_dealloc(_Py_ContextManagerWrapper *self)
{
    ctxmgrwrp_clear(self);
    _PyWeakref_RefType.tp_dealloc((PyObject *)self);
}

PyTypeObject _PyContextDecoratorWrapper_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "context_decorator_wrapper",
    sizeof(_Py_ContextManagerWrapper),
    .tp_base = &_PyWeakref_RefType,
    .tp_dealloc = (destructor)ctxmgrwrp_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)ctxmgrwrp_traverse,
    .tp_clear = (inquiry)ctxmgrwrp_clear,
};

static PyObject *
weakref_callback_impl(PyObject *self, _Py_ContextManagerWrapper *weakref)
{
    /* the weakref provides a callback when the object it's tracking
       is freed.  The only thing holding onto this weakref is the
       function object we're tracking, so we rely upon this callback
       to free the weakref / context mgr wrapper. */
    Py_DECREF(weakref);

    Py_RETURN_NONE;
}

static PyMethodDef _WeakrefCallback = {
    "weakref_callback", (PyCFunction)weakref_callback_impl, METH_O, NULL};


static PyObject *weakref_callback;

PyObject *make_context_decorator_wrapper(PyObject *mod, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 3) {
        PyErr_SetString(PyExc_TypeError, "expected 3 arguments: context decorator, wrapper func, and original func");
        return NULL;
    } else if (PyType_Ready(&_PyContextDecoratorWrapper_Type)) {
        return NULL;
    } else if (!PyFunction_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError, "expected function for argument 2");
        return NULL;
    }

    PyFunctionObject *wrapper_func = (PyFunctionObject *)args[1];
    PyObject *wrapped_func = args[2];

    if (weakref_callback == NULL) {
        weakref_callback = PyCFunction_New(&_WeakrefCallback, NULL);
        if (weakref_callback == NULL) {
            return NULL;
        }
    }

    PyObject *wrargs = PyTuple_New(2);
    if (wrargs == NULL) {
        return NULL;
    }

    PyTuple_SET_ITEM(wrargs, 0, (PyObject *)wrapper_func);
    Py_INCREF(wrapper_func);
    PyTuple_SET_ITEM(wrargs, 1, weakref_callback);
    Py_INCREF(weakref_callback);

    _Py_ContextManagerWrapper *ctxmgr_wrapper = (_Py_ContextManagerWrapper *)_PyWeakref_RefType.tp_new(
        &_PyContextDecoratorWrapper_Type, wrargs, NULL);
    Py_DECREF(wrargs);

    if (ctxmgr_wrapper == NULL) {
        return NULL;
    }

    ctxmgr_wrapper->recreate_cache_version = -1;
    ctxmgr_wrapper->cache_version = -1;
    ctxmgr_wrapper->enter = ctxmgr_wrapper->exit = ctxmgr_wrapper->recreate_cm = NULL;
    ctxmgr_wrapper->ctxdec = args[0];
    Py_INCREF(args[0]);
    ctxmgr_wrapper->func = wrapped_func; /* borrowed, the weak ref will live as long as the function */
    ctxmgr_wrapper->is_coroutine = ((PyCodeObject *)wrapper_func->func_code)->co_flags & CO_COROUTINE;

    wrapper_func->func_weakreflist = (PyObject *)ctxmgr_wrapper;
    wrapper_func->vectorcall = (vectorcallfunc)ctxmgrwrp_vectorcall;

    Py_INCREF(wrapper_func);
    return (PyObject *)wrapper_func;
}

static int64_t
static_rand(PyObject *self)
{
    return rand();
}

Ci_Py_TYPED_SIGNATURE(static_rand, Ci_Py_SIG_INT32, NULL);


static int64_t posix_clock_gettime_ns(PyObject* mod)
{
    struct timespec result;
    int64_t ret;

    clock_gettime(CLOCK_MONOTONIC, &result);
    ret = result.tv_sec * 1e9 + result.tv_nsec;
    return ret;
}

Ci_Py_TYPED_SIGNATURE(posix_clock_gettime_ns, Ci_Py_SIG_INT64, NULL);

static Py_ssize_t
static_property_missing_fget(PyObject *mod, PyObject *self)
{
    PyErr_SetString(PyExc_AttributeError, "unreadable attribute");
    return -1;
}

Ci_Py_TYPED_SIGNATURE(static_property_missing_fget, Ci_Py_SIG_ERROR, &Ci_Py_Sig_Object, NULL);

static Py_ssize_t
static_property_missing_fset(PyObject *mod, PyObject *self, PyObject *val)
{
    PyErr_SetString(PyExc_AttributeError, "can't set attribute");
    return -1;
}

Ci_Py_TYPED_SIGNATURE(static_property_missing_fset, Ci_Py_SIG_ERROR, &Ci_Py_Sig_Object, &Ci_Py_Sig_Object, NULL);


/*
    Static Python compiles cached properties into something like this:
        class C:
            __slots__ = ("x")

            def _x_impl(self): ...

            C.x = cached_property(C._x_impl, C.x)
            del C._x_impl

    The last two lines result in a STORE_ATTR + DELETE_ATTR. However, both those
    opcodes result in us creating a v-table on the C class. That's not correct, because
    the v-table should be created only _after_ `C.x` is assigned (and the impl deleted).

    This function does the job, without going through the v-table creation.
*/
static PyObject *
setup_cached_property_on_type(PyObject *Py_UNUSED(module), PyObject **args, Py_ssize_t nargs)
{
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError, "Expected 4 arguments");
        return NULL;
    }
    PyObject *typ = args[0];
    if (!PyType_Check(typ)) {
        PyErr_SetString(PyExc_TypeError, "Expected a type object as 1st argument");
        return NULL;
    }
    PyObject *property = args[1];
    PyObject *name = args[2];
    if (!PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_TypeError, "Expected str as 3rd argument (name of the cached property)");
        return NULL;
    }
    PyObject *impl_name = args[3];
    if (!PyUnicode_Check(impl_name)) {
        PyErr_SetString(PyExc_TypeError, "Expected str as 4th argument (name of the implementation slot)");
        return NULL;
    }

    // First setup the cached_property
    int res;
    res = _PyObject_GenericSetAttrWithDict(typ, name, property, NULL);
    if (res != 0) {
        return NULL;
    }

    // Next clear the backing slot
    res = _PyObject_GenericSetAttrWithDict(typ, impl_name, NULL, NULL);
    if (res != 0) {
        return NULL;
    }

    PyType_Modified((PyTypeObject*)typ);
    Py_RETURN_NONE;
}

static int
create_overridden_slot_descriptors_with_default(PyTypeObject *type)
{
    PyObject *mro = type->tp_mro;
    if (mro == NULL) {
        return 0;
    }
    Py_ssize_t mro_size = PyTuple_GET_SIZE(mro);
    if (mro_size <= 1) {
        return 0;
    }

    PyObject *slots_with_default = NULL;
    PyTypeObject *next;
    for (Py_ssize_t i = 1; i < mro_size; i++) {
        next = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        if (!(PyType_HasFeature(next, Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED))) {
            continue;
        }
        assert(next->tp_dict != NULL);
        slots_with_default = PyDict_GetItemString(next->tp_dict, "__slots_with_default__");
        break;
    }
    if (slots_with_default == NULL) {
        // Any class built before `__build_class__` is patched won't have a slots_with_default. In
        // order to support bootstrapping, silently allow that to go through.
        return 0;
    }
    if (!PyDict_CheckExact(slots_with_default)) {
        PyErr_Format(PyExc_TypeError,
                     "The `__slots_with_default__` attribute of the class `%s` is not a dict.",
                     type->tp_name);
        return -1;
    }
    PyObject *type_slots = PyDict_GetItemString(type->tp_dict, "__slots_with_default__");
    if (type_slots == NULL) {
        type_slots = type->tp_dict;
    }
    Py_ssize_t i = 0;
    PyObject *name, *default_value;
    while (PyDict_Next(slots_with_default, &i, &name, &default_value)) {
        PyObject *override = PyDict_GetItem(type->tp_dict, name);
        if (override != NULL && Py_TYPE(override)->tp_descr_get != NULL) {
            // If the subclass overrides the base slot with a descriptor, just leave it be.
            continue;
        }
        PyObject *override_default = NULL;
        if (type_slots != NULL && (override_default = PyDict_GetItem(type_slots, name)) != NULL) {
            default_value = override_default;
        }
        PyObject *typed_descriptor = _PyType_Lookup(next, name);
        if (typed_descriptor == NULL ||
              Py_TYPE(typed_descriptor) != &_PyTypedDescriptorWithDefaultValue_Type) {
            PyErr_Format(PyExc_TypeError,
                         "The slot at %R is not a typed descriptor for class `%s`.",
                         name,
                         next->tp_name);
            return -1;
        }
        _PyTypedDescriptorWithDefaultValue *td = (_PyTypedDescriptorWithDefaultValue *) typed_descriptor;
        PyObject *new_typed_descriptor = _PyTypedDescriptorWithDefaultValue_New(td->td_name,
                                                                                td->td_type,
                                                                                td->td_offset,
                                                                                default_value);
        PyDict_SetItem(type->tp_dict, name, new_typed_descriptor);
        Py_DECREF(new_typed_descriptor);
    }
    return 0;
}

static PyObject *
init_subclass(PyObject *self, PyObject *type)
{
    if (!PyType_Check(type)) {
        PyErr_SetString(PyExc_TypeError, "init_subclass expected type");
        return NULL;
    }
    // Validate that no Static Python final methods are overridden.
    PyTypeObject *typ = (PyTypeObject *)type;
    if (_PyClassLoader_IsFinalMethodOverridden(typ->tp_base, typ->tp_dict)) {
        return NULL;
    }
    if (create_overridden_slot_descriptors_with_default(typ) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

// Gets the __build_class__ builtin so that we can defer class creation to it.
// Returns a new reference.
static PyObject *
get_build_class() {
    _Py_IDENTIFIER(__build_class__);
    PyObject *bltins = PyEval_GetBuiltins();
    PyObject *bc;
    if (PyDict_CheckExact(bltins)) {
        bc = _PyDict_GetItemIdWithError(bltins, &PyId___build_class__);
        if (bc == NULL) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_NameError, "__build_class__ not found");
            }
            return NULL;
        }
        Py_INCREF(bc);
    }
    else {
        PyObject *build_class_str = _PyUnicode_FromId(&PyId___build_class__);
        if (build_class_str == NULL)
            return NULL;
        bc = PyObject_GetItem(bltins, build_class_str);
        if (bc == NULL) {
            if (PyErr_ExceptionMatches(PyExc_KeyError))
                PyErr_SetString(PyExc_NameError, "__build_class__ not found");
            return NULL;
        }
    }
    return bc;
}

static int
parse_slot_type(PyObject *name, Py_ssize_t *size)
{
    int primitive = _PyClassLoader_ResolvePrimitiveType(name);

    // In order to support forward references, we can't resolve non-primitive
    // types and verify they are valid at this point, we just assume any
    // non-primitive is an object type.
    if (primitive == -1) {
        PyErr_Clear();
        primitive = TYPED_OBJECT;
    }
    *size = _PyClassLoader_PrimitiveTypeToSize(primitive);
    return _PyClassLoader_PrimitiveTypeToStructMemberType(primitive);
}

PyObject *
get_sortable_slot(PyTypeObject *type, PyObject *name, PyObject *slot_type_descr) {
    PyObject *slot;
    Py_ssize_t slot_size = sizeof(Py_ssize_t);
    PyObject *size_original = PyTuple_New(2);
    if (size_original == NULL) {
        return NULL;
    }

    if (slot_type_descr == NULL) {
        slot_size = sizeof(PyObject *);
        slot_type_descr = PyTuple_New(0);
        if (slot_type_descr == NULL) {
            goto error;
        }
    } else {
        int slot_type = parse_slot_type(slot_type_descr, &slot_size);
        if (slot_type == -1) {
            goto error;
        }

        slot = _PyDict_GetItem_UnicodeExact(type->tp_dict, name);
        if (slot == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "missing slot\n");
            goto error;
        }
        Py_INCREF(slot_type_descr);
    }

    PyObject *name_and_type_descr = PyTuple_New(2);
    if (name_and_type_descr == NULL) {
        Py_DECREF(slot_type_descr);
        goto error;
    }

    Py_INCREF(name);
    PyTuple_SET_ITEM(name_and_type_descr, 0, name);
    PyTuple_SET_ITEM(name_and_type_descr, 1, slot_type_descr);
    slot = name_and_type_descr;

    // We negate slot slot size here so that when we sort the
    // slots the largest members will come first and we naturally
    // get good alignment.  This also allows a single sort which
    // preserves the alphabetical order of slots as well as long as
    // they're the same size.
    PyObject *slot_size_obj = PyLong_FromLong(-slot_size);
    if (slot_size_obj == NULL) {
        Py_DECREF(name_and_type_descr);
        goto error;
    }
    PyTuple_SET_ITEM(size_original, 0, slot_size_obj);
    PyTuple_SET_ITEM(size_original, 1, slot);
    return size_original;
error:
    Py_DECREF(size_original);
    return NULL;
}

static int
type_new_descriptors(const PyObject *slots, PyTypeObject *type, int leaked_type)
{
    PyHeapTypeObject *et = (PyHeapTypeObject *)type;
    Py_ssize_t slotoffset = type->tp_base->tp_basicsize;
    PyObject *dict = type->tp_dict;
    int needs_gc = (type->tp_base->tp_flags & Py_TPFLAGS_HAVE_GC) != 0; /* non-primitive fields require GC */

    _Py_IDENTIFIER(__slots_with_default__);
    PyObject *slots_with_default = _PyDict_GetItemIdWithError(dict, &PyId___slots_with_default__);
    if (slots_with_default == NULL && PyErr_Occurred()) {
        return -1;
    }

    Py_ssize_t nslot = PyTuple_GET_SIZE(slots);
    for (Py_ssize_t i = 0; i < nslot; i++) {
        PyObject *name = PyTuple_GET_ITEM(et->ht_slots, i);
        int slottype;
        Py_ssize_t slotsize;
        if (PyUnicode_Check(name)) {
            needs_gc = 1;
            slottype = T_OBJECT_EX;
            slotsize = sizeof(PyObject *);
        } else if (Py_SIZE(PyTuple_GET_ITEM(name, 1)) == 0) {
            needs_gc = 1;
            slottype = T_OBJECT_EX;
            slotsize = sizeof(PyObject *);
            name = PyTuple_GET_ITEM(name, 0);
        } else {
            /* TODO: it'd be nice to unify with the calls above */
            slottype =
                parse_slot_type(PyTuple_GET_ITEM(name, 1), &slotsize);
            assert(slottype != -1);
            if (slottype == T_OBJECT_EX) {
                /* Add strongly typed reference type descriptor,
                    * add_members will check and not overwrite this new
                    * descriptor  */
                PyObject *default_value = NULL;
                if (slots_with_default != NULL) {
                    default_value = PyDict_GetItemWithError(slots_with_default, PyTuple_GET_ITEM(name, 0));
                }
                if (default_value == NULL && PyErr_Occurred()) {
                    return -1;
                }
                PyObject *descr;
                if (default_value != NULL) {
                    descr = _PyTypedDescriptorWithDefaultValue_New(PyTuple_GET_ITEM(name, 0),
                                                                    PyTuple_GET_ITEM(name, 1),
                                                                    slotoffset,
                                                                    default_value);
                } else {
                    descr = _PyTypedDescriptor_New(PyTuple_GET_ITEM(name, 0),
                                                    PyTuple_GET_ITEM(name, 1),
                                                    slotoffset);
                }

                if (descr == NULL ||
                    PyDict_SetItem(
                        dict, PyTuple_GET_ITEM(name, 0), descr)) {
                    return -1;
                }
                Py_DECREF(descr);

                if (!needs_gc) {
                    int optional, exact;
                    PyTypeObject *resolved_type =
                        _PyClassLoader_ResolveType(PyTuple_GET_ITEM(name, 1), &optional, &exact);

                    if (resolved_type == NULL) {
                        /* this can fail if the type isn't loaded yet, in which case
                            * we need to be pessimistic about whether or not this type
                            * needs gc */
                        PyErr_Clear();
                    }

                    if (resolved_type == NULL ||
                        resolved_type->tp_flags & (Py_TPFLAGS_HAVE_GC|Py_TPFLAGS_BASETYPE)) {
                        needs_gc = 1;
                    }
                    Py_XDECREF(resolved_type);
                }
            }

            name = PyTuple_GET_ITEM(name, 0);
        }

        // find the member that we're updating...  By default we do the base initialization
        // with all of the slots defined, and we're just changing their types and moving
        // them around.
        PyMemberDef *mp = PyHeapType_GET_MEMBERS(et);
        const char *slot_name = PyUnicode_AsUTF8(name);
        for (Py_ssize_t i = 0; i < nslot; i++, mp++) {
            if (strcmp(slot_name, mp->name) == 0) {
                break;
            }
        }

        if (leaked_type && (mp->type != slottype || mp->offset != slotoffset)) {
            // We can't account for all of the references to this type, which
            // means an instance of the type was created, and now we're changing
            // the layout which is dangerous.  Disallow the type definition.
            goto leaked_error;
        }

        mp->type = slottype;
        mp->offset = slotoffset;

        /* __dict__ and __weakref__ are already filtered out */
        assert(strcmp(mp->name, "__dict__") != 0);
        assert(strcmp(mp->name, "__weakref__") != 0);

        slotoffset += slotsize;
    }
    /* Round slotoffset up so any child class layouts start properly aligned. */
    slotoffset = _Py_SIZE_ROUND_UP(slotoffset, sizeof(PyObject *));

    if (type->tp_dictoffset) {
        if (type->tp_base->tp_itemsize == 0) {
            type->tp_dictoffset = slotoffset;
        }
        slotoffset += sizeof(PyObject *);
        needs_gc = 1;
    }

    if (type->tp_weaklistoffset) {
        type->tp_weaklistoffset = slotoffset;
        slotoffset += sizeof(PyObject *);
        needs_gc = 1;
    }

    // We should have checked for leakage earlier...
    if (leaked_type && type->tp_basicsize != slotoffset) {
        goto leaked_error;
    }

    type->tp_basicsize = slotoffset;
    if (!needs_gc) {
        assert(!leaked_type);
        type->tp_flags &= ~Py_TPFLAGS_HAVE_GC;
        // If we don't have GC then our base doesn't either, and we
        // need to undo the switch over to PyObject_GC_Del.
        type->tp_free = type->tp_base->tp_free;
    }
    return 0;

leaked_error:
    PyErr_SetString(PyExc_RuntimeError, "type has leaked, make sure no instances "
                                        "were created before the class initialization "
                                        "was completed and that a meta-class or base "
                                        "class did not register the type externally");
    return -1;
}

int
init_static_type(PyObject *obj, int leaked_type)
{
    PyTypeObject *type = (PyTypeObject *)obj;
    PyMemberDef *mp = PyHeapType_GET_MEMBERS(type);
    Py_ssize_t nslot = Py_SIZE(type);

    _Py_IDENTIFIER(__slot_types__);
    PyObject *slot_types = _PyDict_GetItemIdWithError(type->tp_dict, &PyId___slot_types__);
    if (PyErr_Occurred()) {
        return -1;
    }
    if (slot_types != NULL) {
        if (!PyDict_CheckExact(slot_types)) {
            PyErr_Format(PyExc_TypeError,
                         "__slot_types__ should be a dict");
                return -1;
        }
        if (PyDict_GetItemString(slot_types, "__dict__") ||
            PyDict_GetItemString(slot_types, "__weakref__")) {
            PyErr_Format(PyExc_TypeError,
                         "__slots__ type spec cannot be provided for "
                         "__weakref__ or __dict__");
            return -1;
        }

        PyObject *new_slots = PyList_New(nslot);

        for (Py_ssize_t i = 0; i < nslot; i++, mp++) {
            PyObject *name = PyUnicode_FromString(mp->name);
            PyObject *slot_type_descr = PyDict_GetItem(slot_types, name);
            PyObject *size_original = get_sortable_slot(type, name, slot_type_descr);
            Py_DECREF(name);
            if (size_original == NULL) {
                Py_DECREF(new_slots);
                return -1;
            }

            PyList_SET_ITEM(new_slots, i, size_original);
        }

        if (PyList_Sort(new_slots) == -1) {
            Py_DECREF(new_slots);
            return -1;
        }

        /* convert back to the original values */
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(new_slots); i++) {
            PyObject *val = PyList_GET_ITEM(new_slots, i);

            PyObject *original =
                PyTuple_GET_ITEM(val, PyTuple_GET_SIZE(val) - 1);
            Py_INCREF(original);
            PyList_SET_ITEM(new_slots, i, original);
            Py_DECREF(val);
        }

        PyObject *tuple = PyList_AsTuple(new_slots);
        Py_DECREF(new_slots);
        if (tuple == NULL) {
            return -1;
        }

        Py_SETREF(((PyHeapTypeObject *)type)->ht_slots, tuple);

        if (type_new_descriptors(tuple, type, leaked_type)) {
            return -1;
        }
    }

    if (_PyClassLoader_IsFinalMethodOverridden(type->tp_base, type->tp_dict)) {
        return -1;
    }

    return 0;
}


static PyObject *
_static___build_cinder_class__(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *mkw;

    if (nargs < 4) {
        PyErr_SetString(PyExc_TypeError,
                        "__build_cinder_class__: not enough arguments");
        return NULL;
    }

    mkw = args[2];
    if (mkw != Py_None) {
        if (!PyDict_CheckExact(mkw)) {
            PyErr_SetString(PyExc_TypeError,
                            "__build_cinder_class__: kwargs is not a dict or None");
            return NULL;
        }
    } else {
        mkw = NULL;
    }

    int has_class_cell = PyObject_IsTrue(args[3]);

    PyObject *bc = get_build_class();
    if (bc == NULL) {
        return NULL;
    }

    int kwarg_count = 0;
    if (mkw != NULL) {
        kwarg_count = PyDict_GET_SIZE(mkw);
    }

    // remove the kwarg dict and add the kwargs
    PyObject *call_args[kwarg_count + nargs - 1];
    PyObject *call_names[kwarg_count];
    call_args[0] = args[0]; // func
    call_args[1] = args[1]; // name

    // bases are offset by one due to kwarg dict
    for (Py_ssize_t i = 4; i < nargs; i++) {
        call_args[i - 2] = args[i];
    }

    if (mkw != NULL) {
        Py_ssize_t i = 0, cur = 0;
        PyObject *key, *value;
        while (PyDict_Next(mkw, &i, &key, &value)) {
            call_args[nargs - 2 + cur] = value;
            call_names[cur++] = key;
        }
    }

    PyObject *call_names_tuple = NULL;
    if (kwarg_count != 0) {
        call_names_tuple = _PyTuple_FromArray(call_names, kwarg_count);
        if (call_names_tuple == NULL) {
            goto error;
        }
    }

    PyObject *type = PyObject_Vectorcall(bc, call_args, nargs - 2, call_names_tuple);
    if (type == NULL) {
        goto error;
    }

    int slot_count = 0;
    int leaked_type = 0;
    if (((PyHeapTypeObject *)type)->ht_slots != NULL) {
        slot_count = PyTuple_GET_SIZE(((PyHeapTypeObject *)type)->ht_slots);
    }

    // If we don't have any slots then there's no layout to fixup
    if (slot_count) {
        // Account for things which add extra references...
        if (has_class_cell) {
            slot_count++;
        }
        PyTypeObject *tp = (PyTypeObject *)type;
        if (tp->tp_weaklistoffset && !tp->tp_base->tp_weaklistoffset) {
            slot_count++;
        }
        if (tp->tp_dictoffset && !tp->tp_base->tp_dictoffset) {
            slot_count++;
        }
        // Type by default has 2 references, the one which we'll return, and one which
        // is a circular reference between the type and its MRO
        if (type->ob_refcnt != 2 + slot_count) {
            leaked_type = 1;
        }
    }

    if (!PyType_Check(type)) {
        PyErr_SetString(PyExc_TypeError, "__build_class__ returned non-type for static Python");
        goto error;
    } else if (init_static_type(type, leaked_type) ||
               create_overridden_slot_descriptors_with_default((PyTypeObject *)type) < 0) {
        goto error;
    }
    Py_XDECREF(call_names_tuple);
    Py_DECREF(bc);
    return type;
error:
    Py_XDECREF(call_names_tuple);
    Py_DECREF(bc);
    return NULL;
}

PyObject *resolve_primitive_descr(PyObject *mod, PyObject *descr) {
    int type_code = _PyClassLoader_ResolvePrimitiveType(descr);
    if (type_code < 0) {
        return NULL;
    }
    return PyLong_FromLong(type_code);
}

static PyObject *lookup_native_symbol(PyObject *Py_UNUSED(module),
                                      PyObject **args, Py_ssize_t nargs) {
  if (nargs != 2) {
    PyErr_SetString(PyExc_TypeError,
                    "lookup_native_symbol: Expected 2 arguments");
    return NULL;
  }
  PyObject *lib_name = args[0];
  PyObject *symbol_name = args[1];
  void* addr = _PyClassloader_LookupSymbol(lib_name, symbol_name);
  if (addr == NULL) {
      return NULL;
  }
  return PyLong_FromVoidPtr(addr);
}

PyObject* sizeof_dlopen_cache(PyObject *Py_UNUSED(module)) {
    return _PyClassloader_SizeOf_DlOpen_Cache();
}

PyObject* sizeof_dlsym_cache(PyObject *Py_UNUSED(module)) {
    return _PyClassloader_SizeOf_DlSym_Cache();
}

PyObject* clear_dlopen_cache(PyObject *Py_UNUSED(module)) {
    _PyClassloader_Clear_DlOpen_Cache();
    Py_RETURN_NONE;
}

PyObject* clear_dlsym_cache(PyObject *Py_UNUSED(module)) {
    _PyClassloader_Clear_DlSym_Cache();
    Py_RETURN_NONE;
}

static PyMethodDef static_methods[] = {
    {"set_type_code", (PyCFunction)(void(*)(void))set_type_code, METH_FASTCALL, ""},
    {"rand", (PyCFunction)&static_rand_def, Ci_METH_TYPED, ""},
    {"is_type_static", (PyCFunction)(void(*)(void))is_type_static, METH_O, ""},
    {"set_type_static", (PyCFunction)(void(*)(void))set_type_static, METH_O, ""},
    {"set_type_static_final", (PyCFunction)(void(*)(void))set_type_static_final, METH_O, ""},
    {"set_type_final", (PyCFunction)(void(*)(void))set_type_final, METH_O, ""},
    {"make_recreate_cm", (PyCFunction)(void(*)(void))make_recreate_cm, METH_O, ""},
    {"make_context_decorator_wrapper", (PyCFunction)(void(*)(void))make_context_decorator_wrapper, METH_FASTCALL, ""},
    {"posix_clock_gettime_ns", (PyCFunction)&posix_clock_gettime_ns_def, Ci_METH_TYPED,
     "Returns time in nanoseconds as an int64. Note: Does no error checks at all."},
    {"_property_missing_fget", (PyCFunction)&static_property_missing_fget_def, Ci_METH_TYPED, ""},
    {"_property_missing_fset", (PyCFunction)&static_property_missing_fset_def, Ci_METH_TYPED, ""},
    {"_setup_cached_property_on_type", (PyCFunction)setup_cached_property_on_type, METH_FASTCALL, ""},
    {"resolve_primitive_descr", (PyCFunction)(void(*)(void))resolve_primitive_descr, METH_O, ""},
    {"__build_cinder_class__",
     (PyCFunction)_static___build_cinder_class__,
     METH_FASTCALL,
     ""},
    {"init_subclass",
     (PyCFunction)init_subclass,
     METH_O,
     ""},
    {"lookup_native_symbol", (PyCFunction)(void(*)(void))lookup_native_symbol, METH_FASTCALL, ""},
    {"_sizeof_dlopen_cache", (PyCFunction)(void(*)(void))sizeof_dlopen_cache, METH_FASTCALL, ""},
    {"_sizeof_dlsym_cache", (PyCFunction)(void(*)(void))sizeof_dlsym_cache, METH_FASTCALL, ""},
    {"_clear_dlopen_cache", (PyCFunction)(void(*)(void))clear_dlopen_cache, METH_FASTCALL, ""},
    {"_clear_dlsym_cache", (PyCFunction)(void(*)(void))clear_dlsym_cache, METH_FASTCALL, ""},
    {}
};

static struct PyModuleDef _staticmodule = {PyModuleDef_HEAD_INIT,
                                           "_static",
                                           _static__doc__,
                                           0,
                                           static_methods,
                                           _static_slots,
                                           NULL,
                                           NULL,
                                           NULL};

PyMODINIT_FUNC
PyInit__static(void)
{

    return PyModuleDef_Init(&_staticmodule);
}
