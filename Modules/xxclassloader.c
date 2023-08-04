/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#include "Python.h"
#include "cinder/exports.h"
#include "structmember.h"
#include "pycore_initconfig.h"
#include "pycore_interp.h"
#include "pycore_pystate.h" // _PyInterpreterState_GET()

#include "StaticPython/classloader.h"

PyDoc_STRVAR(xxclassloader__doc__,
             "xxclassloader contains helpers for testing the class loader\n");

/* We link this module statically for convenience.  If compiled as a shared
   library instead, some compilers don't allow addresses of Python objects
   defined in other libraries to be used in static initializers here.  The
   DEFERRED_ADDRESS macro is used to tag the slots where such addresses
   appear; the module init function must fill in the tagged slots at runtime.
   The argument is for documentation -- the macro ignores it.
*/
#define DEFERRED_ADDRESS(ADDR) 0

/* spamobj -- a generic type*/

typedef struct {
    PyObject_HEAD PyObject *state;
    PyObject *str;
    Py_ssize_t val;
    size_t uval;
} spamobject;

static Py_ssize_t
spamobj_error(spamobject *self, Py_ssize_t val)
{
    if (val) {
        PyErr_SetString(PyExc_TypeError, "no way!");
        return -1;
    }
    return 0;
}

static PyObject *
spamobj_getstate(spamobject *self)
{
    if (self->state) {
        Py_INCREF(self->state);
        return self->state;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static void
spamobj_setstate(spamobject *self, PyObject *state)
{
    Py_XDECREF(self->state);
    self->state = state;
    Py_INCREF(state);
}

static PyObject *
spamobj_setstate_untyped(spamobject *self, PyObject *state)
{
    if (!_PyClassLoader_CheckParamType((PyObject *)self, state, 0)) {
        PyErr_SetString(PyExc_TypeError, "bad type");
        return NULL;
    }
    Py_XDECREF(self->state);
    self->state = state;
    Py_INCREF(state);
    Py_RETURN_NONE;
}

static void
spamobj_setstate_optional(spamobject *self, PyObject *state)
{
    if (state == Py_None) {
        return;
    }
    Py_XDECREF(self->state);
    self->state = state;
    Py_INCREF(state);
}

static void
spamobj_setint(spamobject *self, Py_ssize_t val)
{
    self->val = val;
}

static void
spamobj_setint8(spamobject *self, int8_t val)
{
    self->val = val;
}

static void
spamobj_setint16(spamobject *self, int16_t val)
{
    self->val = val;
}

static void
spamobj_setint32(spamobject *self, int32_t val)
{
    self->val = val;
}

static void
spamobj_setuint8(spamobject *self, uint8_t val)
{
    self->uval = val;
}

static void
spamobj_setuint16(spamobject *self, uint16_t val)
{
    self->uval = val;
}

static void
spamobj_setuint32(spamobject *self, uint32_t val)
{
    self->uval = val;
}

static void
spamobj_setuint64(spamobject *self, uint64_t val)
{
    self->uval = val;
}

static Py_ssize_t
spamobj_twoargs(spamobject *self, Py_ssize_t x, Py_ssize_t y)
{
    return x + y;
}

static Py_ssize_t
spamobj_getint(spamobject *self)
{
    return self->val;
}

static int8_t
spamobj_getint8(spamobject *self)
{
    return self->val;
}

static int16_t
spamobj_getint16(spamobject *self)
{
    return self->val;
}

static int32_t
spamobj_getint32(spamobject *self)
{
    return self->val;
}

static uint8_t
spamobj_getuint8(spamobject *self)
{
    return self->uval;
}

static uint16_t
spamobj_getuint16(spamobject *self)
{
    return self->uval;
}

static uint32_t
spamobj_getuint32(spamobject *self)
{
    return self->uval;
}

static uint64_t
spamobj_getuint64(spamobject *self)
{
    return self->uval;
}


static void
spamobj_setstr(spamobject *self, PyObject *str)
{
    Py_XDECREF(self->str);
    self->str = str;
    Py_INCREF(str);
}

static PyObject *
spamobj_getstr(spamobject *self)
{
    if (self->str == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->str);
    return self->str;
}

Ci_Py_TYPED_SIGNATURE(spamobj_getstate, Ci_Py_SIG_TYPE_PARAM_OPT(0), NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setstate, Ci_Py_SIG_VOID, &Ci_Py_Sig_T0, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setstate_optional,
                    Ci_Py_SIG_VOID,
                    &Ci_Py_Sig_T0_Opt,
                    NULL);

Ci_Py_TYPED_SIGNATURE(spamobj_getint, Ci_Py_SIG_SSIZE_T, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setint, Ci_Py_SIG_VOID, &Ci_Py_Sig_SSIZET, NULL);

Ci_Py_TYPED_SIGNATURE(spamobj_getuint64, Ci_Py_SIG_SIZE_T, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setuint64, Ci_Py_SIG_VOID, &Ci_Py_Sig_SIZET, NULL);

Ci_Py_TYPED_SIGNATURE(spamobj_getint8, Ci_Py_SIG_INT8, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setint8, Ci_Py_SIG_VOID, &Ci_Py_Sig_INT8, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_getint16, Ci_Py_SIG_INT16, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setint16, Ci_Py_SIG_VOID, &Ci_Py_Sig_INT16, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_getint32, Ci_Py_SIG_INT32, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setint32, Ci_Py_SIG_VOID, &Ci_Py_Sig_INT32, NULL);

Ci_Py_TYPED_SIGNATURE(spamobj_getuint8, Ci_Py_SIG_UINT8, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setuint8, Ci_Py_SIG_VOID, &Ci_Py_Sig_UINT8, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_getuint16, Ci_Py_SIG_UINT16, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setuint16, Ci_Py_SIG_VOID, &Ci_Py_Sig_UINT16, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_getuint32, Ci_Py_SIG_UINT32, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setuint32, Ci_Py_SIG_VOID, &Ci_Py_Sig_UINT32, NULL);

Ci_Py_TYPED_SIGNATURE(spamobj_getstr, Ci_Py_SIG_STRING, NULL);
Ci_Py_TYPED_SIGNATURE(spamobj_setstr, Ci_Py_SIG_VOID, &Ci_Py_Sig_String, NULL);

Ci_Py_TYPED_SIGNATURE(
    spamobj_twoargs, Ci_Py_SIG_SSIZE_T, &Ci_Py_Sig_SSIZET, &Ci_Py_Sig_SSIZET, NULL);

Ci_Py_TYPED_SIGNATURE(
    spamobj_error, Ci_Py_SIG_ERROR, &Ci_Py_Sig_SSIZET,NULL);

static PyMethodDef spamobj_methods[] = {
    {"error",
     (PyCFunction)&spamobj_error_def,
     Ci_METH_TYPED,
     PyDoc_STR("error() -> raises")},
    {"getstate",
     (PyCFunction)&spamobj_getstate_def,
     Ci_METH_TYPED,
     PyDoc_STR("getstate() -> state")},
    {"setstate",
     (PyCFunction)&spamobj_setstate_def,
     Ci_METH_TYPED,
     PyDoc_STR("setstate(state)")},
    {"setstate_untyped",
     (PyCFunction)&spamobj_setstate_untyped,
     METH_O,
     PyDoc_STR("setstate(state)")},
    {"setstateoptional",
     (PyCFunction)&spamobj_setstate_optional_def,
     Ci_METH_TYPED,
     PyDoc_STR("setstate(state|None)")},
    {"getint",
     (PyCFunction)&spamobj_getint_def,
     Ci_METH_TYPED,
     PyDoc_STR("getint() -> i")},
    {"setint",
     (PyCFunction)&spamobj_setint_def,
     Ci_METH_TYPED,
     PyDoc_STR("setint(i)")},

    {"getint8",
     (PyCFunction)&spamobj_getint8_def,
     Ci_METH_TYPED,
     PyDoc_STR("getint8() -> i")},
    {"setint8",
     (PyCFunction)&spamobj_setint8_def,
     Ci_METH_TYPED,
     PyDoc_STR("setint8(i)")},
    {"getint16",
     (PyCFunction)&spamobj_getint16_def,
     Ci_METH_TYPED,
     PyDoc_STR("getint16() -> i")},
    {"setint16",
     (PyCFunction)&spamobj_setint16_def,
     Ci_METH_TYPED,
     PyDoc_STR("setint16(i)")},
    {"getint32",
     (PyCFunction)&spamobj_getint32_def,
     Ci_METH_TYPED,
     PyDoc_STR("getint32() -> i")},
    {"setint32",
     (PyCFunction)&spamobj_setint32_def,
     Ci_METH_TYPED,
     PyDoc_STR("setint32(i)")},

    {"getuint8",
     (PyCFunction)&spamobj_getuint8_def,
     Ci_METH_TYPED,
     PyDoc_STR("getuint8() -> i")},
    {"setuint8",
     (PyCFunction)&spamobj_setuint8_def,
     Ci_METH_TYPED,
     PyDoc_STR("setuint8(i)")},
    {"getuint16",
     (PyCFunction)&spamobj_getuint16_def,
     Ci_METH_TYPED,
     PyDoc_STR("getuint16() -> i")},
    {"setuint16",
     (PyCFunction)&spamobj_setuint16_def,
     Ci_METH_TYPED,
     PyDoc_STR("setuint16(i)")},
    {"getuint32",
     (PyCFunction)&spamobj_getuint32_def,
     Ci_METH_TYPED,
     PyDoc_STR("getuint32() -> i")},
    {"setuint32",
     (PyCFunction)&spamobj_setuint32_def,
     Ci_METH_TYPED,
     PyDoc_STR("setuint32(i)")},
    {"getuint64",
     (PyCFunction)&spamobj_getuint64_def,
     Ci_METH_TYPED,
     PyDoc_STR("getuint64() -> i")},
    {"setuint64",
     (PyCFunction)&spamobj_setuint64_def,
     Ci_METH_TYPED,
     PyDoc_STR("setuint64(i)")},

    {"getstr",
     (PyCFunction)&spamobj_getstr_def,
     Ci_METH_TYPED,
     PyDoc_STR("getstr() -> s")},
    {"setstr",
     (PyCFunction)&spamobj_setstr_def,
     Ci_METH_TYPED,
     PyDoc_STR("setstr(s)")},
    {"twoargs",
     (PyCFunction)&spamobj_twoargs_def,
     Ci_METH_TYPED,
     PyDoc_STR("twoargs(s)")},
    {"__class_getitem__",
     (PyCFunction)_PyClassLoader_GtdGetItem,
     METH_VARARGS | METH_CLASS,
     NULL},
    {NULL, NULL},
};

static int
spamobj_traverse(spamobject *o, visitproc visit, void *arg)
{
    Py_VISIT(o->state);
    return 0;
}

static int
spamobj_clear(spamobject *o)
{
    Py_CLEAR(o->state);
    return 0;
}

static void
spamobj_dealloc(spamobject *o)
{
    PyObject_GC_UnTrack(o);
    Py_XDECREF(o->state);
    Py_XDECREF(o->str);
    Py_TYPE(o)->tp_free(o);
}

_PyGenericTypeDef spamobj_type = {
    .gtd_type =
        {
            PyVarObject_HEAD_INIT(&PyType_Type, 0) "spamobj[T]",
            sizeof(spamobject),
            0,
            (destructor)spamobj_dealloc, /* tp_dealloc */
            0,                           /* tp_vectorcall_offset */
            0,                           /* tp_getattr */
            0,                           /* tp_setattr */
            0,                           /* tp_as_async */
            0,                           /* tp_repr */
            0,                           /* tp_as_number */
            0,                           /* tp_as_sequence */
            0,                           /* tp_as_mapping */
            0,                           /* tp_hash */
            0,                           /* tp_call */
            0,                           /* tp_str */
            0,                           /* tp_getattro */
            0,                           /* tp_setattro */
            0,                           /* tp_as_buffer */
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
              Ci_Py_TPFLAGS_GENERIC_TYPE_DEF, /* tp_flags */
            0,                               /* tp_doc */
            (traverseproc)spamobj_traverse,  /* tp_traverse */
            (inquiry)spamobj_clear,          /* tp_clear */
            0,                               /* tp_richcompare */
            0,                               /* tp_weaklistoffset */
            0,                               /* tp_iter */
            0,                               /* tp_iternext */
            spamobj_methods,                 /* tp_methods */
            0,                               /* tp_members */
            0,                               /* tp_getset */
            0,                               /* tp_base */
            0,                               /* tp_dict */
            0,                               /* tp_descr_get */
            0,                               /* tp_descr_set */
            0,                               /* tp_dictoffset */
            0,                               /* tp_init */
            PyType_GenericAlloc,             /* tp_alloc */
            0,                               /* tp_new */
            PyObject_GC_Del,                 /* tp_free */
        },
    .gtd_size = 1,
    .gtd_new = PyType_GenericNew,
};

static int
xxclassloader_exec(PyObject *m)
{
    /* Fill in deferred data addresses.  This must be done before
       PyType_Ready() is called.  Note that PyType_Ready() automatically
       initializes the ob.ob_type field to &PyType_Type if it's NULL,
       so it's not necessary to fill in ob_type first. */
    if (PyType_Ready((PyTypeObject *)&spamobj_type) < 0)
        return -1;

    Py_INCREF(&spamobj_type);
    if (PyModule_AddObject(m, "spamobj", (PyObject *)&spamobj_type) < 0)
        return -1;

    return 0;
}

static struct PyModuleDef_Slot xxclassloader_slots[] = {
    {Py_mod_exec, xxclassloader_exec},
    {0, NULL},
};

static int64_t
xxclassloader_foo(PyObject *self)
{
    return 42;
}

Ci_Py_TYPED_SIGNATURE(xxclassloader_foo, Ci_Py_SIG_INT64, NULL);

static int64_t
xxclassloader_bar(PyObject *self, int64_t f)
{
    return f;
}

Ci_Py_TYPED_SIGNATURE(xxclassloader_bar, Ci_Py_SIG_INT64, &Ci_Py_Sig_SIZET, NULL);


static int64_t
xxclassloader_neg(PyObject *self)
{
    return -1;
}

Ci_Py_TYPED_SIGNATURE(xxclassloader_neg, Ci_Py_SIG_INT64, NULL);


static int no_op_visit(PyObject *obj, PyObject *Py_UNUSED(args)) {
    if (PyObject_IS_GC(obj) && PyObject_GC_IsTracked(obj)) {
        return 0;
    }

    /* visit objects not discoverable through GC  */
    if (Py_TYPE(obj)->tp_traverse == 0) {
        return 0;
    }

    /* tp_traverse can not be called for non-heap type object  */
    if (PyType_Check(obj) && !PyType_HasFeature((PyTypeObject*)(obj), Py_TPFLAGS_HEAPTYPE)) {
        return 0;
    }

    Py_TYPE(obj)->tp_traverse(obj, (visitproc)no_op_visit, NULL);

    return 0;
}

#define FROM_GC(g) ((PyObject *)(((PyGC_Head *)g)+1))
#define GEN_HEAD(state, n) (&(state)->generations[n].head)
PyObject* visit_heap(PyObject *module)
{

    PyInterpreterState *interp = _PyInterpreterState_GET();
    struct _gc_runtime_state *state = &interp->gc;

    for (int i = 0; i < NUM_GENERATIONS; i++) {
        PyGC_Head *gc, *list;

        list = GEN_HEAD(state, i);
        for (gc = _PyGCHead_NEXT(list); gc != list; gc = _PyGCHead_NEXT(gc)) {
            no_op_visit(FROM_GC(gc), NULL);
            Py_TYPE(FROM_GC(gc))->tp_traverse(
                FROM_GC(gc), (visitproc)no_op_visit, NULL);
        }

    }
    Py_RETURN_NONE;
}
#undef GEN_HEAD
#undef FROM_GC

static PyObject *
traverse_heap(PyObject *self, PyObject *Py_UNUSED(args))
{
    visit_heap(self);
    Py_RETURN_NONE;
}

PyObject *
unsafe_change_type(PyObject *self, PyObject **args, Py_ssize_t nargs)
{
    PyObject *obj;
    PyObject *new_type;
    if (!_PyArg_ParseStack(args, nargs, "OO:unsafe_change_type", &obj, &new_type)) {
        return NULL;
    }
    if (Py_TYPE(new_type) != &PyType_Type) {
        PyErr_SetString(PyExc_TypeError, "second argument must be a type");
        return NULL;
    }
    PyObject *old_type = (PyObject *)obj->ob_type;
    Py_INCREF(new_type);
    obj->ob_type = (PyTypeObject *)new_type;
    Py_DECREF(old_type);
    Py_RETURN_NONE;
}


static PyMethodDef xxclassloader_methods[] = {
    {"foo", (PyCFunction)&xxclassloader_foo_def, Ci_METH_TYPED, ""},
    {"bar", (PyCFunction)&xxclassloader_bar_def, Ci_METH_TYPED, ""},
    {"neg", (PyCFunction)&xxclassloader_neg_def, Ci_METH_TYPED, ""},
    {"traverse_heap", traverse_heap, METH_NOARGS},
    {"unsafe_change_type", (PyCFunction)(void(*)(void))unsafe_change_type, METH_FASTCALL},
    {}
};

static struct PyModuleDef xxclassloadermodule = {PyModuleDef_HEAD_INIT,
                                                 "xxclassloader",
                                                 xxclassloader__doc__,
                                                 0,
                                                 xxclassloader_methods,
                                                 xxclassloader_slots,
                                                 NULL,
                                                 NULL,
                                                 NULL};

PyMODINIT_FUNC
PyInit_xxclassloader(void)
{
    return PyModuleDef_Init(&xxclassloadermodule);
}
