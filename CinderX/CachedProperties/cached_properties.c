#include "Python.h"
#include "structmember.h"         // PyMemberDef

#include "cached_properties.h"

/* fb t46346203 */

typedef struct {
    PyObject_HEAD
    PyObject *func;             /* function object */
    PyObject *name;             /* str or member descriptor object */
    PyObject *value;            /* value or NULL when uninitialized */
} PyCachedClassPropertyDescrObject;

static int
cached_classproperty_traverse(PyCachedClassPropertyDescrObject *prop, visitproc visit, void *arg) {
    Py_VISIT(prop->func);
    Py_VISIT(prop->value);
    return 0;
}

static PyObject *
cached_classproperty_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *func;
    PyCachedClassPropertyDescrObject *descr;

    if (PyTuple_GET_SIZE(args) != 1) {
        PyErr_SetString(PyExc_TypeError, "cached_classproperty: 1 argument expected");
        return NULL;
    }

    func = PyTuple_GET_ITEM(args, 0);

    descr = (PyCachedClassPropertyDescrObject *)PyType_GenericAlloc(type, 0);
    if (descr != NULL) {
        PyObject *name;
        if (PyFunction_Check(func)) {
            name = ((PyFunctionObject*)func)->func_name;
        } else {
            name = PyObject_GetAttrString(func, "__name__");
            if (name == NULL) {
                Py_DECREF(descr);
                return NULL;
            }
        }
        descr->func = func;
        descr->name = name;
        Py_INCREF(func);
        Py_INCREF(name);
    }
    return (PyObject *)descr;
}

static PyObject *
cached_classproperty_get(PyObject *self, PyObject *obj, PyObject *cls)
{
    PyCachedClassPropertyDescrObject *cp = (PyCachedClassPropertyDescrObject *)self;
    PyObject *res;

    res = cp->value;
    if (res == NULL) {
        res = _PyObject_Vectorcall(cp->func, &cls, 1, NULL);
        if (res == NULL) {
            return NULL;
        }
        if (cp->value == NULL) {
            /* we steal the ref count */
            cp->value = res;
        } else {
            /* first value to return wins */
            Py_DECREF(res);
            res = cp->value;
        }
    }

    Py_INCREF(res);
    return res;
}

static void
cached_classproperty_dealloc(PyCachedClassPropertyDescrObject *cp)
{
    PyObject_GC_UnTrack(cp);
    Py_XDECREF(cp->func);
    Py_XDECREF(cp->name);
    Py_XDECREF(cp->value);
    PyTypeObject *type = Py_TYPE(cp);
    Py_TYPE(cp)->tp_free(cp);
    Py_DECREF(type);
}

static PyObject *
cached_classproperty_get___doc__(PyCachedClassPropertyDescrObject *cp, void *closure)
{
    PyObject *res = ((PyFunctionObject*)cp->func)->func_doc;
    Py_INCREF(res);
    return res;
}

static PyObject *
cached_classproperty_get_name(PyCachedClassPropertyDescrObject *cp, void *closure)
{
    PyObject *res = cp->name;
    Py_INCREF(res);
    return res;
}

static PyGetSetDef cached_classproperty_getsetlist[] = {
    {"__doc__", (getter)cached_classproperty_get___doc__, NULL, NULL, NULL},
    {"name", (getter)cached_classproperty_get_name, NULL, NULL, NULL},
    {"__name__", (getter)cached_classproperty_get_name, NULL, NULL, NULL},
    {NULL} /* Sentinel */
};

static PyMemberDef cached_classproperty_members[] = {
    {"func", T_OBJECT, offsetof(PyCachedClassPropertyDescrObject, func), READONLY},
    {0}
};

static PyType_Slot PyCachedClassProperty_slots[] = {
    {Py_tp_dealloc, cached_classproperty_dealloc},
    {Py_tp_traverse, cached_classproperty_traverse},
    {Py_tp_descr_get, cached_classproperty_get},
    {Py_tp_members, cached_classproperty_members},
    {Py_tp_getset, cached_classproperty_getsetlist},
    {Py_tp_new, cached_classproperty_new},
    {Py_tp_alloc, PyType_GenericAlloc},
    {Py_tp_free, PyObject_GC_Del},
    {0, 0},
};

PyType_Spec _PyCachedClassProperty_TypeSpec = {
    "builtins.cached_classproperty",
    sizeof(PyCachedClassPropertyDescrObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    PyCachedClassProperty_slots
};

/* end fb t46346203 */

/* fb t46346203 */

static int
cached_property_traverse(PyCachedPropertyDescrObject *prop, visitproc visit, void *arg)
{
    Py_VISIT(prop->func);
    Py_VISIT(prop->name_or_descr);
    return 0;
}


PyTypeObject PyCachedProperty_Type;
PyTypeObject PyCachedPropertyWithDescr_Type;

static int
cached_property_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyCachedPropertyDescrObject *cp = (PyCachedPropertyDescrObject *)self;
    PyObject *name_or_descr, *func;

    if (PyTuple_GET_SIZE(args) != 1 && PyTuple_GET_SIZE(args) != 2) {
        PyErr_SetString(PyExc_TypeError, "cached_property: 1 or 2 arguments expected");
        return -1;
    }

    func = PyTuple_GET_ITEM(args, 0);

    if (PyTuple_GET_SIZE(args) == 2) {
        PyMemberDescrObject *descr;
        name_or_descr = PyTuple_GET_ITEM(args, 1);

        if (Py_TYPE(name_or_descr) != &PyMemberDescr_Type) {
            PyErr_SetString(PyExc_TypeError, "cached_property: member descriptor expected for 2nd argument");
            return -1;
        }

        descr = (PyMemberDescrObject *)name_or_descr;
        if(descr->d_member->type != T_OBJECT_EX ||
           descr->d_member->flags) {
           PyErr_SetString(PyExc_TypeError, "cached_property: incompatible descriptor");
           return -1;
        }

        /* change our type to enable setting the cached property, we don't allow
         * subtypes because we can't change their type, and the descriptor would
         * need to account for doing the lookup, and we'd need to dynamically
         * create a subtype of them too, not to mention dealing with extra ref
         * counting on the types */
        if (Py_TYPE(self) != &PyCachedProperty_Type &&
            Py_TYPE(self) != &PyCachedPropertyWithDescr_Type) {
            PyErr_SetString(
                PyExc_TypeError,
                "cached_property: descr cannot be used with subtypes of cached_property");
            return -1;
        }

        Py_TYPE(self) = &PyCachedPropertyWithDescr_Type;
    } else {
        name_or_descr = Py_None;
    }

    cp->func = func;
    cp->name_or_descr = name_or_descr;
    Py_INCREF(func);
    Py_INCREF(name_or_descr);

    return 0;
}

PyDoc_STRVAR(cached_property_doc,
"cached_property(function, [slot]) --> cached_property object\n\
\n\
Creates a new cached property where function will be called to produce\n\
the value on the first access.\n\
\n\
If slot descriptor is provided it will be used for storing the value.");


static PyObject *
cached_property_get(PyObject *self, PyObject *obj, PyObject *cls)
{
    PyObject *res, *dict;
    PyCachedPropertyDescrObject *cp = (PyCachedPropertyDescrObject *)self;
    PyObject *stack[1] = {obj};

    if (obj == NULL) {
        Py_INCREF(self);
        return self;
    }

    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        PyObject **addr;
        PyMemberDescrObject *descr = (PyMemberDescrObject *)cp->name_or_descr;

        if (Py_TYPE(obj) != PyDescr_TYPE(descr) &&
           !PyObject_TypeCheck(obj, PyDescr_TYPE(descr))) {
            PyErr_Format(PyExc_TypeError,
                         "descriptor '%V' for '%s' objects "
                         "doesn't apply to '%s' object",
                         ((PyDescrObject*)descr)->d_name, "?",
                         PyDescr_TYPE(descr)->tp_name,
                         Py_TYPE(obj)->tp_name);
             return NULL;
        }

        addr = (PyObject **)(((const char *)obj) + descr->d_member->offset);
        res = *addr;
        if (res != NULL) {
            Py_INCREF(res);
            return res;
        }

        res = _PyObject_Vectorcall(cp->func, stack, 1, NULL);
        if (res == NULL) {
            return NULL;
        }

        *addr = res;
        Py_INCREF(res);
    } else {
        dict = PyObject_GenericGetDict(obj, NULL);
        if (dict == NULL) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
                PyErr_Format(
                    PyExc_TypeError,
                    "No '__dict__' attribute on '%s' instance to cache '%U' property.",
                    Py_TYPE(obj)->tp_name, cp->name_or_descr
                );
            }
            return NULL;
        }

        res = PyDict_GetItem(dict, cp->name_or_descr);
        Py_DECREF(dict);
        if (res != NULL) {
            Py_INCREF(res); /* we got a borrowed ref */
            return res;
        }

        res = _PyObject_Vectorcall(cp->func, stack, 1, NULL);
        if (res == NULL) {
            return NULL;
        }

        if (_PyObjectDict_SetItem(Py_TYPE(obj), _PyObject_GetDictPtr(obj), cp->name_or_descr, res)) {
            Py_DECREF(res);
            return NULL;
        }
    }

    return res;
}

static int
cached_property_set(PyObject *self, PyObject *obj, PyObject *value)
{
    PyCachedPropertyDescrObject *cp = (PyCachedPropertyDescrObject *)self;
    PyObject **dictptr;

    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        return Py_TYPE(cp->name_or_descr)->tp_descr_set(cp->name_or_descr, obj, value);
    }

    dictptr = _PyObject_GetDictPtr(obj);

    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return -1;
    }

    return _PyObjectDict_SetItem(Py_TYPE(obj), dictptr, cp->name_or_descr, value);
}

static void
cached_property_dealloc(PyCachedPropertyDescrObject *cp)
{
    PyObject_GC_UnTrack(cp);
    Py_XDECREF(cp->func);
    Py_XDECREF(cp->name_or_descr);
    Py_TYPE(cp)->tp_free(cp);
}

static PyObject *
cached_property_get___doc__(PyCachedPropertyDescrObject *cp, void *closure)
{
    PyObject *res = ((PyFunctionObject*)cp->func)->func_doc;
    Py_INCREF(res);
    return res;
}

static PyObject *
cached_property_get_name(PyCachedPropertyDescrObject *cp, void *closure)
{
    PyObject *res;

    if (Py_TYPE(cp->name_or_descr) != &PyMemberDescr_Type) {
        res = cp->name_or_descr;
    } else {
        res = PyDescr_NAME(cp->name_or_descr);
    }
    Py_INCREF(res);
    return res;
}

static PyObject *
cached_property_get_slot(PyCachedPropertyDescrObject *cp, void *closure)
{
    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        PyObject *res = cp->name_or_descr;

        Py_INCREF(res);
        return res;
    }
    Py_RETURN_NONE;
}

static PyObject *
cached_property_clear(PyCachedPropertyDescrObject *self, PyObject *obj)
{
    PyCachedPropertyDescrObject *cp = (PyCachedPropertyDescrObject *)self;
    PyObject **dictptr;

    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        if (Py_TYPE(cp->name_or_descr)->tp_descr_set(cp->name_or_descr, obj, NULL) < 0) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
                Py_RETURN_NONE;
            }
            return NULL;
        }
        Py_RETURN_NONE;
    }

    dictptr = _PyObject_GetDictPtr(obj);

    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return NULL;
    }

    if (_PyObjectDict_SetItem(Py_TYPE(obj), dictptr, cp->name_or_descr, NULL) < 0) {
        if (PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear();
            Py_RETURN_NONE;
        }
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
cached_property_has_value(PyCachedPropertyDescrObject *self, PyObject *obj)
{
    PyCachedPropertyDescrObject *cp = (PyCachedPropertyDescrObject *)self;
    PyObject **dictptr;

    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        PyObject *value = Py_TYPE(cp->name_or_descr)->tp_descr_get(
            cp->name_or_descr, obj, (PyObject *)Py_TYPE(obj));

        if (value == NULL) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
                Py_RETURN_FALSE;
            }
            return NULL;
        }
        Py_DECREF(value);
        Py_RETURN_TRUE;
    }

    dictptr = _PyObject_GetDictPtr(obj);

    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return NULL;
    }

    if (*dictptr == NULL) {
        Py_RETURN_FALSE;
    }

    PyObject *value = PyDict_GetItem(*dictptr, cp->name_or_descr);
    if (value == NULL) {
        Py_RETURN_FALSE;
    }
    Py_RETURN_TRUE;
}

static PyObject *
cached_property___set_name__(PyObject* self, PyObject* const* args, Py_ssize_t nargs)
{
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "cached_property.__set_name__: 2 arguments expected");
        return NULL;
    }
    PyCachedPropertyDescrObject *cp = (PyCachedPropertyDescrObject *)self;
    PyObject *Py_UNUSED(owner) = args[0];
    PyObject *name = args[1];

    // Perform error checks if the name wasn't initialized (i.e, not None)
    if (cp->name_or_descr != Py_None) {
        if (PyUnicode_CheckExact(cp->name_or_descr)) {
            // Check for naming conflicts
            if (PyUnicode_Compare(cp->name_or_descr, name) != 0) {
                if (!PyErr_Occurred()) {
                    // Avoid masking existing errors
                    PyErr_Format(
                        PyExc_TypeError,
                        "Cannot assign the same cached_property to two different names (%R and %R).",
                        cp->name_or_descr, name
                    );
                }
                return NULL;
            }
        } else {
            // This cannot normally happen in managed code, unless someone manually calls `__set_name__`
            // after a slot-backed property was defined (see test_cached_property_set_name_on_slot_backed_property)
            PyErr_Format(
                PyExc_RuntimeError,
                "Cannot set name (%R) for a cached property backed by a slot (%R)",
                name, cp
            );
            return NULL;
        }
    }

    Py_SETREF(cp->name_or_descr, name);
    Py_INCREF(name);
    Py_RETURN_NONE;
}


static PyGetSetDef cached_property_getsetlist[] = {
    {"__doc__", (getter)cached_property_get___doc__, NULL, NULL, NULL},
    {"__name__", (getter)cached_property_get_name, NULL, NULL, NULL},
    {"name", (getter)cached_property_get_name, NULL, NULL, NULL},
    {"slot", (getter)cached_property_get_slot, NULL, NULL, NULL},
    {NULL} /* Sentinel */
};

static PyMemberDef cached_property_members[] = {
    {"func", T_OBJECT, offsetof(PyCachedPropertyDescrObject, func), READONLY},
    /* currently duplicated until all consumers are updated in favor of fget */
    {"fget", T_OBJECT, offsetof(PyCachedPropertyDescrObject, func), READONLY},
    {0}
};

static PyMethodDef cached_property_methods[] = {
    {"clear", (PyCFunction)cached_property_clear, METH_O, NULL},
    {"has_value", (PyCFunction)cached_property_has_value, METH_O, NULL},
    {"__set_name__", (PyCFunction)cached_property___set_name__, METH_FASTCALL, NULL},
    {NULL, NULL}
};

PyTypeObject PyCachedProperty_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "cached_property",
    .tp_basicsize = sizeof(PyCachedPropertyDescrObject),
    .tp_dealloc =  (destructor)cached_property_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    .tp_doc = cached_property_doc,
    .tp_traverse = (traverseproc)cached_property_traverse,
    .tp_descr_get = cached_property_get,
    .tp_members = cached_property_members,
    .tp_getset = cached_property_getsetlist,
    .tp_new = PyType_GenericNew,
    .tp_init = cached_property_init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
    .tp_methods = cached_property_methods,
};

PyTypeObject PyCachedPropertyWithDescr_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "cached_property_with_descr",
    .tp_base = &PyCachedProperty_Type,
    .tp_basicsize = sizeof(PyCachedPropertyDescrObject),
    .tp_dealloc =  (destructor)cached_property_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    .tp_doc = cached_property_doc,
    .tp_traverse = (traverseproc)cached_property_traverse,
    .tp_descr_get = cached_property_get,
    .tp_descr_set = cached_property_set,
    .tp_members = cached_property_members,
    .tp_getset = cached_property_getsetlist,
    .tp_new = PyType_GenericNew,
    .tp_init = cached_property_init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};

/* end fb t46346203 */

/* fb T82701047 */
/*********************** AsyncCachedProperty*********************/
static PyObject *_AsyncLazyValue_Type;

static int
async_cached_property_traverse(PyAsyncCachedPropertyDescrObject *prop, visitproc visit, void *arg)
{
    Py_VISIT(prop->func);
    Py_VISIT(prop->name_or_descr);
    return 0;
}

static int
async_cached_property_init_impl(PyAsyncCachedPropertyDescrObject *self,
                                PyObject *func, PyObject *name_or_descr)
/*[clinic end generated code: output=d8f17f423e7ad7f2 input=5bfa6f9a771d138d]*/
{
    if (name_or_descr != NULL) {
        PyMemberDescrObject *descr = (PyMemberDescrObject *)name_or_descr;
        if(descr->d_member->type != T_OBJECT_EX ||
           descr->d_member->flags) {
           PyErr_SetString(PyExc_TypeError, "async_cached_property: incompatible descriptor");
           return -1;
        }

        /* change our type to enable setting the cached property, we don't allow
         * subtypes because we can't change their type, and the descriptor would
         * need to account for doing the lookup, and we'd need to dynamically
         * create a subtype of them too, not to mention dealing with extra ref
         * counting on the types */
        if (Py_TYPE(self) != &PyAsyncCachedProperty_Type &&
            Py_TYPE(self) != &PyAsyncCachedPropertyWithDescr_Type) {
            PyErr_SetString(
                PyExc_TypeError,
                "async_cached_property: descr cannot be used with subtypes of async_cached_property");
            return -1;
        }

        Py_TYPE(self) = &PyAsyncCachedPropertyWithDescr_Type;
        self->name_or_descr = name_or_descr;
    } else if (PyFunction_Check(func)) {
        self->name_or_descr = ((PyFunctionObject *)func)->func_name;
    } else {
        self->name_or_descr = PyObject_GetAttrString(func, "__name__");
        if (self->name_or_descr == NULL) {
            return -1;
        }
    }

    self->func = func;
    Py_INCREF(self->func);
    Py_INCREF(self->name_or_descr);

    return 0;
}

static inline int import_async_lazy_value() {
    if (_AsyncLazyValue_Type == NULL) {
        _Py_IDENTIFIER(AsyncLazyValue);
        PyObject *asyncio = PyImport_ImportModule("_asyncio");
        if (asyncio == NULL) {
            return -1;
        }
        _AsyncLazyValue_Type = _PyObject_GetAttrId(asyncio, &PyId_AsyncLazyValue);
        Py_DECREF(asyncio);
        if (_AsyncLazyValue_Type == NULL) {
            return -1;
        }
    }
    return 0;
}

static PyObject *
async_cached_property_get(PyObject *self, PyObject *obj, PyObject *cls)
{
    PyObject *res;
    PyAsyncCachedPropertyDescrObject *cp = (PyAsyncCachedPropertyDescrObject *)self;

    if (obj == NULL) {
        Py_INCREF(self);
        return self;
    }

    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        PyObject **addr;
        PyMemberDescrObject *descr = (PyMemberDescrObject *)cp->name_or_descr;

        if (Py_TYPE(obj) != PyDescr_TYPE(descr) &&
           !PyObject_TypeCheck(obj, PyDescr_TYPE(descr))) {
            PyErr_Format(PyExc_TypeError,
                         "descriptor '%V' for '%s' objects "
                         "doesn't apply to '%s' object",
                         ((PyDescrObject*)descr)->d_name, "?",
                         PyDescr_TYPE(descr)->tp_name,
                         Py_TYPE(obj)->tp_name);
             return NULL;
        }

        addr = (PyObject **)(((const char *)obj) + descr->d_member->offset);
        res = *addr;
        if (res != NULL) {
            Py_INCREF(res);
            return res;
        }
        if (import_async_lazy_value() < 0) {
            return NULL;
        }
        res = PyObject_CallFunctionObjArgs(_AsyncLazyValue_Type, cp->func, obj, NULL);
        if (res == NULL) {
            return NULL;
        }

        *addr = res;
        Py_INCREF(res);
    } else {
        if (import_async_lazy_value() < 0) {
            return NULL;
        }

        res = PyObject_CallFunctionObjArgs(_AsyncLazyValue_Type, cp->func, obj, NULL);
        if (res == NULL) {
            return NULL;
        }

        if (PyObject_SetAttr(obj, cp->name_or_descr, res) < 0) {
            Py_DECREF(res);
            return NULL;
        }
    }

    return res;
}

static void
async_cached_property_dealloc(PyAsyncCachedPropertyDescrObject *cp)
{
    PyObject_GC_UnTrack(cp);
    Py_XDECREF(cp->func);
    Py_XDECREF(cp->name_or_descr);
    Py_TYPE(cp)->tp_free(cp);
}

static PyObject *
async_cached_property_get___doc__(PyAsyncCachedPropertyDescrObject *cp, void *closure)
{
    PyObject *res = ((PyFunctionObject*)cp->func)->func_doc;
    Py_INCREF(res);
    return res;
}

static PyObject *
async_cached_property_get_name(PyAsyncCachedPropertyDescrObject *cp, void *closure)
{
    PyObject *res;

    if (Py_TYPE(cp->name_or_descr) != &PyMemberDescr_Type) {
        res = cp->name_or_descr;
    } else {
        res = PyDescr_NAME(cp->name_or_descr);
    }
    Py_INCREF(res);
    return res;
}

static PyObject *
async_cached_property_get_slot(PyAsyncCachedPropertyDescrObject *cp, void *closure)
{
    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        PyObject *res = cp->name_or_descr;

        Py_INCREF(res);
        return res;
    }
    Py_RETURN_NONE;
}

static int
async_cached_property_set(PyObject *self, PyObject *obj, PyObject *value)
{
    PyAsyncCachedPropertyDescrObject *cp = (PyAsyncCachedPropertyDescrObject *)self;
    PyObject **dictptr;

    if (Py_TYPE(cp->name_or_descr) == &PyMemberDescr_Type) {
        return Py_TYPE(cp->name_or_descr)->tp_descr_set(cp->name_or_descr, obj, value);
    }

    dictptr = _PyObject_GetDictPtr(obj);

    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return -1;
    }

    return _PyObjectDict_SetItem(Py_TYPE(obj), dictptr, cp->name_or_descr, value);
}


static PyGetSetDef async_cached_property_getsetlist[] = {
    {"__doc__", (getter)async_cached_property_get___doc__, NULL, NULL, NULL},
    {"name", (getter)async_cached_property_get_name, NULL, NULL, NULL},
    {"slot", (getter)async_cached_property_get_slot, NULL, NULL, NULL},
    {NULL} /* Sentinel */
};

static PyMemberDef async_cached_property_members[] = {
    {"func", T_OBJECT, offsetof(PyAsyncCachedPropertyDescrObject, func), READONLY},
    {"fget", T_OBJECT, offsetof(PyAsyncCachedPropertyDescrObject, func), READONLY},
    {0}
};

// This was originally clinic-generated from Objects/descrobject.c
//
// Copied over wholesale for speed of implementation to unblock CinderX, but a
// handwritten implementation would be cleaner.
/* start clinic-generated code */
PyDoc_STRVAR(async_cached_property_init__doc__,
"async_cached_property(func, name_or_descr=None)\n"
"--\n"
"\n"
"init a async_cached_property.\n"
"\n"
"Creates a new async cached property where function will be called to produce\n"
"the async lazy value on the first access.\n"
"\n"
"If slot descriptor is provided it will be used for storing the value.\"");

static int
async_cached_property_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"func", "name_or_descr", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "async_cached_property", 0};
    PyObject *argsbuf[2];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 1;
    PyObject *func;
    PyObject *name_or_descr = NULL;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 2, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    func = fastargs[0];
    if (!noptargs) {
        goto skip_optional_pos;
    }
    if (!PyObject_TypeCheck(fastargs[1], &PyMemberDescr_Type)) {
        _PyArg_BadArgument("async_cached_property", "argument 'name_or_descr'", (&PyMemberDescr_Type)->tp_name, fastargs[1]);
        goto exit;
    }
    name_or_descr = fastargs[1];
skip_optional_pos:
    return_value = async_cached_property_init_impl((PyAsyncCachedPropertyDescrObject *)self, func, name_or_descr);

exit:
    return return_value;
}

PyDoc_STRVAR(async_cached_classproperty_new__doc__,
"async_cached_classproperty(func)\n"
"--\n"
"\n"
"Provides an async cached class property.\n"
"\n"
"Works with normal types and frozen types to create values on demand\n"
"and cache them in the class.");

static PyObject *
async_cached_classproperty_new_impl(PyTypeObject *type, PyObject *func);

static PyObject *
async_cached_classproperty_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"func", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "async_cached_classproperty", 0};
    PyObject *argsbuf[1];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    PyObject *func;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 1, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    if (!PyObject_TypeCheck(fastargs[0], &PyFunction_Type)) {
        _PyArg_BadArgument("async_cached_classproperty", "argument 'func'", (&PyFunction_Type)->tp_name, fastargs[0]);
        goto exit;
    }
    func = fastargs[0];
    return_value = async_cached_classproperty_new_impl(type, func);

exit:
    return return_value;
}
/* end clinic-generated code */

PyTypeObject PyAsyncCachedProperty_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "async_cached_property",
    .tp_basicsize = sizeof(PyAsyncCachedPropertyDescrObject),
    .tp_dealloc =  (destructor)async_cached_property_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    .tp_doc = async_cached_property_init__doc__,
    .tp_traverse = (traverseproc)async_cached_property_traverse,
    .tp_descr_get = async_cached_property_get,
    .tp_members = async_cached_property_members,
    .tp_getset = async_cached_property_getsetlist,
    .tp_new = PyType_GenericNew,
    .tp_init = async_cached_property_init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};

PyTypeObject PyAsyncCachedPropertyWithDescr_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "async_cached_property_with_descr",
    .tp_basicsize = sizeof(PyAsyncCachedPropertyDescrObject),
    .tp_dealloc =  (destructor)async_cached_property_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    .tp_doc = async_cached_property_init__doc__,
    .tp_traverse = (traverseproc)async_cached_property_traverse,
    .tp_descr_get = async_cached_property_get,
    .tp_descr_set = async_cached_property_set,
    .tp_members = async_cached_property_members,
    .tp_getset = async_cached_property_getsetlist,
    .tp_new = PyType_GenericNew,
    .tp_init = async_cached_property_init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};


/* end fb T82701047 */
/* fb T82701047 */
/*********************** AsyncCachedClassProperty*********************/
static int
async_cached_classproperty_traverse(PyAsyncCachedClassPropertyDescrObject *prop, visitproc visit, void *arg) {
    Py_VISIT(prop->func);
    Py_VISIT(prop->value);
    return 0;
}

static PyObject *
async_cached_classproperty_new_impl(PyTypeObject *type, PyObject *func)
/*[clinic end generated code: output=b7972e5345764116 input=056050fde0415935]*/
{
    PyAsyncCachedClassPropertyDescrObject *descr;
    descr = (PyAsyncCachedClassPropertyDescrObject *)PyType_GenericAlloc(type, 0);
    if (descr != NULL) {
        PyObject *name = ((PyFunctionObject*)func)->func_name;
        descr->func = func;
        descr->name = name;
        Py_INCREF(func);
        Py_INCREF(name);
    }
    return (PyObject *)descr;
}

static PyObject *
async_cached_classproperty_get(PyObject *self, PyObject *obj, PyObject *cls)
{
    PyAsyncCachedClassPropertyDescrObject *cp = (PyAsyncCachedClassPropertyDescrObject *)self;
    PyObject *res;

    res = cp->value;
    if (res == NULL) {
        if (import_async_lazy_value() < 0) {
            return NULL;
        }
        res = PyObject_CallFunctionObjArgs(_AsyncLazyValue_Type, cp->func, cls, NULL);
        if (res == NULL) {
            return NULL;
        }
        if (cp->value == NULL) {
            /* we steal the ref count */
            cp->value = res;
        } else {
            /* first value to return wins */
            Py_DECREF(res);
            res = cp->value;
        }
    }

    Py_INCREF(res);
    return res;
}

static void
async_cached_classproperty_dealloc(PyAsyncCachedClassPropertyDescrObject *cp)
{
    PyObject_GC_UnTrack(cp);
    Py_XDECREF(cp->func);
    Py_XDECREF(cp->name);
    Py_XDECREF(cp->value);
    Py_TYPE(cp)->tp_free(cp);
}

static PyObject *
async_cached_classproperty_get___doc__(PyAsyncCachedClassPropertyDescrObject *cp, void *closure)
{
    PyObject *res = ((PyFunctionObject*)cp->func)->func_doc;
    Py_INCREF(res);
    return res;
}

static PyObject *
async_cached_classproperty_get_name(PyAsyncCachedClassPropertyDescrObject *cp, void *closure)
{
    PyObject *res = cp->name;
    Py_INCREF(res);
    return res;
}

static PyGetSetDef async_cached_classproperty_getsetlist[] = {
    {"__doc__", (getter)async_cached_classproperty_get___doc__, NULL, NULL, NULL},
    {"name", (getter)async_cached_classproperty_get_name, NULL, NULL, NULL},
    {NULL} /* Sentinel */
};

static PyMemberDef async_cached_classproperty_members[] = {
    {"func", T_OBJECT, offsetof(PyAsyncCachedClassPropertyDescrObject, func), READONLY},
    {0}
};

PyTypeObject PyAsyncCachedClassProperty_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "async_cached_classproperty",
    .tp_basicsize = sizeof(PyAsyncCachedClassPropertyDescrObject),
    .tp_dealloc =  (destructor)async_cached_classproperty_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_doc = async_cached_classproperty_new__doc__,
    .tp_traverse = (traverseproc)async_cached_classproperty_traverse,
    .tp_descr_get = async_cached_classproperty_get,
    .tp_members = async_cached_classproperty_members,
    .tp_getset = async_cached_classproperty_getsetlist,
    .tp_new = async_cached_classproperty_new,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};

/* end fb T82701047 */
