// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/StaticPython/generic_type.h"

#include "pycore_unionobject.h" // _PyUnion_Type
#include "pycore_tuple.h" // _PyTuple_FromArray

#include "cinder/exports.h"

static PyObject *genericinst_cache;

void
_PyClassLoader_ClearGenericTypes()
{
    Py_CLEAR(genericinst_cache);
}

static PyObject *
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

int
_PyClassLoader_TypeDealloc(PyTypeObject *type)
{
    if (type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST) {
        _PyGenericTypeInst *gti = (_PyGenericTypeInst *)type;
        for (Py_ssize_t i = 0; i < gti->gti_size; i++) {
            Py_XDECREF(gti->gti_inst[i].gtp_type);
        }
        Py_XDECREF(gti->gti_gtd);
        return 1;
    }
    return 0;
}

int
_PyClassLoader_TypeTraverse(PyTypeObject *type, visitproc visit, void *arg)
{
    if (type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST) {
        _PyGenericTypeInst *gti = (_PyGenericTypeInst *)type;
        Py_VISIT(gti->gti_gtd);
        for (Py_ssize_t i = 0; i < gti->gti_size; i++) {
            Py_VISIT(gti->gti_inst[i].gtp_type);
        }
    }
    return 0;
}

void
_PyClassLoader_TypeClear(PyTypeObject *type)
{
    if (type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST) {
        _PyGenericTypeInst *gti = (_PyGenericTypeInst *)type;
        Py_CLEAR(gti->gti_gtd);
        for (Py_ssize_t i = 0; i < gti->gti_size; i++) {
            Py_CLEAR(gti->gti_inst[i].gtp_type);
        }
    }
}
