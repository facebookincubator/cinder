/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */

#include "Python.h"
#include "boolobject.h"
#include "dictobject.h"
#include "frameobject.h"
#include "funcobject.h"
#include "import.h"
#include "methodobject.h"
#include "object.h"
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


    SET_TYPE_CODE(SEQ_LIST)
    SET_TYPE_CODE(SEQ_TUPLE)
    SET_TYPE_CODE(SEQ_LIST_INEXACT)
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

    Ci_PyHeapType_CINDER_EXTRA(type)->type_code = code;
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
    if (!PyTuple_CheckExact(slots_with_default)) {
        PyErr_Format(PyExc_TypeError,
                     "The `__slots_with_default__` attribute of the class `%s` is not a tuple.",
                     type->tp_name);
        return -1;
    }
    Py_ssize_t nslots_with_default = PyTuple_GET_SIZE(slots_with_default);
    for (Py_ssize_t i = 0; i < nslots_with_default; ++i) {
        PyObject *name = PyTuple_GET_ITEM(slots_with_default, i);
        PyObject *default_value = PyObject_GetAttr((PyObject *)type, name);
        if (default_value == NULL) {
            PyObject *exc, *val, *tb;
            PyErr_Fetch(&exc, &val, &tb);
            PyErr_Format(PyExc_TypeError,
                         "The `slot_with_default` at %s is missing when creating class `%s`.",
                         type->tp_name);
            _PyErr_ChainExceptions(exc, val, tb);
            return -1;
        }
        if (Py_TYPE(default_value)->tp_descr_get != NULL) {
            // If the subclass overrides the base slot with a descriptor, just leave it be.
            Py_DECREF(default_value);
            continue;
        }
        PyObject *typed_descriptor = _PyType_Lookup(next, name);
        if (typed_descriptor == NULL ||
              Py_TYPE(typed_descriptor) != &_PyTypedDescriptorWithDefaultValue_Type) {
            Py_DECREF(default_value);
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
        Py_DECREF(default_value);
    }
    return 0;
}

_Py_IDENTIFIER(__prepare__);
_Py_IDENTIFIER(__mro_entries__);
_Py_IDENTIFIER(metaclass);

static PyObject*
update_bases(PyObject *bases, PyObject *const *args, Py_ssize_t nargs)
{
    Py_ssize_t i, j;
    PyObject *base, *meth, *new_base, *result, *new_bases = NULL;
    PyObject *stack[1] = {bases};
    assert(PyTuple_Check(bases));

    for (i = 0; i < nargs; i++) {
        base  = args[i];
        if (PyType_Check(base)) {
            if (new_bases) {
                /* If we already have made a replacement, then we append every normal base,
                   otherwise just skip it. */
                if (PyList_Append(new_bases, base) < 0) {
                    goto error;
                }
            }
            continue;
        }
        if (_PyObject_LookupAttrId(base, &PyId___mro_entries__, &meth) < 0) {
            goto error;
        }
        if (!meth) {
            if (new_bases) {
                if (PyList_Append(new_bases, base) < 0) {
                    goto error;
                }
            }
            continue;
        }
        new_base = _PyObject_FastCall(meth, stack, 1);
        Py_DECREF(meth);
        if (!new_base) {
            goto error;
        }
        if (!PyTuple_Check(new_base)) {
            PyErr_SetString(PyExc_TypeError,
                            "__mro_entries__ must return a tuple");
            Py_DECREF(new_base);
            goto error;
        }
        if (!new_bases) {
            /* If this is a first successful replacement, create new_bases list and
               copy previously encountered bases. */
            if (!(new_bases = PyList_New(i))) {
                goto error;
            }
            for (j = 0; j < i; j++) {
                base = args[j];
                PyList_SET_ITEM(new_bases, j, base);
                Py_INCREF(base);
            }
        }
        j = PyList_GET_SIZE(new_bases);
        if (PyList_SetSlice(new_bases, j, j, new_base) < 0) {
            goto error;
        }
        Py_DECREF(new_base);
    }
    if (!new_bases) {
        return bases;
    }
    result = PyList_AsTuple(new_bases);
    Py_DECREF(new_bases);
    return result;

error:
    Py_XDECREF(new_bases);
    return NULL;
}

static PyObject *
_static___build_cinder_class__(PyObject *self, PyObject *const *args, Py_ssize_t nargs,
                               PyObject *kwnames)
{
    PyObject *func, *name, *bases, *mkw, *meta, *winner, *prep, *ns, *orig_bases;
    PyObject *cls = NULL, *cell = NULL;
    int isclass = 0;   /* initialize to prevent gcc warning */

    if (nargs < 2) {
        PyErr_SetString(PyExc_TypeError,
                        "__build_cinder_class__: not enough arguments");
        return NULL;
    }
    func = args[0];   /* Better be callable */
    if (!PyFunction_Check(func)) {
        PyErr_SetString(PyExc_TypeError,
                        "__build_cinder_class__: func must be a function");
        return NULL;
    }
    name = args[1];
    if (!PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_TypeError,
                        "__build_cinder_class__: name is not a string");
        return NULL;
    }
    orig_bases = _PyTuple_FromArray(args + 2, nargs - 2);
    if (orig_bases == NULL)
        return NULL;

    bases = update_bases(orig_bases, args + 2, nargs - 2);
    if (bases == NULL) {
        Py_DECREF(orig_bases);
        return NULL;
    }

    if (kwnames == NULL) {
        meta = NULL;
        mkw = NULL;
    }
    else {
        mkw = _PyStack_AsDict(args + nargs, kwnames);
        if (mkw == NULL) {
            Py_DECREF(bases);
            return NULL;
        }

        meta = _PyDict_GetItemIdWithError(mkw, &PyId_metaclass);
        if (meta != NULL) {
            Py_INCREF(meta);
            if (_PyDict_DelItemId(mkw, &PyId_metaclass) < 0) {
                Py_DECREF(meta);
                Py_DECREF(mkw);
                Py_DECREF(bases);
                return NULL;
            }
            /* metaclass is explicitly given, check if it's indeed a class */
            isclass = PyType_Check(meta);
        }
        else if (PyErr_Occurred()) {
            Py_DECREF(mkw);
            Py_DECREF(bases);
            return NULL;
        }
    }
    if (meta == NULL) {
        /* if there are no bases, use type: */
        if (PyTuple_GET_SIZE(bases) == 0) {
            meta = (PyObject *) (&PyType_Type);
        }
        /* else get the type of the first base */
        else {
            PyObject *base0 = PyTuple_GET_ITEM(bases, 0);
            meta = (PyObject *) (base0->ob_type);
        }
        Py_INCREF(meta);
        isclass = 1;  /* meta is really a class */
    }

    if (isclass) {
        /* meta is really a class, so check for a more derived
           metaclass, or possible metaclass conflicts: */
        winner = (PyObject *)_PyType_CalculateMetaclass((PyTypeObject *)meta,
                                                        bases);
        if (winner == NULL) {
            Py_DECREF(meta);
            Py_XDECREF(mkw);
            Py_DECREF(bases);
            return NULL;
        }
        if (winner != meta) {
            Py_DECREF(meta);
            meta = winner;
            Py_INCREF(meta);
        }
    }
    /* else: meta is not a class, so we cannot do the metaclass
       calculation, so we will use the explicitly given object as it is */
    if (_PyObject_LookupAttrId(meta, &PyId___prepare__, &prep) < 0) {
        ns = NULL;
    }
    else if (prep == NULL) {
        ns = PyDict_New();
    }
    else {
        PyObject *pargs[2] = {name, bases};
        ns = _PyObject_FastCallDict(prep, pargs, 2, mkw);
        Py_DECREF(prep);
    }
    if (ns == NULL) {
        Py_DECREF(meta);
        Py_XDECREF(mkw);
        Py_DECREF(bases);
        return NULL;
    }
    if (!PyMapping_Check(ns)) {
        PyErr_Format(PyExc_TypeError,
                     "%.200s.__prepare__() must return a mapping, not %.200s",
                     isclass ? ((PyTypeObject *)meta)->tp_name : "<metaclass>",
                     Py_TYPE(ns)->tp_name);
        goto error;
    }
    cell = PyEval_EvalCodeEx(PyFunction_GET_CODE(func), PyFunction_GET_GLOBALS(func), ns,
                             NULL, 0, NULL, 0, NULL, 0, NULL,
                             PyFunction_GET_CLOSURE(func));
    if (cell != NULL) {
        if (bases != orig_bases) {
            if (PyMapping_SetItemString(ns, "__orig_bases__", orig_bases) < 0) {
                goto error;
            }
        }
        PyObject *margs[3] = {name, bases, ns};
        cls = _PyObject_FastCallDict(meta, margs, 3, mkw);
        if (cls != NULL && PyType_Check(cls) && PyCell_Check(cell)) {
            PyObject *cell_cls = PyCell_GET(cell);
            if (cell_cls != cls) {
                if (cell_cls == NULL) {
                    const char *msg =
                        "__class__ not set defining %.200R as %.200R. "
                        "Was __classcell__ propagated to type.__new__?";
                    PyErr_Format(PyExc_RuntimeError, msg, name, cls);
                } else {
                    const char *msg =
                        "__class__ set to %.200R defining %.200R as %.200R";
                    PyErr_Format(PyExc_TypeError, msg, cell_cls, name, cls);
                }
                Py_DECREF(cls);
                cls = NULL;
                goto error;
            }
        }
    }
    if (cls != NULL && create_overridden_slot_descriptors_with_default((PyTypeObject *) cls) < 0) {
        Py_DECREF(cls);
        cls = NULL;
        goto error;
    }
error:
    Py_XDECREF(cell);
    Py_DECREF(ns);
    Py_DECREF(meta);
    Py_XDECREF(mkw);
    Py_DECREF(bases);
    if (bases != orig_bases) {
        Py_DECREF(orig_bases);
    }
    return cls;
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
     METH_FASTCALL | METH_KEYWORDS,
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
