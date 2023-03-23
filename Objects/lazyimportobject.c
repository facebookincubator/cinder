/* Lazy object implementation */

#include "Python.h"
#include "pycore_lazyimport.h"
#include "pycore_pystate.h"     // _PyThreadState_GET()

PyObject *
_PyLazyImport_NewModule(
    PyObject *name, PyObject *globals, PyObject *locals, PyObject *fromlist, PyObject *level)
{
    PyLazyImportObject *m;
    if (!name || !PyUnicode_Check(name) ||
        !globals || !locals || !fromlist) {
        PyErr_BadArgument();
        return NULL;
    }
    if (level == NULL) {
        level = PyLong_FromLong(0);
        if (level == NULL) {
            return NULL;
        }
    } else {
        Py_INCREF(level);
    }
    m = PyObject_GC_New(PyLazyImportObject, &PyLazyImport_Type);
    if (m == NULL) {
        return NULL;
    }
    m->lz_lazy_import = NULL;
    Py_INCREF(name);
    m->lz_name = name;
    Py_INCREF(globals);
    m->lz_globals = globals;
    Py_INCREF(locals);
    m->lz_locals = locals;
    Py_INCREF(fromlist);
    m->lz_fromlist = fromlist;
    m->lz_level = level;
    m->lz_resolved = NULL;
    m->lz_resolving = NULL;
    PyObject_GC_Track(m);
    return (PyObject *)m;
}

PyObject *
_PyLazyImport_NewObject(PyObject *from, PyObject *name)
{
    PyLazyImportObject *m;
    if (!from || !PyLazyImport_CheckExact(from) || !name || !PyUnicode_Check(name)) {
        PyErr_BadArgument();
        return NULL;
    }
    m = PyObject_GC_New(PyLazyImportObject, &PyLazyImport_Type);
    if (m == NULL) {
        return NULL;
    }
    PyLazyImportObject *d = (PyLazyImportObject *)from;
    if (d->lz_fromlist != NULL && d->lz_fromlist != Py_None) {
        PyObject *frmlst = PyList_New(0);
        if (frmlst == NULL) {
            return NULL;
        }
        PyList_Append(frmlst, name);
        PyObject *frm = _PyLazyImport_NewModule(
            d->lz_name, d->lz_globals, d->lz_locals, frmlst, d->lz_level);
        Py_DECREF(frmlst);
        if (frm == NULL) {
            return NULL;
        }
        m->lz_lazy_import = frm;
    } else {
        Py_INCREF(from);
        m->lz_lazy_import = from;
    }
    Py_INCREF(name);
    m->lz_name = name;
    m->lz_globals = NULL;
    m->lz_locals = NULL;
    m->lz_fromlist = NULL;
    m->lz_level = NULL;
    m->lz_resolved = NULL;
    m->lz_resolving = NULL;
    PyObject_GC_Track(m);
    return (PyObject *)m;
}

static void
lazy_import_dealloc(PyLazyImportObject *m)
{
    PyObject_GC_UnTrack(m);
    Py_XDECREF(m->lz_lazy_import);
    Py_XDECREF(m->lz_name);
    Py_XDECREF(m->lz_globals);
    Py_XDECREF(m->lz_locals);
    Py_XDECREF(m->lz_fromlist);
    Py_XDECREF(m->lz_level);
    Py_XDECREF(m->lz_resolved);
    Py_XDECREF(m->lz_resolving);
    Py_TYPE(m)->tp_free((PyObject *)m);
}

static PyObject *
lazy_import_name(PyLazyImportObject *m)
{
    if (m->lz_lazy_import != NULL) {
        PyObject *name = lazy_import_name((PyLazyImportObject *)m->lz_lazy_import);
        PyObject *res = PyUnicode_FromFormat("%U.%U", name, m->lz_name);
        Py_DECREF(name);
        return res;
    }
    if (m->lz_fromlist == NULL ||
        m->lz_fromlist == Py_None ||
        !PyObject_IsTrue(m->lz_fromlist)) {
        Py_ssize_t dot = PyUnicode_FindChar(m->lz_name, '.', 0, PyUnicode_GET_LENGTH(m->lz_name), 1);
        if (dot >= 0) {
            return PyUnicode_Substring(m->lz_name, 0, dot);
        }
    }
    Py_INCREF(m->lz_name);
    return m->lz_name;
}

static PyObject *
lazy_import_repr(PyLazyImportObject *m)
{
    PyObject *name = lazy_import_name(m);
    PyObject *res = PyUnicode_FromFormat("<lazy_import '%U'>", name);
    Py_DECREF(name);
    return res;
}

static int
lazy_import_traverse(PyLazyImportObject *m, visitproc visit, void *arg)
{
    Py_VISIT(m->lz_lazy_import);
    Py_VISIT(m->lz_name);
    Py_VISIT(m->lz_globals);
    Py_VISIT(m->lz_locals);
    Py_VISIT(m->lz_fromlist);
    Py_VISIT(m->lz_level);
    Py_VISIT(m->lz_resolved);
    Py_VISIT(m->lz_resolving);
    return 0;
}

static int
lazy_import_clear(PyLazyImportObject *m)
{
    Py_CLEAR(m->lz_lazy_import);
    Py_CLEAR(m->lz_name);
    Py_CLEAR(m->lz_globals);
    Py_CLEAR(m->lz_locals);
    Py_CLEAR(m->lz_fromlist);
    Py_CLEAR(m->lz_level);
    Py_CLEAR(m->lz_resolved);
    Py_CLEAR(m->lz_resolving);
    return 0;
}

PyObject *
_PyLazyImport_GetName(PyObject *lazy_import)
{
    assert(PyLazyImport_CheckExact(lazy_import));
    return lazy_import_name((PyLazyImportObject *)lazy_import);
}

PyTypeObject PyLazyImport_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "lazy_import",                              /* tp_name */
    sizeof(PyLazyImportObject),                       /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)lazy_import_dealloc,            /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    (reprfunc)lazy_import_repr,                 /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,                    /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)lazy_import_traverse,         /* tp_traverse */
    (inquiry)lazy_import_clear,                 /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    0,                                          /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    PyType_GenericNew,                          /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};
