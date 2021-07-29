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
    Py_XDECREF(op->vt_funcdict);

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
    return 0;
}

static int
vtableclear(_PyType_VTable *op)
{
    for (Py_ssize_t i = 0; i < op->vt_size; i++) {
        Py_CLEAR(op->vt_entries[i].vte_state);
    }
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

static PyObject *
rettype_check(PyObject *self, PyObject *ret, PyObject *state)
{
    if (ret == NULL) {
        return NULL;
    }

    PyObject *expected = PyTuple_GET_ITEM(state, 2);
    int optional = PyTuple_GET_ITEM(state, 3) == Py_True;

    int type_code = _PyClassLoader_GetTypeCode((PyTypeObject *)expected);
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
                return NULL;
        }
    }

    if (overflow || !((optional && ret == Py_None) ||
                         _PyObject_RealIsInstance(ret, expected))) {
        PyObject *name = PyTuple_GET_ITEM(state, 1);
        /* The override returned an incompatible value, report error */
        const char *msg;
        PyObject *exc_type = PyExc_TypeError;
        if (overflow) {
            exc_type = PyExc_OverflowError;
            msg = "unexpected return type from %s.%U, expected %s, got out-of-range %s (%R)";
        } else if (optional) {
            msg = "unexpected return type from %s.%U, expected  Optional[%s], "
                  "got %s";
        } else {
            msg = "unexpected return type from %s.%U, expected %s, got %s";
        }

        PyErr_Format(exc_type,
                     msg,
                     Py_TYPE(self)->tp_name,
                     name,
                     ((PyTypeObject *)expected)->tp_name,
                     Py_TYPE(ret)->tp_name,
                     ret);

        Py_DECREF(ret);
        return NULL;
    }
    return ret;
}

static PyObject *
type_vtable_nonfunc(PyObject *state,
                    PyObject **args,
                    size_t nargsf,
                    PyObject *kwnames)
{

    PyObject *self = args[0];
    PyObject *descr = PyTuple_GET_ITEM(state, 0);
    PyObject *name = PyTuple_GET_ITEM(state, 1);
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
    return rettype_check(self, res, state);
}

static PyObject *
type_vtable_func_overridable(PyObject *state,
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
        PyObject *name = PyTuple_GET_ITEM(state, 1);
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

    PyFunctionObject *func = (PyFunctionObject *)PyTuple_GET_ITEM(state, 0);
    res = func->vectorcall((PyObject *)func, args, nargsf, kwnames);

done:
    return rettype_check(self, res, state);
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
type_vtable_func_missing(PyObject *state, PyObject **args, Py_ssize_t nargs)
{
    PyObject *self = args[0];
    PyObject *name = PyTuple_GET_ITEM(state, 0);

    PyErr_Format(PyExc_AttributeError,
                 "%s object has no attribute %U",
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

PyTypeObject *
_PyClassLoader_ResolveExpectedReturnType(PyFunctionObject *func, int *optional) {
    /* We don't do any typing on co-routines, and if we did, the annotated return
     * type isn't what we want to enforce - we would want to enforce returning a
     * coroutine, or some awaitable shape.  So if we have a co-routine allow
     * any value to be returned */
    if (((PyCodeObject *)func->func_code)->co_flags & CO_COROUTINE) {
        Py_INCREF(&PyBaseObject_Type);
        *optional = 1;
        return &PyBaseObject_Type;
    }

    return _PyClassLoader_ResolveType(
            _PyClassLoader_GetReturnTypeDescr((PyFunctionObject *)func), optional);
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
type_vtable_setslot_typecheck(PyObject *ret_type,
                              int optional,
                              PyObject *name,
                              _PyType_VTable *vtable,
                              Py_ssize_t slot,
                              PyObject *value)
{

    PyObject *state = PyTuple_New(4);
    if (state == NULL) {
        Py_XDECREF(ret_type);
        return -1;
    }
    PyTuple_SET_ITEM(state, 0, value);
    Py_INCREF(value);
    PyTuple_SET_ITEM(state, 1, name);
    Py_INCREF(name);
    PyTuple_SET_ITEM(state, 2, ret_type);
    Py_INCREF(ret_type);
    PyObject *opt_val = optional ? Py_True : Py_False;
    Py_INCREF(opt_val);
    PyTuple_SET_ITEM(state, 3, opt_val);

    Py_XDECREF(vtable->vt_entries[slot].vte_state);
    vtable->vt_entries[slot].vte_state = state;
    if (PyFunction_Check(value)) {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_func_overridable;
    } else {
        vtable->vt_entries[slot].vte_entry =
            (vectorcallfunc)type_vtable_nonfunc;
    }
    return 0;
}

/* Figure out our existing enforced return type for a given func/vtable/index.
 * If we're still the original entry then we get it from the function, but if we've
 * replaced it with a non-vtable helper then we'll grab the type from there. If the
 * function is an untyped builtin method, we just fall back to `object`; if it's any
 * other kind of untyped function, we return NULL and the caller needs to look up the
 * MRO further.
 *
 * Returns a new reference or NULL.
 */
static PyObject *
resolve_slot_local_return_type(PyObject *func,
                               _PyType_VTable *vtable,
                               Py_ssize_t index,
                               int *optional)
{
    PyObject *cur_type = NULL;
    if (func == NULL) {
        /* someone removed the method, and now is adding a method back */
        assert(vtable->vt_entries[index].vte_entry ==
               (vectorcallfunc)type_vtable_func_missing);
        PyObject *state = vtable->vt_entries[index].vte_state;
        cur_type = PyTuple_GET_ITEM(state, 1);
        Py_INCREF(cur_type);
        *optional = PyTuple_GET_ITEM(state, 2) == Py_True;
    } else if (vtable->vt_entries[index].vte_entry ==
                   (vectorcallfunc)type_vtable_func_overridable ||
               vtable->vt_entries[index].vte_entry == (vectorcallfunc)type_vtable_nonfunc) {
        PyObject *state = vtable->vt_entries[index].vte_state;
        assert(PyTuple_Check(state));
        cur_type = PyTuple_GET_ITEM(state, 2);
        Py_INCREF(cur_type);
        *optional = PyTuple_GET_ITEM(state, 3) == Py_True;
    } else if (PyFunction_Check(func) && _PyClassLoader_IsStaticFunction(func)) {
        cur_type = (PyObject *)_PyClassLoader_ResolveExpectedReturnType((PyFunctionObject *)func, optional);
    } else if (Py_TYPE(func) == &PyStaticMethod_Type) {
        PyObject *static_func = _PyStaticMethod_GetFunc(func);
        if (_PyClassLoader_IsStaticFunction(static_func)) {
            cur_type = (PyObject *)_PyClassLoader_ResolveExpectedReturnType(
                (PyFunctionObject*)static_func, optional);
        }
    } else if (Py_TYPE(func) == &PyMethodDescr_Type) {
        // builtin methods for now assumed to return object
        cur_type = (PyObject *)&PyBaseObject_Type;
        Py_INCREF(cur_type);
        *optional = 1;
    }
    return cur_type;
}

/* Figure out what the proper return type is for this slot by finding the
 * base class definition which is statically compiled.  Returns NULL with
 * an error or a new reference
 *
 * Returns a new reference
 */
static PyObject *
resolve_slot_overload_return_type(PyTypeObject *tp,
                                  PyObject *name,
                                  Py_ssize_t index,
                                  int *optional)
{
    PyObject *mro = tp->tp_mro;
    PyObject *found_type = NULL;

    for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(mro); i++) {
        PyTypeObject *cur_type = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        PyObject *value = PyDict_GetItem(cur_type->tp_dict, name);

        if (value != NULL) {
            assert(cur_type->tp_cache != NULL);
            found_type = resolve_slot_local_return_type(
                value, (_PyType_VTable *)cur_type->tp_cache, index, optional);
            if (found_type != NULL) {
                return found_type;
            }
        }
    }

    PyErr_Format(PyExc_RuntimeError,
                "missing type annotation on static compiled method %s.%U",
                 tp->tp_name, name);
    return NULL;
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

            _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(subtype);
            if (vtable == NULL) {
                return -1;
            }
            if (type_init_subclass_vtables((PyTypeObject *) ref)) {
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

typedef struct {
    PyObject_HEAD
    PyObject *fr_func; /* borrowed ref */
} funcref;


static void
funcref_dealloc(funcref *self)
{
    Py_TYPE(self)->tp_free(self);
}

PyTypeObject _PyFuncRef_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0).tp_name = "funcref",
    .tp_basicsize = sizeof(funcref),
    .tp_flags = Py_TPFLAGS_DEFAULT | _Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_Del,
    .tp_dealloc = (destructor)funcref_dealloc,
};

int
_PyClassLoader_UpdateSlot(PyTypeObject *type,
                          PyObject *name,
                          PyObject *previous,
                          PyObject *new_value)
{
    assert(type->tp_cache != NULL);

    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    PyObject *slotmap = vtable->vt_slotmap;
    if (vtable->vt_funcdict != NULL) {
        funcref *fr = (funcref *)PyDict_GetItem(vtable->vt_funcdict, name);
        if (fr != NULL) {
            fr->fr_func = new_value;
        }
    }

    PyObject *slot = PyDict_GetItem(slotmap, name);
    if (slot == NULL) {
        return 0;
    }

    if (previous == NULL || new_value == NULL) {
        /* we're overriding something that we inherited */
        PyObject *mro = type->tp_mro;

        PyObject *base = NULL;
        for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(mro); i++) {
            PyObject *next = PyTuple_GET_ITEM(type->tp_mro, i);
            PyObject *dict = ((PyTypeObject *)next)->tp_dict;
            if (dict == NULL) {
                continue;
            }
            base = PyDict_GetItem(dict, name);
            if (base != NULL) {
                break;
            }
        }
        /* What if base is null? */
        if (base != NULL) {
            if (previous == NULL) {
                previous = base;
            }
            if (new_value == NULL) {
                new_value = base;
            }
        }
    }

    Py_ssize_t index = PyLong_AsSsize_t(slot);
    int cur_optional;
    PyObject* cur_type = resolve_slot_local_return_type(previous, vtable, index, &cur_optional);
    if (cur_type == NULL) {
        // Can't resolve slot type on this type (might have a patched-in non-static function),
        // let's check the MRO.
        cur_type = resolve_slot_overload_return_type(type, name, index, &cur_optional);
    }

    /* we make no attempts to keep things efficient when types start getting
     * mutated.  We always install the less efficient type checked functions,
     * rather than having to deal with a proliferation of states */
    if (new_value == NULL) {
        /* The value is deleted, and we didn't find one in a base class.
         * We'll put in a value which raises AttributeError */
        PyObject *missing_state = PyTuple_New(3);
        if (missing_state == NULL) {
            Py_DECREF(cur_type);
            return -1;
        }

        PyTuple_SET_ITEM(missing_state, 0, name);
        PyTuple_SET_ITEM(missing_state, 1, cur_type);
        PyObject *optional = cur_optional ? Py_True : Py_False;
        PyTuple_SET_ITEM(missing_state, 2, optional);
        Py_INCREF(name);
        Py_INCREF(cur_type);
        Py_INCREF(optional);

        Py_XDECREF(vtable->vt_entries[index].vte_state);
        vtable->vt_entries[index].vte_state = missing_state;
        vtable->vt_entries[index].vte_entry = (vectorcallfunc)type_vtable_func_missing;
    } else if (type_vtable_setslot_typecheck(
                   cur_type, cur_optional, name, vtable, index, new_value)) {
        Py_DECREF(cur_type);
        return -1;
    }
    Py_DECREF(cur_type);

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

        } else if (Py_TYPE(value) == &PyMethodDescr_Type) {
            Py_XSETREF(vtable->vt_entries[slot].vte_state, value);
            vtable->vt_entries[slot].vte_entry =
                ((PyMethodDescrObject *)value)->vectorcall;
            Py_INCREF(value);
            return 0;
        }
    }

    int optional = 0;
    PyObject *ret_type;
    if (_PyClassLoader_IsStaticFunction(value)) {
        ret_type = (PyObject *)_PyClassLoader_ResolveExpectedReturnType(
            (PyFunctionObject *)value, &optional);
    } else {
        ret_type =
            resolve_slot_overload_return_type(tp, name, slot, &optional);
    }
    if (ret_type == NULL) {
        return -1;
    }

    int res = type_vtable_setslot_typecheck(
        ret_type, optional, name, vtable, slot, value);
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
    PyTypeObject *type = Py_TYPE(self);
    _PyType_VTable *vtable = (_PyType_VTable *)type->tp_cache;
    PyObject *mro = type->tp_mro;
    Py_ssize_t slot =
        PyLong_AsSsize_t(PyDict_GetItem(vtable->vt_slotmap, name));

    assert(vtable != NULL);

    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        PyTypeObject *cur_type = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        PyObject *value = PyDict_GetItem(cur_type->tp_dict, name);
        if (value != NULL) {
            if (type_vtable_setslot(type, name, vtable, slot, value)) {
                return NULL;
            }

            return vtable->vt_entries[slot].vte_entry(
                vtable->vt_entries[slot].vte_state, args, nargsf, kwnames);
        }
    }

    PyErr_Format(
        PyExc_TypeError, "%s has no attribute %U", type->tp_name, name);
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
    } else if (PyFunction_Check(value) && _PyClassLoader_IsStaticFunction(value)) {
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
    }
    return 0;
}

_PyType_VTable *
_PyClassLoader_EnsureVtable(PyTypeObject *self)
{
    _PyType_VTable *vtable = (_PyType_VTable *)self->tp_cache;
    PyObject *slotmap = NULL;
    PyObject *key, *mro, *value;
    Py_ssize_t i;

    if (self == &PyBaseObject_Type) {
        PyErr_SetString(PyExc_RuntimeError, "cannot initialize vtable for builtins.object");
        return NULL;
    }
    if (vtable != NULL) {
        return vtable;
    }

    mro = self->tp_mro;
    if (PyTuple_GET_SIZE(mro) != 1) {
        /* TODO: Non-type objects in mro? */
        /* TODO: Multiple inheritance */

        /* Get the size of the next element in our mro, we'll build on it */
        PyObject *next = PyTuple_GET_ITEM(self->tp_mro, 1);
        assert(PyType_Check(next));
        if ((PyTypeObject *)next != &PyBaseObject_Type) {
            _PyType_VTable *base_vtable =
                _PyClassLoader_EnsureVtable((PyTypeObject *)next);

            if (base_vtable == NULL) {
                return NULL;
            }

            PyObject *next_slotmap = base_vtable->vt_slotmap;
            assert(next_slotmap != NULL);

            slotmap = PyDict_Copy(next_slotmap);
        }
    }

    if (slotmap == NULL) {
        slotmap = _PyDict_NewPresized(PyDict_Size(self->tp_dict));
    }

    if (slotmap == NULL) {
        return NULL;
    }

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
            Py_DECREF(slotmap);
            return NULL;
        }
    }
    /* finally allocate the vtable, which will have empty slots initially */
    vtable =
        PyObject_GC_NewVar(_PyType_VTable, &_PyType_VTableType, slot_index);

    if (vtable == NULL) {
        Py_DECREF(slotmap);
        return NULL;
    }
    vtable->vt_size = slot_index;
    vtable->vt_funcdict = NULL;
    vtable->vt_slotmap = slotmap;
    self->tp_cache = (PyObject *)vtable;

    _PyClassLoader_ReinitVtable(vtable);

    PyObject_GC_Track(vtable);

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
classloader_get_member(PyObject *path,
                       Py_ssize_t items,
                       PyObject **container)
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
    for (Py_ssize_t i = 0; i < items; i++) {
        PyObject *d = NULL;
        PyObject *name = PyTuple_GET_ITEM(path, i);

        if (container != NULL) {
            Py_CLEAR(*container);
            Py_INCREF(cur);
            *container = cur;
        }

        if (PyTuple_CheckExact(name)) {
            if (!PyType_Check(cur)) {
                PyErr_Format(PyExc_TypeError,
                             "generic type instantiation without type: %R on "
                             "%U from %s",
                             path,
                             name,
                             cur->ob_type->tp_name);
                goto error;
            }
            PyObject *tmp_tuple = PyTuple_New(PyTuple_GET_SIZE(name));
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(name); i++) {
                int optional;
                PyObject *param = (PyObject *)_PyClassLoader_ResolveType(
                    PyTuple_GET_ITEM(name, i), &optional);
                if (param == NULL) {
                    Py_DECREF(tmp_tuple);
                    goto error;
                }
                if (optional) {
                    PyObject *union_args = PyTuple_New(2);
                    if (union_args == NULL) {
                        Py_DECREF(tmp_tuple);
                        goto error;
                    }
                    /* taking ref from _PyClassLoader_ResolveType */
                    PyTuple_SET_ITEM(union_args, 0, param);
                    PyTuple_SET_ITEM(union_args, 1, Py_None);
                    Py_INCREF(Py_None);

                    PyObject *union_obj = _Py_Union(union_args);
                    if (union_obj == NULL) {
                        Py_DECREF(union_args);
                        Py_DECREF(tmp_tuple);
                        goto error;
                    }
                    Py_DECREF(union_args);
                    param = union_obj;
                }
                PyTuple_SET_ITEM(tmp_tuple, i, param);
            }

            PyObject *next = _PyClassLoader_GetGenericInst(
                cur,
                ((PyTupleObject *)tmp_tuple)->ob_item,
                PyTuple_GET_SIZE(tmp_tuple));
            Py_DECREF(tmp_tuple);
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
        } else if (PyType_CheckExact(cur)) {
            d = ((PyTypeObject *)cur)->tp_dict;
        }

        if (d == NULL) {
            PyObject *next = PyObject_GetAttr(cur, name);
            if (next == NULL) {
                PyErr_Format(
                    PyExc_TypeError,
                    "bad name provided for class loader: %R on %U from %s",
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
        PyObject *next = _PyDict_GetItem_Unicode(d, name);
        if (next == NULL && d == tstate->interp->modules) {
            /* import module in case it's not available in sys.modules */
            if (PyImport_ImportModuleLevelObject(name, NULL, NULL, NULL, 0) == NULL) {
                PyErr_Fetch(&et, &ev, &tb);
            } else {
                next = _PyDict_GetItem_Unicode(d, name);
            }
        } else
        if (next == Py_None && d == tstate->interp->builtins) {
            /* special case builtins.None, it's used to represent NoneType */
            next = (PyObject *)&_PyNone_Type;
            Py_INCREF(next);
        } else {
            Py_XINCREF(next);
        }

        if (next == NULL) {
            PyErr_Format(
                PyExc_TypeError,
                "bad name provided for class loader, '%U' doesn't exist in %R",
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

    PyObject *res = classloader_get_member(descr, items, NULL);
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
        classloader_get_member(path, PyTuple_GET_SIZE(path), (PyObject **)&target_type);
    if (cur == NULL) {
        assert(target_type == NULL);
        return -1;
    } else if (classloader_verify_type((PyObject *)target_type, path)) {
        Py_DECREF(cur);
        Py_XDECREF(target_type);
        return -1;
    }


    /* Now we need to update or make the v-table for this type */
    _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(target_type);
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

PyObject *
_PyClassLoader_ResolveFunction(PyObject *path, PyObject **container)
{
    PyObject *func =
        classloader_get_member(path, PyTuple_GET_SIZE(path), container);

    if (func != NULL && Py_TYPE(func) == &PyStaticMethod_Type) {
        PyObject *res = _PyStaticMethod_GetFunc(func);
        Py_INCREF(res);
        Py_DECREF(func);
        func = res;
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

        _PyType_VTable *vtable = _PyClassLoader_EnsureVtable((PyTypeObject *)container);
        if (vtable == NULL) {
            goto done;
        }
        if (vtable->vt_funcdict == NULL) {
            vtable->vt_funcdict = PyDict_New();
            if (vtable->vt_funcdict == NULL) {
                goto done;
            }
        }
        funcref *fr = (funcref *)PyDict_GetItem(vtable->vt_funcdict, name);
        if (fr != NULL) {
            assert(fr->fr_func == func);
            cache = &fr->fr_func;
            goto done;
        }

        fr = PyObject_New(funcref, &_PyFuncRef_Type);
        if (fr == NULL) {
            goto done;
        }

        fr->fr_func = func; /* borrowed */
        cache = &fr->fr_func;
        if (PyDict_SetItem(vtable->vt_funcdict, name, (PyObject *)fr)) {
            cache = NULL;
            goto done;
        }
        Py_DECREF(fr);
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
        classloader_get_member(path, PyTuple_GET_SIZE(path), (PyObject **)&target_type);

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

    _PyType_VTable *vtable = _PyClassLoader_EnsureVtable(type);
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
        classloader_get_member(path, PyTuple_GET_SIZE(path), NULL);
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
    PyVarObject_HEAD_INIT(&PyType_Type, 0).tp_name = "typed_descriptor",
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
    if (PyTuple_Check(args)) {
        return _PyClassLoader_GetGenericInst((PyObject *)type,
                                             ((PyTupleObject *)args)->ob_item,
                                             PyTuple_GET_SIZE(args));
    } else {
        return _PyClassLoader_GetGenericInst((PyObject *)type, &args, 1);
    }
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
