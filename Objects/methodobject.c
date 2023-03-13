
/* Method object implementation */

#include "Python.h"
#include "cinder/exports.h"
#include "classloader.h"
#include "pycore_ceval.h"         // _Py_EnterRecursiveCall()
#include "pycore_object.h"
#include "pycore_pyerrors.h"
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "structmember.h"         // PyMemberDef

/* undefine macro trampoline to PyCFunction_NewEx */
#undef PyCFunction_New
/* undefine macro trampoline to PyCMethod_New */
#undef PyCFunction_NewEx

/* Forward declarations */
static PyObject * cfunction_vectorcall_FASTCALL(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_FASTCALL_KEYWORDS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_FASTCALL_KEYWORDS_METHOD(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_NOARGS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_O(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_call(
    PyObject *func, PyObject *args, PyObject *kwargs);

#ifdef ENABLE_CINDERVM
static PyObject *Ci_cfunction_vectorcall_typed_0(PyObject *func,
                                              PyObject *const *args,
                                              size_t nargsf,
                                              PyObject *kwnames);
static PyObject *Ci_cfunction_vectorcall_typed_1(PyObject *func,
                                              PyObject *const *args,
                                              size_t nargsf,
                                              PyObject *kwnames);
static PyObject *Ci_cfunction_vectorcall_typed_2(PyObject *func,
                                              PyObject *const *args,
                                              size_t nargsf,
                                              PyObject *kwnames);
#endif

PyObject *
PyCFunction_New(PyMethodDef *ml, PyObject *self)
{
    return PyCFunction_NewEx(ml, self, NULL);
}

PyObject *
PyCFunction_NewEx(PyMethodDef *ml, PyObject *self, PyObject *module)
{
    return PyCMethod_New(ml, self, module, NULL);
}

PyObject *
PyCMethod_New(PyMethodDef *ml, PyObject *self, PyObject *module, PyTypeObject *cls)
{
    /* Figure out correct vectorcall function to use */
    vectorcallfunc vectorcall;
#ifdef ENABLE_CINDERVM
    Ci_PyTypedMethodDef *sig;
#endif
    switch (ml->ml_flags & (METH_VARARGS | METH_FASTCALL | METH_NOARGS |
                            METH_O | METH_KEYWORDS | METH_METHOD | Ci_METH_TYPED))
    {
        case METH_VARARGS:
        case METH_VARARGS | METH_KEYWORDS:
            /* For METH_VARARGS functions, it's more efficient to use tp_call
             * instead of vectorcall. */
            vectorcall = NULL;
            break;
        case METH_FASTCALL:
            vectorcall = cfunction_vectorcall_FASTCALL;
            break;
        case METH_FASTCALL | METH_KEYWORDS:
            vectorcall = cfunction_vectorcall_FASTCALL_KEYWORDS;
            break;
        case METH_NOARGS:
            vectorcall = cfunction_vectorcall_NOARGS;
            break;
        case METH_O:
            vectorcall = cfunction_vectorcall_O;
            break;
        case METH_METHOD | METH_FASTCALL | METH_KEYWORDS:
            vectorcall = cfunction_vectorcall_FASTCALL_KEYWORDS_METHOD;
            break;
 #ifdef ENABLE_CINDERVM
        case Ci_METH_TYPED:
            sig = (Ci_PyTypedMethodDef *)ml->ml_meth;
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
                                "%s() method: unsupported argument count", ml->ml_name, arg_cnt);
                    return NULL;
            }
            break;
#endif
        default:
            PyErr_Format(PyExc_SystemError,
                         "%s() method: bad call flags");
            return NULL;
    }

    PyCFunctionObject *op = NULL;

    if (ml->ml_flags & METH_METHOD) {
        if (!cls) {
            PyErr_SetString(PyExc_SystemError,
                            "attempting to create PyCMethod with a METH_METHOD "
                            "flag but no class");
            return NULL;
        }
        PyCMethodObject *om = PyObject_GC_New(PyCMethodObject, &PyCMethod_Type);
        if (om == NULL) {
            return NULL;
        }
        Py_INCREF(cls);
        om->mm_class = cls;
        op = (PyCFunctionObject *)om;
    } else {
        if (cls) {
            PyErr_SetString(PyExc_SystemError,
                            "attempting to create PyCFunction with class "
                            "but no METH_METHOD flag");
            return NULL;
        }
        op = PyObject_GC_New(PyCFunctionObject, &PyCFunction_Type);
        if (op == NULL) {
            return NULL;
        }
    }

    op->m_weakreflist = NULL;
    op->m_ml = ml;
    Py_XINCREF(self);
    op->m_self = self;
    Py_XINCREF(module);
    op->m_module = module;
    op->vectorcall = vectorcall;
    _PyObject_GC_TRACK(op);
    return (PyObject *)op;
}

PyCFunction
PyCFunction_GetFunction(PyObject *op)
{
    if (!PyCFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return PyCFunction_GET_FUNCTION(op);
}

PyObject *
PyCFunction_GetSelf(PyObject *op)
{
    if (!PyCFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return PyCFunction_GET_SELF(op);
}

int
PyCFunction_GetFlags(PyObject *op)
{
    if (!PyCFunction_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return PyCFunction_GET_FLAGS(op);
}

PyTypeObject *
PyCMethod_GetClass(PyObject *op)
{
    if (!PyCFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return PyCFunction_GET_CLASS(op);
}

/* Methods (the standard built-in methods, that is) */

static void
meth_dealloc(PyCFunctionObject *m)
{
    // The Py_TRASHCAN mechanism requires that we be able to
    // call PyObject_GC_UnTrack twice on an object.
    PyObject_GC_UnTrack(m);
    Py_TRASHCAN_BEGIN(m, meth_dealloc);
    if (m->m_weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject*) m);
    }
    // Dereference class before m_self: PyCFunction_GET_CLASS accesses
    // PyMethodDef m_ml, which could be kept alive by m_self
    Py_XDECREF(PyCFunction_GET_CLASS(m));
    Py_XDECREF(m->m_self);
    Py_XDECREF(m->m_module);
    PyObject_GC_Del(m);
    Py_TRASHCAN_END;
}

static PyObject *
meth_reduce(PyCFunctionObject *m, PyObject *Py_UNUSED(ignored))
{
    _Py_IDENTIFIER(getattr);

    if (m->m_self == NULL || PyModule_Check(m->m_self))
        return PyUnicode_FromString(m->m_ml->ml_name);

    return Py_BuildValue("N(Os)", _PyEval_GetBuiltinId(&PyId_getattr),
                         m->m_self, m->m_ml->ml_name);
}

static PyMethodDef meth_methods[] = {
    {"__reduce__", (PyCFunction)meth_reduce, METH_NOARGS, NULL},
    {NULL, NULL}
};

static PyObject *
meth_get__text_signature__(PyCFunctionObject *m, void *closure)
{
    return _PyType_GetTextSignatureFromInternalDoc(m->m_ml->ml_name, m->m_ml->ml_doc);
}

static int
Ci_populate_type_info(PyObject *arg_info, int argtype)
{
    _Py_IDENTIFIER(NoneType);
    _Py_IDENTIFIER(object);
    _Py_IDENTIFIER(str);
    _Py_static_string(__static__int8, "__static__.int8");
    _Py_static_string(__static__int16, "__static__.int16");
    _Py_static_string(__static__int32, "__static__.int32");
    _Py_static_string(__static__int64, "__static__.int64");
    _Py_static_string(__static__uint8, "__static__.uint8");
    _Py_static_string(__static__uint16, "__static__.uint16");
    _Py_static_string(__static__uint32, "__static__.uint32");
    _Py_static_string(__static__uint64, "__static__.uint64");
    _Py_IDENTIFIER(optional);
    _Py_IDENTIFIER(type_param);
    _Py_IDENTIFIER(type);

    if ((argtype & Ci_Py_SIG_OPTIONAL) &&
        _PyDict_SetItemId(arg_info, &PyId_optional, Py_True)) {
        return -1;
    }

    if (argtype & Ci_Py_SIG_TYPE_PARAM) {
        /* indicate the type parameter */
        PyObject *type = PyLong_FromLong(Ci_Py_SIG_TYPE_MASK(argtype));
        if (_PyDict_SetItemId(arg_info, &PyId_type_param, type)) {
            Py_DECREF(type);
            return -1;
        }
        Py_DECREF(type);
    } else {
        PyObject *name;
        switch (argtype & ~Ci_Py_SIG_OPTIONAL) {
        case Ci_Py_SIG_ERROR:
        case Ci_Py_SIG_VOID:
            name = _PyUnicode_FromId(&PyId_NoneType);
            break;
        case Ci_Py_SIG_OBJECT:
            name = _PyUnicode_FromId(&PyId_object);
            break;
        case Ci_Py_SIG_STRING:
            name = _PyUnicode_FromId(&PyId_str);
            break;
        case Ci_Py_SIG_INT8:
            name = _PyUnicode_FromId(&__static__int8);
            break;
        case Ci_Py_SIG_INT16:
            name = _PyUnicode_FromId(&__static__int16);
            break;
        case Ci_Py_SIG_INT32:
            name = _PyUnicode_FromId(&__static__int32);
            break;
        case Ci_Py_SIG_INT64:
            name = _PyUnicode_FromId(&__static__int64);
            break;
        case Ci_Py_SIG_UINT8:
            name = _PyUnicode_FromId(&__static__uint8);
            break;
        case Ci_Py_SIG_UINT16:
            name = _PyUnicode_FromId(&__static__uint16);
            break;
        case Ci_Py_SIG_UINT32:
            name = _PyUnicode_FromId(&__static__uint32);
            break;
        case Ci_Py_SIG_UINT64:
            name = _PyUnicode_FromId(&__static__uint64);
            break;
        default:
            PyErr_SetString(PyExc_RuntimeError, "unknown type");
            return -1;
        }
        if (name == NULL || _PyDict_SetItemId(arg_info, &PyId_type, name)) {
            return -1;
        }
    }
    return 0;
}

PyObject *
Ci_PyMethodDef_GetTypedSignature(PyMethodDef *method)
{
    _Py_IDENTIFIER(default);
    _Py_IDENTIFIER(type);
    if (!(method->ml_flags & Ci_METH_TYPED)) {
        Py_RETURN_NONE;
    }
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)method->ml_meth;
    PyObject *res = PyDict_New();
    PyObject *args = PyList_New(0);
    if (PyDict_SetItemString(res, "args", args)) {
        Py_DECREF(res);
        return NULL;
    }
    Py_DECREF(args);
    const Ci_Py_SigElement *const *sig = def->tmd_sig;
    while (*sig != NULL) {
        /* each arg is a dictionary */
        PyObject *arg_info = PyDict_New();
        if (arg_info == NULL) {
            Py_DECREF(res);
            return NULL;
        } else if (PyList_Append(args, arg_info)) {
            Py_DECREF(arg_info);
            Py_DECREF(res);
            return NULL;
        }
        Py_DECREF(arg_info); /* kept alive by args list */
        int argtype = (*sig)->se_argtype;
        if (Ci_populate_type_info(arg_info, argtype)) {
            return NULL;
        }

        PyObject *name;
        if ((*sig)->se_name != NULL) {
            name = PyUnicode_FromString((*sig)->se_name);
            if (name == NULL) {
                Py_DECREF(res);
                return NULL;
            } else if (_PyDict_SetItemId(arg_info, &PyId_type, name)) {
                Py_DECREF(name);
                Py_DECREF(res);
                return NULL;
            }
            Py_DECREF(name);
        }

        if ((*sig)->se_default_value != NULL &&
            _PyDict_SetItemId(
                arg_info, &PyId_default, (*sig)->se_default_value)) {
            Py_DECREF(res);
            return NULL;
        }
        sig++;
    }

    PyObject *ret_info = PyDict_New();
    if (ret_info == NULL || PyDict_SetItemString(res, "return", ret_info) ||
        Ci_populate_type_info(ret_info, def->tmd_ret)) {
        Py_XDECREF(ret_info);
        return NULL;
    }
    Py_DECREF(ret_info);

    return res;
}

static PyObject *
Ci_meth_get__typed_signature__(PyCFunctionObject *m, void *closure)
{
    return Ci_PyMethodDef_GetTypedSignature(m->m_ml);
}

static PyObject *
meth_get__doc__(PyCFunctionObject *m, void *closure)
{
    return _PyType_GetDocFromInternalDoc(m->m_ml->ml_name, m->m_ml->ml_doc);
}

static PyObject *
meth_get__name__(PyCFunctionObject *m, void *closure)
{
    return PyUnicode_FromString(m->m_ml->ml_name);
}

static PyObject *
meth_get__qualname__(PyCFunctionObject *m, void *closure)
{
    /* If __self__ is a module or NULL, return m.__name__
       (e.g. len.__qualname__ == 'len')

       If __self__ is a type, return m.__self__.__qualname__ + '.' + m.__name__
       (e.g. dict.fromkeys.__qualname__ == 'dict.fromkeys')

       Otherwise return type(m.__self__).__qualname__ + '.' + m.__name__
       (e.g. [].append.__qualname__ == 'list.append') */
    PyObject *type, *type_qualname, *res;
    _Py_IDENTIFIER(__qualname__);

    if (m->m_self == NULL || PyModule_Check(m->m_self))
        return PyUnicode_FromString(m->m_ml->ml_name);

    type = PyType_Check(m->m_self) ? m->m_self : (PyObject*)Py_TYPE(m->m_self);

    type_qualname = _PyObject_GetAttrId(type, &PyId___qualname__);
    if (type_qualname == NULL)
        return NULL;

    if (!PyUnicode_Check(type_qualname)) {
        PyErr_SetString(PyExc_TypeError, "<method>.__class__."
                        "__qualname__ is not a unicode object");
        Py_XDECREF(type_qualname);
        return NULL;
    }

    res = PyUnicode_FromFormat("%S.%s", type_qualname, m->m_ml->ml_name);
    Py_DECREF(type_qualname);
    return res;
}

static int
meth_traverse(PyCFunctionObject *m, visitproc visit, void *arg)
{
    Py_VISIT(PyCFunction_GET_CLASS(m));
    Py_VISIT(m->m_self);
    Py_VISIT(m->m_module);
    return 0;
}

static PyObject *
meth_get__self__(PyCFunctionObject *m, void *closure)
{
    PyObject *self;

    self = PyCFunction_GET_SELF(m);
    if (self == NULL)
        self = Py_None;
    Py_INCREF(self);
    return self;
}

static PyGetSetDef meth_getsets [] = {
    {"__doc__",  (getter)meth_get__doc__,  NULL, NULL},
    {"__name__", (getter)meth_get__name__, NULL, NULL},
    {"__qualname__", (getter)meth_get__qualname__, NULL, NULL},
    {"__self__", (getter)meth_get__self__, NULL, NULL},
    {"__text_signature__", (getter)meth_get__text_signature__, NULL, NULL},
    {"__typed_signature__", (getter)Ci_meth_get__typed_signature__, NULL, NULL},
    {0}
};

#define OFF(x) offsetof(PyCFunctionObject, x)

static PyMemberDef meth_members[] = {
    {"__module__",    T_OBJECT,     OFF(m_module), 0},
    {NULL}
};

static PyObject *
meth_repr(PyCFunctionObject *m)
{
    if (m->m_self == NULL || PyModule_Check(m->m_self))
        return PyUnicode_FromFormat("<built-in function %s>",
                                   m->m_ml->ml_name);
    return PyUnicode_FromFormat("<built-in method %s of %s object at %p>",
                               m->m_ml->ml_name,
                               Py_TYPE(m->m_self)->tp_name,
                               m->m_self);
}

static PyObject *
meth_richcompare(PyObject *self, PyObject *other, int op)
{
    PyCFunctionObject *a, *b;
    PyObject *res;
    int eq;

    if ((op != Py_EQ && op != Py_NE) ||
        !PyCFunction_Check(self) ||
        !PyCFunction_Check(other))
    {
        Py_RETURN_NOTIMPLEMENTED;
    }
    a = (PyCFunctionObject *)self;
    b = (PyCFunctionObject *)other;
    eq = a->m_self == b->m_self;
    if (eq)
        eq = a->m_ml->ml_meth == b->m_ml->ml_meth;
    if (op == Py_EQ)
        res = eq ? Py_True : Py_False;
    else
        res = eq ? Py_False : Py_True;
    Py_INCREF(res);
    return res;
}

static Py_hash_t
meth_hash(PyCFunctionObject *a)
{
    Py_hash_t x, y;
    x = _Py_HashPointer(a->m_self);
    y = _Py_HashPointer((void*)(a->m_ml->ml_meth));
    x ^= y;
    if (x == -1)
        x = -2;
    return x;
}


PyTypeObject PyCFunction_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "builtin_function_or_method",
    sizeof(PyCFunctionObject),
    0,
    (destructor)meth_dealloc,                   /* tp_dealloc */
    offsetof(PyCFunctionObject, vectorcall),    /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)meth_repr,                        /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    (hashfunc)meth_hash,                        /* tp_hash */
    cfunction_call,                             /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
    Py_TPFLAGS_HAVE_VECTORCALL,                 /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)meth_traverse,                /* tp_traverse */
    0,                                          /* tp_clear */
    meth_richcompare,                           /* tp_richcompare */
    offsetof(PyCFunctionObject, m_weakreflist), /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    meth_methods,                               /* tp_methods */
    meth_members,                               /* tp_members */
    meth_getsets,                               /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
};

PyTypeObject PyCMethod_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "builtin_method",
    .tp_basicsize = sizeof(PyCMethodObject),
    .tp_base = &PyCFunction_Type,
};

/* Vectorcall functions for each of the PyCFunction calling conventions,
 * except for METH_VARARGS (possibly combined with METH_KEYWORDS) which
 * doesn't use vectorcall.
 *
 * First, common helpers
 */

static inline int
cfunction_check_kwargs(PyThreadState *tstate, PyObject *func, PyObject *kwnames)
{
    assert(!_PyErr_Occurred(tstate));
    assert(PyCFunction_Check(func));
    if (kwnames && PyTuple_GET_SIZE(kwnames)) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            _PyErr_Format(tstate, PyExc_TypeError,
                         "%U takes no keyword arguments", funcstr);
            Py_DECREF(funcstr);
        }
        return -1;
    }
    return 0;
}

typedef void (*funcptr)(void);

static inline funcptr
cfunction_enter_call(PyThreadState *tstate, PyObject *func)
{
    if (_Py_EnterRecursiveCall(tstate, " while calling a Python object")) {
        return NULL;
    }
    return (funcptr)PyCFunction_GET_FUNCTION(func);
}

/* Now the actual vectorcall functions */
static PyObject *
cfunction_vectorcall_FASTCALL(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyThreadState *tstate = _PyThreadState_GET();
    if (cfunction_check_kwargs(tstate, func, kwnames)) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    _PyCFunctionFast meth = (_PyCFunctionFast)
                            cfunction_enter_call(tstate, func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), args, nargs);
    _Py_LeaveRecursiveCall(tstate);
    return result;
}

static PyObject *
cfunction_vectorcall_FASTCALL_KEYWORDS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyThreadState *tstate = _PyThreadState_GET();
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    _PyCFunctionFastWithKeywords meth = (_PyCFunctionFastWithKeywords)
                                        cfunction_enter_call(tstate, func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), args, nargs, kwnames);
    _Py_LeaveRecursiveCall(tstate);
    return result;
}

static PyObject *
cfunction_vectorcall_FASTCALL_KEYWORDS_METHOD(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyThreadState *tstate = _PyThreadState_GET();
    PyTypeObject *cls = PyCFunction_GET_CLASS(func);
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    PyCMethod meth = (PyCMethod)cfunction_enter_call(tstate, func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), cls, args, nargs, kwnames);
    _Py_LeaveRecursiveCall(tstate);
    return result;
}

static PyObject *
cfunction_vectorcall_NOARGS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyThreadState *tstate = _PyThreadState_GET();
    if (cfunction_check_kwargs(tstate, func, kwnames)) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 0) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            _PyErr_Format(tstate, PyExc_TypeError,
                "%U takes no arguments (%zd given)", funcstr, nargs);
            Py_DECREF(funcstr);
        }
        return NULL;
    }
    PyCFunction meth = (PyCFunction)cfunction_enter_call(tstate, func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), NULL);
    _Py_LeaveRecursiveCall(tstate);
    return result;
}

static PyObject *
cfunction_vectorcall_O(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyThreadState *tstate = _PyThreadState_GET();
    if (cfunction_check_kwargs(tstate, func, kwnames)) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            _PyErr_Format(tstate, PyExc_TypeError,
                "%U takes exactly one argument (%zd given)", funcstr, nargs);
            Py_DECREF(funcstr);
        }
        return NULL;
    }
    PyCFunction meth = (PyCFunction)cfunction_enter_call(tstate, func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), args[0]);
    _Py_LeaveRecursiveCall(tstate);
    return result;
}


static PyObject *
cfunction_call(PyObject *func, PyObject *args, PyObject *kwargs)
{
    assert(kwargs == NULL || PyDict_Check(kwargs));

    PyThreadState *tstate = _PyThreadState_GET();
    assert(!_PyErr_Occurred(tstate));

    int flags = PyCFunction_GET_FLAGS(func);
    if (!(flags & METH_VARARGS)) {
        /* If this is not a METH_VARARGS function, delegate to vectorcall */
        return PyVectorcall_Call(func, args, kwargs);
    }

    /* For METH_VARARGS, we cannot use vectorcall as the vectorcall pointer
     * is NULL. This is intentional, since vectorcall would be slower. */
    PyCFunction meth = PyCFunction_GET_FUNCTION(func);
    PyObject *self = PyCFunction_GET_SELF(func);

    PyObject *result;
    if (flags & METH_KEYWORDS) {
        result = (*(PyCFunctionWithKeywords)(void(*)(void))meth)(self, args, kwargs);
    }
    else {
        if (kwargs != NULL && PyDict_GET_SIZE(kwargs) != 0) {
            _PyErr_Format(tstate, PyExc_TypeError,
                          "%.200s() takes no keyword arguments",
                          ((PyCFunctionObject*)func)->m_ml->ml_name);
            return NULL;
        }
        result = meth(self, args);
    }
    return _Py_CheckFunctionResult(tstate, func, result, NULL);
}
#ifdef ENABLE_CINDERVM

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
    if (cfunction_check_kwargs(tstate, func, kwnames)) {
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

    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)cfunction_enter_call(tstate, func);
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
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)cfunction_enter_call(tstate, func);
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
    Ci_PyTypedMethodDef *def = (Ci_PyTypedMethodDef *)cfunction_enter_call(tstate, func);
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
#endif
