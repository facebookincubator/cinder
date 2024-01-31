/* Copyright (c) Meta Platforms, Inc. and affiliates. */
#include "Python.h"
#include "structmember.h"

#include "internal/pycore_tuple.h"

#include "cinder/exports.h"

/* facebook begin */

static PyTypeObject memoize_func_wrapper_type;
typedef struct memoize_func_wrapper_object memoize_func_wrapper_object;

/*[clinic input]
module memoize
class memoize.memoize_func_wrapper "memoize_func_wrapper_object *" "&memoize_func_wrapper_type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=6233ec6ea450373c]*/

/*  args and keywords delimiter in the cache keys */
static PyObject *kwd_mark = NULL;

#include "clinic/memoizemodule.c.h"

static inline Py_ssize_t
compute_key_size(Py_ssize_t nargs, PyObject *kwnames)
{
    Py_ssize_t key_size = nargs;
    Py_ssize_t kw_count = 0;
    if (kwnames && (kw_count = PyTuple_GET_SIZE(kwnames))) {
        // 2 = names/values
        // final + 1 is kwd_mark
        key_size += kw_count * 2 + 1;
    }
    // +1 for function
    key_size += 1;
    return key_size;
}

static void
fill_key_buffer(PyObject *func,
                PyObject **args,
                Py_ssize_t nargs,
                PyObject *kwnames,
                PyObject **out,
                Py_ssize_t out_size)
{
    Py_ssize_t kw_count = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
    // copy func
    out[0] = func;
    // copy args & kwargs values
    memcpy(out + 1, args, (nargs + kw_count) * sizeof(PyObject *));
    Py_ssize_t key_pos = 1 + nargs + kw_count;
    // copy kwargs names
    if (kw_count) {
        out[key_pos++] = kwd_mark;
        for (Py_ssize_t i = 0; i < kw_count; ++i) {
            out[key_pos++] = PyTuple_GET_ITEM(kwnames, i);
        }
    }
    assert(key_pos == out_size);
}

PyDoc_STRVAR(memoize_wrapper_doc,
"Create a callable that wraps a user function and a callable cache_fetcher\n\
cache_fetcher() must return an object of dict type, to cache user function results.\n\
\n\
func:      the user function being memoized\n\
\n\
cache_fetcher:  callable that returns the cache\n"
);

struct memoize_func_wrapper_object {
    PyObject_HEAD
    vectorcallfunc vectorcall;
    PyObject *cache_fetcher;
    PyObject *func;
    PyObject *dict;
};

static int
memoize_wrapper_tp_traverse(memoize_func_wrapper_object *self, visitproc visit, void *arg)
{
    Py_VISIT(self->func);
    Py_VISIT(self->cache_fetcher);
    Py_VISIT(self->dict);
    return 0;
}

static int
memoize_wrapper_tp_clear(memoize_func_wrapper_object *self)
{
    Py_CLEAR(self->func);
    Py_CLEAR(self->cache_fetcher);
    Py_CLEAR(self->dict);
    return 0;
}

static void
memoize_wrapper_dealloc(memoize_func_wrapper_object *self)
{
    /* bpo-31095: UnTrack is needed before calling any callbacks */
    PyObject_GC_UnTrack(self);
    memoize_wrapper_tp_clear(self);
    PyObject_GC_Del(self);
}

static PyObject *
memoize_wrapper_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    if (obj == Py_None || obj == NULL) {
        Py_INCREF(self);
        return self;
    }
    return PyMethod_New(self, obj);
}

static PyGetSetDef memoize_func_wrapper_getsetlist[] = {
    {"__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict},
    {NULL}
};

static PyTypeObject memoize_func_wrapper_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "memoize.memoize_func_wrapper",       /* tp_name */
    sizeof(memoize_func_wrapper_object),  /* tp_basicsize */
    0,                                    /* tp_itemsize */
    /* methods */
    (destructor)memoize_wrapper_dealloc,          /* tp_dealloc */
    offsetof(memoize_func_wrapper_object, vectorcall), /* tp_vectorcall_offset */
    0,                                    /* tp_getattr */
    0,                                    /* tp_setattr */
    0,                                    /* tp_as_async */
    0,                                    /* tp_repr */
    0,                                    /* tp_as_number */
    0,                                    /* tp_as_sequence */
    0,                                    /* tp_as_mapping */
    0,                                    /* tp_hash */
    PyVectorcall_Call,                    /* tp_call */
    0,                                    /* tp_str */
    0,                                    /* tp_getattro */
    0,                                    /* tp_setattro */
    0,                                    /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_METHOD_DESCRIPTOR |
    _Py_TPFLAGS_HAVE_VECTORCALL,
    /* tp_flags */
    memoize_wrapper_doc,                       /* tp_doc */
    (traverseproc)memoize_wrapper_tp_traverse, /* tp_traverse */
    (inquiry)memoize_wrapper_tp_clear,         /* tp_clear */
    0,                                    /* tp_richcompare */
    0,                                    /* tp_weaklistoffset */
    0,                                    /* tp_iter */
    0,                                    /* tp_iternext */
    0,                                    /* tp_methods */
    0,                                    /* tp_members */
    memoize_func_wrapper_getsetlist,      /* tp_getset */
    0,                                    /* tp_base */
    0,                                    /* tp_dict */
    memoize_wrapper_descr_get,            /* tp_descr_get */
    0,                                    /* tp_descr_set */
    offsetof(memoize_func_wrapper_object, dict), /* tp_dictoffset */
    memoize_memoize_func_wrapper___init__, /* tp_init */
    0,                                    /* tp_alloc */
    PyType_GenericNew,                    /* tp_new */
};

static PyObject *
func_memoize_wrapper_impl(memoize_func_wrapper_object *self,
                            PyObject **args,
                            ssize_t nargsf,
                            PyObject *kwnames,
                            PyObject **cache_key,
                            Py_ssize_t cache_keysize)
{
    PyObject *result = NULL;
    PyObject *keyobj = NULL;

    PyObject* cache = _PyObject_Vectorcall(self->cache_fetcher, NULL, 0, NULL);
    if (cache == NULL) {
        return NULL;
    }
    if (!PyDict_Check(cache)) {
        PyErr_SetString(PyExc_TypeError,
                        "cache_fetcher must return a dictionary");
        goto exit;
    }
    Py_hash_t hash = Ci_TupleHashItems(cache_key, cache_keysize);
    if (hash == -1) {
        goto exit;
    }
    result = _PyDict_GetItem_StackKnownHash(cache, cache_key, cache_keysize, hash);
    if (result != NULL || PyErr_Occurred()) {
        Py_XINCREF(result);
        goto exit; // either cache hit or error
    }
    keyobj = _PyTuple_FromArray(cache_key, cache_keysize);
    if (keyobj == NULL) {
        goto exit;
    }
    result = _PyObject_Vectorcall(self->func, args, nargsf, kwnames);
    if (result == NULL) {
        goto exit;
    }
    if (_PyDict_SetItem_KnownHash(cache, keyobj, result, hash) < 0) {
      Py_CLEAR(result);
    }
exit:
    Py_DECREF(cache);
    Py_XDECREF(keyobj);
    return result;
}

static PyObject *
func_memoize_wrapper(memoize_func_wrapper_object *self,
                           PyObject **args,
                           ssize_t nargsf,
                           PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    PyObject **cache_key; // cache_key=(func, *args, **kwargs)
    Py_ssize_t cache_keysize;
    PyObject *tmp = NULL;

    if (kwnames == NULL && nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET) {
        cache_key = args - 1;
        tmp = cache_key[0]; // save old value at offset - 1
        cache_keysize = nargs + 1;
        cache_key[0] = self->func;
    }
    else {
        cache_keysize = compute_key_size(nargs, kwnames);
        cache_key = alloca(cache_keysize * sizeof(PyObject *));
        fill_key_buffer(self->func, args, nargs, kwnames, cache_key, cache_keysize);
    }

    PyObject *result = func_memoize_wrapper_impl(self, args, nargsf, kwnames, cache_key, cache_keysize);
    if (tmp != NULL) {
        cache_key[0] = tmp; // restore old key value
    }
    return result;
}


/*[clinic input]
memoize.memoize_func_wrapper.__init__
    user_function as func: object
    cache_fetcher: object
[clinic start generated code]*/

static int
memoize_memoize_func_wrapper___init___impl(memoize_func_wrapper_object *self,
                                           PyObject *func,
                                           PyObject *cache_fetcher)
/*[clinic end generated code: output=05b2dd77674fabe5 input=9b270f2e9432cbb7]*/
{
    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError,
                        "func must be callable");
        return -1;
    }

    if (!PyCallable_Check(cache_fetcher)) {
        PyErr_SetString(PyExc_TypeError,
                        "cache_fetcher must be callable");
        return -1;
    }

    vectorcallfunc vectorcall;
    vectorcall = (vectorcallfunc)func_memoize_wrapper;
    self->vectorcall = vectorcall;
    Py_INCREF(func);
    self->func = func;
    Py_INCREF(cache_fetcher);
    self->cache_fetcher=cache_fetcher;
    return 0;
}

/* module level code ********************************************************/

PyDoc_STRVAR(module_doc,
"Functions that support memoization");

static void
module_free(void *m)
{
    Py_CLEAR(kwd_mark);
}

static struct PyModuleDef memoizemodule = {
    PyModuleDef_HEAD_INIT,
    "memoize",
    module_doc,
    -1,
    NULL,
    NULL,
    NULL,
    NULL,
    module_free,
};

PyMODINIT_FUNC
PyInit_memoize(void)
{
    PyObject *m = PyState_FindModule(&memoizemodule);
    if (m != NULL) {
        Py_INCREF(m);
        return m;
    }

    m = PyModule_Create(&memoizemodule);
    if (m == NULL) {
        return NULL;
    }

    kwd_mark = _PyObject_CallNoArg((PyObject *)&PyBaseObject_Type);
    if (!kwd_mark) {
        goto exit;
    }
    if (PyType_Ready(&memoize_func_wrapper_type) < 0) {
        goto exit;
    }
    const char *name = _PyType_Name(&memoize_func_wrapper_type);
    if (PyModule_AddObject(m, name, (PyObject *)&memoize_func_wrapper_type) < 0) {
        goto exit;
    }
    Py_INCREF(&memoize_func_wrapper_type);
    return m;
exit:
    Py_DECREF(m);
    return NULL;
}
/* facebook end */
