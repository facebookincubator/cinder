/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include "cinderx/StaticPython/static_array.h"

/**
 *   Lightweight implementation of Static Arrays.
 */

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
