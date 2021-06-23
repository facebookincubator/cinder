
/* Method object implementation */

#include "Python.h"
#include "pycore_object.h"
#include "pycore_pymem.h"
#include "pycore_pystate.h"
#include "structmember.h"
#include "classloader.h"

/* Free list for method objects to safe malloc/free overhead
 * The m_self element is used to chain the objects.
 */
static PyCFunctionObject *free_list = NULL;
static int numfree = 0;
#ifndef PyCFunction_MAXFREELIST
#define PyCFunction_MAXFREELIST 256
#endif

/* undefine macro trampoline to PyCFunction_NewEx */
#undef PyCFunction_New

/* Forward declarations */
static PyObject * cfunction_vectorcall_FASTCALL(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_FASTCALL_KEYWORDS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_NOARGS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject * cfunction_vectorcall_O(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames);
static PyObject *cfunction_vectorcall_typed_0(PyObject *func,
                                              PyObject *const *args,
                                              size_t nargsf,
                                              PyObject *kwnames);
static PyObject *cfunction_vectorcall_typed_1(PyObject *func,
                                              PyObject *const *args,
                                              size_t nargsf,
                                              PyObject *kwnames);
static PyObject *cfunction_vectorcall_typed_2(PyObject *func,
                                              PyObject *const *args,
                                              size_t nargsf,
                                              PyObject *kwnames);

PyObject *
PyCFunction_New(PyMethodDef *ml, PyObject *self)
{
    return PyCFunction_NewEx(ml, self, NULL);
}

PyObject *
PyCFunction_NewEx(PyMethodDef *ml, PyObject *self, PyObject *module)
{
    /* Figure out correct vectorcall function to use */
    vectorcallfunc vectorcall;
    _PyTypedMethodDef *sig;
    switch (ml->ml_flags & (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O | METH_KEYWORDS | METH_TYPED))
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
        case METH_TYPED:
            sig = (_PyTypedMethodDef *)ml->ml_meth;
            Py_ssize_t arg_cnt = 0;
            while (sig->tmd_sig[arg_cnt] != NULL) {
                arg_cnt++;
            }
            switch (arg_cnt) {
                case 0: vectorcall = cfunction_vectorcall_typed_0; break;
                case 1: vectorcall = cfunction_vectorcall_typed_1; break;
                case 2: vectorcall = cfunction_vectorcall_typed_2; break;
                default:
                    PyErr_Format(PyExc_SystemError,
                                "%s() method: unsupported argument count", ml->ml_name, arg_cnt);
                    return NULL;
            }
            break;
        default:
            PyErr_Format(PyExc_SystemError,
                         "%s() method: bad call flags", ml->ml_name);
            return NULL;
    }

    PyCFunctionObject *op;
    op = free_list;
    if (op != NULL) {
        free_list = (PyCFunctionObject *)(op->m_self);
        (void)PyObject_INIT(op, &PyCFunction_Type);
        numfree--;
    }
    else {
        op = PyObject_GC_New(PyCFunctionObject, &PyCFunction_Type);
        if (op == NULL)
            return NULL;
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

/* Methods (the standard built-in methods, that is) */

static void
meth_dealloc(PyCFunctionObject *m)
{
    _PyObject_GC_UNTRACK(m);
    if (m->m_weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject*) m);
    }
    Py_XDECREF(m->m_self);
    Py_XDECREF(m->m_module);
    if (numfree < PyCFunction_MAXFREELIST) {
        m->m_self = (PyObject *)free_list;
        free_list = m;
        numfree++;
    }
    else {
        PyObject_GC_Del(m);
    }
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
populate_type_info(PyObject *arg_info, int argtype)
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

    if ((argtype & _Py_SIG_OPTIONAL) &&
        _PyDict_SetItemId(arg_info, &PyId_optional, Py_True)) {
        return -1;
    }

    if (argtype & _Py_SIG_TYPE_PARAM) {
        /* indicate the type parameter */
        PyObject *type = PyLong_FromLong(_Py_SIG_TYPE_MASK(argtype));
        if (_PyDict_SetItemId(arg_info, &PyId_type_param, type)) {
            Py_DECREF(type);
            return -1;
        }
        Py_DECREF(type);
    } else {
        PyObject *name;
        switch (argtype & ~_Py_SIG_OPTIONAL) {
        case _Py_SIG_ERROR:
        case _Py_SIG_VOID:
            name = _PyUnicode_FromId(&PyId_NoneType);
            break;
        case _Py_SIG_OBJECT:
            name = _PyUnicode_FromId(&PyId_object);
            break;
        case _Py_SIG_STRING:
            name = _PyUnicode_FromId(&PyId_str);
            break;
        case _Py_SIG_INT8:
            name = _PyUnicode_FromId(&__static__int8);
            break;
        case _Py_SIG_INT16:
            name = _PyUnicode_FromId(&__static__int16);
            break;
        case _Py_SIG_INT32:
            name = _PyUnicode_FromId(&__static__int32);
            break;
        case _Py_SIG_INT64:
            name = _PyUnicode_FromId(&__static__int64);
            break;
        case _Py_SIG_UINT8:
            name = _PyUnicode_FromId(&__static__uint8);
            break;
        case _Py_SIG_UINT16:
            name = _PyUnicode_FromId(&__static__uint16);
            break;
        case _Py_SIG_UINT32:
            name = _PyUnicode_FromId(&__static__uint32);
            break;
        case _Py_SIG_UINT64:
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
_PyMethodDef_GetTypedSignature(PyMethodDef *method)
{
    _Py_IDENTIFIER(default);
    _Py_IDENTIFIER(type);
    if (!(method->ml_flags & METH_TYPED)) {
        Py_RETURN_NONE;
    }
    _PyTypedMethodDef *def = (_PyTypedMethodDef *)method->ml_meth;
    PyObject *res = PyDict_New();
    PyObject *args = PyList_New(0);
    if (PyDict_SetItemString(res, "args", args)) {
        Py_DECREF(res);
        return NULL;
    }
    Py_DECREF(args);
    const _Py_SigElement *const *sig = def->tmd_sig;
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
        if (populate_type_info(arg_info, argtype)) {
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
        populate_type_info(ret_info, def->tmd_ret)) {
        Py_XDECREF(ret_info);
        return NULL;
    }
    Py_DECREF(ret_info);

    return res;
}

static PyObject *
meth_get__typed_signature__(PyCFunctionObject *m, void *closure)
{
    return _PyMethodDef_GetTypedSignature(m->m_ml);
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
    {"__typed_signature__", (getter)meth_get__typed_signature__, NULL, NULL},
    {0}
};

#define OFF(x) offsetof(PyCFunctionObject, x)

static PyMemberDef meth_members[] = {
    {"__module__",    T_OBJECT,     OFF(m_module), PY_WRITE_RESTRICTED},
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
                               m->m_self->ob_type->tp_name,
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
    PyVarObject_HEAD_INIT_IMMORTAL(&PyType_Type, 0)
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
    PyCFunction_Call,                           /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
    _Py_TPFLAGS_HAVE_VECTORCALL,                /* tp_flags */
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

/* Clear out the free list */

int
PyCFunction_ClearFreeList(void)
{
    int freelist_size = numfree;

    while (free_list) {
        PyCFunctionObject *v = free_list;
        free_list = (PyCFunctionObject *)(v->m_self);
        PyObject_GC_Del(v);
        numfree--;
    }
    assert(numfree == 0);
    return freelist_size;
}

void
PyCFunction_Fini(void)
{
    (void)PyCFunction_ClearFreeList();
}

/* Print summary info about the state of the optimized allocator */
void
_PyCFunction_DebugMallocStats(FILE *out)
{
    _PyDebugAllocatorStats(out,
                           "free PyCFunctionObject",
                           numfree, sizeof(PyCFunctionObject));
}


/* Vectorcall functions for each of the PyCFunction calling conventions,
 * except for METH_VARARGS (possibly combined with METH_KEYWORDS) which
 * doesn't use vectorcall.
 *
 * First, common helpers
 */
static const char *
get_name(PyObject *func)
{
    assert(PyCFunction_Check(func));
    PyMethodDef *method = ((PyCFunctionObject *)func)->m_ml;
    return method->ml_name;
}

typedef void (*funcptr)(void);

static inline int
cfunction_check_kwargs(PyObject *func, PyObject *kwnames)
{
    assert(!PyErr_Occurred());
    assert(PyCFunction_Check(func));
    if (kwnames && PyTuple_GET_SIZE(kwnames)) {
        PyErr_Format(PyExc_TypeError,
                     "%.200s() takes no keyword arguments", get_name(func));
        return -1;
    }
    return 0;
}

static inline funcptr
cfunction_enter_call(PyObject *func)
{
    if (Py_EnterRecursiveCall(" while calling a Python object")) {
        return NULL;
    }
    return (funcptr)PyCFunction_GET_FUNCTION(func);
}

/* Now the actual vectorcall functions */
static PyObject *
cfunction_vectorcall_FASTCALL(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    if (cfunction_check_kwargs(func, kwnames)) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    _PyCFunctionFast meth = (_PyCFunctionFast)
                            cfunction_enter_call(func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), args, nargs);
    Py_LeaveRecursiveCall();
    return result;
}

static PyObject *
cfunction_vectorcall_FASTCALL_KEYWORDS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    _PyCFunctionFastWithKeywords meth = (_PyCFunctionFastWithKeywords)
                                        cfunction_enter_call(func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), args, nargs, kwnames);
    Py_LeaveRecursiveCall();
    return result;
}

static PyObject *
cfunction_vectorcall_NOARGS(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    if (cfunction_check_kwargs(func, kwnames)) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 0) {
        PyErr_Format(PyExc_TypeError,
            "%.200s() takes no arguments (%zd given)", get_name(func), nargs);
        return NULL;
    }
    PyCFunction meth = (PyCFunction)cfunction_enter_call(func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), NULL);
    Py_LeaveRecursiveCall();
    return result;
}

static PyObject *
cfunction_vectorcall_O(
    PyObject *func, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    if (cfunction_check_kwargs(func, kwnames)) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyErr_Format(PyExc_TypeError,
            "%.200s() takes exactly one argument (%zd given)",
            get_name(func), nargs);
        return NULL;
    }
    PyCFunction meth = (PyCFunction)cfunction_enter_call(func);
    if (meth == NULL) {
        return NULL;
    }
    PyObject *result = meth(PyCFunction_GET_SELF(func), args[0]);
    Py_LeaveRecursiveCall();
    return result;
}

typedef void *(*call_self_0)(PyObject *self);
typedef void *(*call_self_1)(PyObject *self, void *);
typedef void *(*call_self_2)(PyObject *self, void *, void *);

PyObject *
cfunction_vectorcall_typed_0(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames)
{
    if (cfunction_check_kwargs(func, kwnames)) {
        return NULL;
    }

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 0) {
        PyErr_Format(PyExc_TypeError,
                     "%.200s() takes exactly one argument (%zd given)",
                     get_name(func),
                     nargs);
        return NULL;
    }

    _PyTypedMethodDef *def = (_PyTypedMethodDef *)cfunction_enter_call(func);
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
                _PyClassLoader_ArgError(get_name(func), i, i,                 \
                                        def->tmd_sig[i],  self);              \
            }                                                                 \
            goto done;                                                        \
        }                                                                     \
    }

PyObject *
cfunction_vectorcall_typed_1(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 1) {
        PyErr_Format(PyExc_TypeError,
                     "%.200s() takes exactly one argument (%zd given)",
                     get_name(func),
                     nargs);
        return NULL;
    }

    _PyTypedMethodDef *def = (_PyTypedMethodDef *)cfunction_enter_call(func);
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
cfunction_vectorcall_typed_2(PyObject *func,
                             PyObject *const *args,
                             size_t nargsf,
                             PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs != 2) {
        PyErr_Format(PyExc_TypeError,
                     "%.200s() takes exactly 2 argument s(%zd given)",
                     get_name(func),
                     nargs);
        return NULL;
    }

    _PyTypedMethodDef *def = (_PyTypedMethodDef *)cfunction_enter_call(func);
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
