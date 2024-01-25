
/* Module object implementation */

#include "Python.h"
#include "pycore_interp.h"        // PyInterpreterState.importlib
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_moduleobject.h"  // _PyModule_GetDef()
#include "structmember.h"         // PyMemberDef
#include "cinderx/StaticPython/strictmoduleobject.h"
#include "cinderx/StaticPython/classloader.h"

static inline PyObject* Ci_StrictModuleGetDictSetter(PyObject *mod) {
    assert(Ci_StrictModule_Check(mod));
    return ((Ci_StrictModuleObject*) mod) -> global_setter;
}

// copied unchanged from Object/moduleobject.c
static PyObject *
module_repr(PyModuleObject *m)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();

    return PyObject_CallMethod(interp->importlib, "_module_repr", "O", m);
}

// copied unchanged from Object/moduleobject.c
static void
module_dealloc(PyModuleObject *m)
{
    int verbose = _Py_GetConfig()->verbose;

    PyObject_GC_UnTrack(m);
    if (verbose && m->md_name) {
        PySys_FormatStderr("# destroy %U\n", m->md_name);
    }
    if (m->md_weaklist != NULL)
        PyObject_ClearWeakRefs((PyObject *) m);
    /* bpo-39824: Don't call m_free() if m_size > 0 and md_state=NULL */
    if (m->md_def && m->md_def->m_free
        && (m->md_def->m_size <= 0 || m->md_state != NULL))
    {
        m->md_def->m_free(m);
    }
    Py_XDECREF(m->md_dict);
    Py_XDECREF(m->md_name);
    if (m->md_state != NULL)
        PyMem_Free(m->md_state);
    Py_TYPE(m)->tp_free((PyObject *)m);
}

static int
strictmodule_init(Ci_StrictModuleObject *self, PyObject *args, PyObject *kwds)
{
    PyObject* d;
    PyObject* enable_patching;
    static char *kwlist[] = {"d", "enable_patching", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO", kwlist, &d, &enable_patching)){
        return -1;
    }

    if (d == NULL || !PyDict_CheckExact(d)) {
        return -1;
    }
    if (enable_patching == NULL) {
        return -1;
    }

    return 0;
}

PyObject *
Ci_StrictModule_New(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Ci_StrictModuleObject *self;
    PyObject* d = NULL;
    PyObject* enable_patching = NULL;
    static char *kwlist[] = {"d", "enable_patching", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist, &d, &enable_patching)){
        return NULL;
    }

    if (d != NULL && !PyDict_CheckExact(d)) {
        PyErr_SetString(PyExc_TypeError, "StrictModule.__new__ expected dict for 1st argument");
        return NULL;
    }
    if (enable_patching != NULL &&
       (enable_patching != Py_True && enable_patching != Py_False)) {
        PyErr_SetString(PyExc_TypeError, "StrictModule.__new__ expected bool for 2nd argument");
        return NULL;
    }

    self = (Ci_StrictModuleObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->imported_from = PyDict_New();
    if (d != NULL) {
        PyObject *imported_from = PyDict_GetItemString(d, "<imported-from>");
        if (imported_from != NULL) {
            if (PyDict_MergeFromSeq2(self->imported_from, imported_from, 1)) {
                return NULL;
            }
            PyDict_DelItemString(d, "<imported-from>");
        }
    }

    self->globals = d;
    Py_XINCREF(d);
    if (enable_patching == Py_True) {
        self->global_setter = d;
        Py_XINCREF(d);
    }
    self->originals = NULL;
    self->static_thunks = NULL;
    return (PyObject *)self;
}

static void
strictmodule_dealloc(Ci_StrictModuleObject *m)
{
    Py_XDECREF(m->globals);
    Py_XDECREF(m->global_setter);
    Py_XDECREF(m->originals);
    Py_XDECREF(m->static_thunks);
    Py_XDECREF(m->imported_from);
    module_dealloc((PyModuleObject *)m);
}

static int
strictmodule_traverse(Ci_StrictModuleObject *m, visitproc visit, void *arg)
{
    Py_VISIT(m->globals);
    Py_VISIT(m->global_setter);
    Py_VISIT(m->originals);
    Py_VISIT(m->static_thunks);
    Py_VISIT(m->imported_from);
    return 0;
}

static int
strictmodule_clear(Ci_StrictModuleObject *m)
{
    Py_CLEAR(m->globals);
    Py_CLEAR(m->global_setter);
    Py_CLEAR(m->originals);
    Py_CLEAR(m->static_thunks);
    Py_CLEAR(m->imported_from);
    return 0;
}

PyObject * Ci_StrictModule_GetDictSetter(PyObject * obj) {
    if (!Ci_StrictModule_Check(obj)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return Ci_StrictModuleGetDictSetter(obj);
}

PyObject * Ci_StrictModule_GetDict(PyObject * obj) {
    if (!Ci_StrictModule_Check(obj)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return Ci_StrictModuleGetDict(obj);
}

int Ci_strictmodule_is_unassigned(PyObject *dict, PyObject *name) {
    if (!PyUnicode_Check(name)) {
        // somehow name is not unicode
        return 0;
    }
    else {
        PyObject *assigned_name = PyUnicode_FromFormat("<assigned:%U>", name);
        if (assigned_name == NULL) {
            return -1;
        }
        PyObject *assigned_status = PyDict_GetItemWithError(dict, assigned_name);
        Py_DECREF(assigned_name);
        if (assigned_status == Py_False) {
            // name has a corresponding <assigned:name> that's False
            return 1;
        }
        return 0;
    }
}

static PyObject * strict_module_dict_get(PyObject *self, void *closure)
{
    Ci_StrictModuleObject *m = (Ci_StrictModuleObject *)self;
    if (m->globals == NULL) {
        // module is uninitialized, return None
        Py_RETURN_NONE;
    }
    assert(PyDict_Check(m->globals));

    PyObject *dict = PyDict_New();
    if (dict == NULL) {
        goto error;
    }
    Py_ssize_t i = 0;
    PyObject *key, *value;

    while (PyDict_NextKeepLazy(m->globals, &i, &key, &value)){
        if (key == NULL || value == NULL) {
            goto error;
        }
        if (PyUnicode_Check(key)) {
            PyObject * angle = PyUnicode_FromString("<");
            if (angle == NULL) {
                goto error;
            }
            Py_ssize_t angle_pos = PyUnicode_Find(key, angle, 0, PyUnicode_GET_LENGTH(key), 1);
            Py_DECREF(angle);
            if (angle_pos == -2) {
                goto error;
            }
            if (angle_pos != 0) {
                // name does not start with <, report in __dict__
                int unassigned = Ci_strictmodule_is_unassigned(m->globals, key);
                if (unassigned < 0) {
                    goto error;
                } else if (!unassigned) {
                    const char* key_string = PyUnicode_AsUTF8(key);
                    if (key_string == NULL || PyDict_SetItemString(dict, key_string, value) < 0) {
                        goto error;
                    }
                }
            }

        } else {
            if (PyDict_SetItem(dict, key, value) < 0) {
                goto error;
            }
        }
    }

    return dict;
error:
    Py_XDECREF(dict);
    return NULL;
}

static PyObject * StrictModule_GetNameObject(Ci_StrictModuleObject *self)
{
    Ci_StrictModuleObject *m = (Ci_StrictModuleObject *)self;
    _Py_IDENTIFIER(__name__);
    PyObject * name;
    PyObject *d = m->globals;
    if (d == NULL || !PyDict_Check(d) ||
        (name = _PyDict_GetItemIdWithError(d, &PyId___name__)) == NULL ||
        !PyUnicode_Check(name))
    {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "nameless module");
        }
        return NULL;
    }

    Py_INCREF(name);
    return name;
}

static PyObject * strict_module_name_get(PyObject *self, void *closure)
{
    PyObject *name = StrictModule_GetNameObject((Ci_StrictModuleObject*) self);
    if (name == NULL) {
        PyErr_Clear();
        PyErr_SetString(PyExc_AttributeError, "strict module has no attribute __name__");
        return NULL;
    }
    // already incref
    return name;
}

static PyObject * strict_module_patch_enabled(PyObject *self, void *closure)
{
    if (((Ci_StrictModuleObject *) self) -> global_setter != NULL) {
        Py_INCREF(Py_True);
        return Py_True;
    }
    Py_INCREF(Py_False);
    return Py_False;
}

static PyObject *
strictmodule_dir(PyObject *self, PyObject *args)
{
    _Py_IDENTIFIER(__dict__);
    PyObject *result = NULL;
    PyObject *dict = _PyObject_GetAttrId(self, &PyId___dict__);

    if (dict != NULL) {
        if (PyDict_Check(dict)) {
            PyObject *dirfunc = PyDict_GetItemString(dict, "__dir__");
            if (dirfunc) {
                result = _PyObject_CallNoArg(dirfunc);
            }
            else {
                result = PyDict_Keys(dict);
            }
        }
        else {
            PyObject *name = StrictModule_GetNameObject((Ci_StrictModuleObject *)self);
            if (name) {
                PyErr_Format(PyExc_TypeError, "%U.__dict__ is not a dictionary", name);
                Py_DECREF(name);
            }
        }
    }
    Py_XDECREF(dict);
    return result;
}

static PyObject *
strictmodule_get_original(PyObject *modules, Ci_StrictModuleObject *self, PyObject *name)
{
    PyObject* original = NULL;
    // originals dict must always contain the real original, so if we find
    // it there we're done
    if (self->originals != NULL) {
        original = PyDict_GetItem(self->originals, name);
        if (original != NULL) {
            return original;
        }
    } else {
        self->originals = PyDict_New();
    }
    original = PyDict_GetItem(self->globals, name);
    if (original == NULL) {
        // patching a name onto the module that previously didn't exist
        return original;
    }
    PyObject* source = PyDict_GetItem(self->imported_from, name);
    if (source == NULL) {
        goto done;
    }
    assert(PyTuple_Check(source));
    assert(PyTuple_Size(source) == 2);
    PyObject* next = PyDict_GetItem(modules, PyTuple_GetItem(source, 0));
    if (next == NULL || !Ci_StrictModule_Check(next)) {
        goto done;
    }
    original = strictmodule_get_original(modules, (Ci_StrictModuleObject*)next, PyTuple_GetItem(source, 1));
    // although strictmodule_get_original in general can return NULL, if we have
    // imported-from metadata for a name this should never happen; there should
    // always be an original value for that import.

  done:
    assert(original != NULL);
    PyDict_SetItem(self->originals, name, original);
    return original;
}

PyObject *
Ci_StrictModule_GetOriginal(PyObject *obj, PyObject *name) {
    // Track down and return the original unpatched value for the given name in
    // module self, and record it in self->originals. It could have been patched
    // in the module we imported it from before we imported it, so we have to do
    // this recursively following the imported-from metadata. We record the
    // original value at every module along the imported-from chain, to avoid
    // repeating lookups later. Return NULL if no original value exists.
    assert (Ci_StrictModule_Check(obj));
    Ci_StrictModuleObject* self = (Ci_StrictModuleObject *) obj;
    return strictmodule_get_original(PyThreadState_GET()->interp->modules, self, name);
}

int Ci_do_strictmodule_patch(PyObject *self, PyObject *name, PyObject *value) {
    Ci_StrictModuleObject *mod = (Ci_StrictModuleObject *) self;
    PyObject * global_setter = mod->global_setter;
    if (global_setter == NULL){
        PyObject* repr = module_repr((PyModuleObject *) mod);
        if (repr == NULL) {
            return -1;
        }
        PyErr_Format(PyExc_AttributeError,
                        "cannot modify attribute '%U' of strict module %U", name, repr);
        Py_DECREF(repr);
        return -1;
    }

    Ci_StrictModule_GetOriginal((PyObject *)mod, name);
    if (_PyClassLoader_UpdateModuleName(mod, name, value) < 0) {
        return -1;
    }
    if (_PyObject_GenericSetAttrWithDict(self, name, value, global_setter) < 0) {
        return -1;
    }
    return 0;
}

static PyObject * strictmodule_patch(PyObject *self, PyObject *args)
{
    PyObject* name;
    PyObject* value;
    if (!PyArg_ParseTuple(args, "UO", &name, &value)) {
        return NULL;
    }
    if (Ci_do_strictmodule_patch(self, name, value) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject * strictmodule_patch_delete(PyObject *self, PyObject *args)
{
    PyObject* name;
    if (!PyArg_ParseTuple(args, "U", &name)) {
        return NULL;
    }
    if (Ci_do_strictmodule_patch(self, name, NULL) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
strictmodule_lookupattro_impl(Ci_StrictModuleObject *m, PyObject *name, int suppress)
{
    PyObject *attr;
    if (Py_TYPE(m) != &Ci_StrictModule_Type || !PyUnicode_Check(name)) {
        attr = NULL;
    } else if (PyUnicode_GET_LENGTH(name) == 9 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__class__")) {
        Py_INCREF(&Ci_StrictModule_Type);
        return (PyObject *)&Ci_StrictModule_Type;
    } else if (PyUnicode_GET_LENGTH(name) == 8 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__dict__")) {
        return strict_module_dict_get((PyObject *) m, NULL);
    } else if (PyUnicode_GET_LENGTH(name) == 8 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__name__")) {
        /* This is a data descriptor, it always takes precedence over
         * an entry in __dict__ */
        return strict_module_name_get((PyObject *) m, NULL);
    } else if (PyUnicode_GET_LENGTH(name) == 17 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__patch_enabled__")) {
        return strict_module_patch_enabled((PyObject *)m, NULL);
    } else {
        /* Otherwise we have no other data descriptors, just look in the
         * dictionary  and elide the _PyType_Lookup */
        if (m->globals) {
            int name_unassigned = Ci_strictmodule_is_unassigned(m->globals, name);
            if (name_unassigned < 0) {
                return NULL;
            } else if (!name_unassigned) {
                attr = PyDict_GetItemWithError(m->globals, name);
                if (attr != NULL) {
                    Py_INCREF(attr);
                    return attr;
                } else if (PyErr_Occurred()) {
                    if (suppress && (PyErr_ExceptionMatches(PyExc_AttributeError) ||
                                     PyErr_ExceptionMatches(PyExc_ImportCycleError))) {
                        PyErr_Clear();
                    }
                    return NULL;
                }
            }
        }

        /* see if we're accessing a descriptor defined on the module type */
        attr = _PyType_Lookup(&Ci_StrictModule_Type, name);
        if (attr != NULL) {
            assert(!PyDescr_IsData(
                attr)); /* it better not be a data descriptor */

            descrgetfunc f = attr->ob_type->tp_descr_get;
            if (f != NULL) {
                attr = f(attr, (PyObject *)m, (PyObject *)&Ci_StrictModule_Type);
                if (attr == NULL &&
                    PyErr_ExceptionMatches(PyExc_AttributeError)) {
                    PyErr_Clear();
                }
            } else {
                Py_INCREF(attr); /* got a borrowed ref */
            }
        }
    }

    if (attr) {
        return attr;
    }
    if (PyErr_Occurred()) {
        if (suppress && PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
        }
        return NULL;
    }
    if (m->globals) {
        _Py_IDENTIFIER(__getattr__);
        PyObject *getattr = _PyDict_GetItemIdWithError(m->globals, &PyId___getattr__);
        if (getattr) {
            PyObject* stack[1] = {name};
            PyObject *res = _PyObject_FastCall(getattr, stack, 1);
            if (res == NULL && suppress &&
                PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
            }
            return res;
        }
        if (PyErr_Occurred()) {
            return NULL;
        }

        _Py_IDENTIFIER(__name__);
        PyObject *mod_name = _PyDict_GetItemIdWithError(m->globals, &PyId___name__);
        if (mod_name && PyUnicode_Check(mod_name)) {
            if (!suppress) {
                PyErr_Format(PyExc_AttributeError,
                                "strict module '%U' has no attribute '%U'",
                                mod_name,
                                name);
            }
            return NULL;
        }
        if (PyErr_Occurred()) {
            return NULL;
        }
    }
    if (!suppress) {
        PyErr_Format(
            PyExc_AttributeError, "strict module has no attribute '%U'", name);
    }
    return NULL;
}

static PyObject *
strictmodule_getattro(Ci_StrictModuleObject *m, PyObject *name)
{
    return strictmodule_lookupattro_impl(m, name, 0);
}

static int
strictmodule_setattro(Ci_StrictModuleObject *m, PyObject *name, PyObject *value)
{
    PyObject *modname = StrictModule_GetNameObject(m);
    if (modname == NULL) {
        return -1;
    }
    if (value == NULL) {
        PyErr_Format(PyExc_AttributeError,
                        "cannot delete attribute '%U' of strict module %U", name, modname);
    } else {
        PyErr_Format(PyExc_AttributeError,
                        "cannot modify attribute '%U' of strict module %U", name, modname);
    }

    Py_DECREF(modname);
    return -1;
}

static PyMemberDef strictmodule_members[] = {
    {NULL}
};

static PyMethodDef strictmodule_methods[] = {
    {"__dir__", strictmodule_dir, METH_NOARGS,
     PyDoc_STR("__dir__() -> list\nspecialized dir() implementation")},
     {
         "patch", strictmodule_patch, METH_VARARGS, PyDoc_STR("Patch a strict module. Only enabled for testing")
     },
     {
         "patch_delete",
         strictmodule_patch_delete,
         METH_VARARGS,
         PyDoc_STR("Patch by deleting a field from strict module. Only enabled for testing")
     },
    {0}
};

static PyGetSetDef strict_module_getset[] = {
    {"__dict__", strict_module_dict_get, NULL, NULL, NULL},
    {"__name__", strict_module_name_get, NULL, NULL, NULL},
    {"__patch_enabled__", strict_module_patch_enabled, NULL, NULL, NULL},
    {NULL}
};


PyTypeObject Ci_StrictModule_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "StrictModule",                             /* tp_name */
    sizeof(Ci_StrictModuleObject),               /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)strictmodule_dealloc,           /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    (reprfunc)module_repr,                      /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    (getattrofunc)strictmodule_getattro,        /* tp_getattro */
    (setattrofunc)strictmodule_setattro,        /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)strictmodule_traverse,        /* tp_traverse */
    (inquiry)strictmodule_clear,                /* tp_clear */
    0,                                          /* tp_richcompare */
    offsetof(PyModuleObject, md_weaklist),      /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    strictmodule_methods,                       /* tp_methods */
    strictmodule_members,                       /* tp_members */
    strict_module_getset,                       /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)strictmodule_init,                /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    Ci_StrictModule_New,                        /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};
