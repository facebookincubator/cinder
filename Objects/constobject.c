#include "Python.h"
#include "pycore_object.h"
#include "constobject.h"

#ifndef PyConstObject_MAXFREELIST
#define PyConstObject_MAXFREELIST 512
#endif
static PyConstObject *free_list[PyConstObject_MAXFREELIST];
static int numfree = 0;

static void
const_dealloc(PyConstObject *co) {
  PyConst_CheckExact(co);
  Py_XDECREF(co->ob_item);

  if (numfree < PyConstObject_MAXFREELIST) {
    PyObject_GC_UnTrack(co);
    free_list[numfree++] = co;
  } else {
    Py_TYPE(co)->tp_free((PyObject *)co);
  }
}

static PyObject *
const_repr(PyConstObject *co) {
  PyConst_CheckExact(co);

  if (co->ob_item == NULL) {
    return PyUnicode_FromString("NULL");
  }

  PyObject *org_repr = Py_TYPE(co->ob_item)->tp_repr((PyObject *)co->ob_item);
  PyObject *repr = PyUnicode_FromFormat("<Const %S>", org_repr);
  Py_DECREF(org_repr);
  return repr;
}

static int
const_init(PyConstObject *self, PyObject *args, PyObject *kwargs) {
  PyConst_CheckExact(self);
  if (self->ob_item != NULL) {
    Py_XDECREF(self->ob_item);
    self->ob_item = NULL;
  }

  PyObject *item = PyTuple_GET_ITEM(args, 0);

  if (Py_TYPE(item) == &PyConst_Type) {
    PyConstObject *co = (PyConstObject *)item;
    Py_XINCREF(co->ob_item);
    self->ob_item = co->ob_item;
  } else {
    Py_XINCREF(item);
    self->ob_item = item;
  }
  return 0;
}

static int
const_traverse(PyConstObject *self, visitproc visit, void *arg) {
  Py_VISIT(self->ob_item);
  return 0;
}

PyObject *const_getattr(PyObject *self, PyObject *name) {
  PyConst_CheckExact(self);
  PyConstObject *co = (PyConstObject *)self;
  // XXX: for now, we assume ob_item's tp_getattro is well behaved
  // need to revisit after the semantic for const object is done.
  PyObject *attr = Py_TYPE(co->ob_item)->tp_getattro(co->ob_item, name);
  if (attr != NULL && Py_TYPE(attr) != &PyConst_Type) {
    PyObject *const_attr = PyConst_New();
    PyConst_SetItem(const_attr, attr);
    return const_attr;
  }
  return attr;
}

static PyObject *
const_richcompare(PyObject *v, PyObject *w, int op) {
  PyConst_CheckExact(v);

  PyConstObject *lhs = (PyConstObject *)v;
  if (Py_TYPE(lhs->ob_item)->tp_richcompare == NULL) {
    Py_RETURN_NOTIMPLEMENTED;
  }

  // TODO: this function needs to be revisited after the semantic for
  // comparisons is determined.
  if (Py_TYPE(w) != &PyConst_Type) {
    return Py_TYPE(lhs->ob_item)->tp_richcompare(lhs->ob_item, w, op);
  } else {
    PyConstObject *rhs = (PyConstObject *)w;
    return Py_TYPE(lhs->ob_item)->tp_richcompare(lhs->ob_item, rhs->ob_item, op);
  }
}

static PyObject *
const_new() {
  PyConstObject *co = NULL;

  if (numfree == 0) {
    co = PyObject_GC_New(PyConstObject, &PyConst_Type);
    if (co == NULL) {
      return NULL;
    }
  } else {
    co = free_list[--numfree];
    _Py_NewReference((PyObject *)co);
    _PyObject_GC_TRACK(co);
  }

  co->ob_item = NULL;
  return (PyObject *)co;
}

PyTypeObject PyConst_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "const",
    sizeof(PyConstObject),
    0,
    (destructor)const_dealloc,   /* tp_dealloc */
    0,                           /* tp_vectorcall_offset */
    0,                           /* tp_getattr */
    0,                           /* tp_setattr */
    0,                           /* tp_as_async */
    (reprfunc)const_repr,        /* tp_repr */
    0,                           /* tp_as_number */
    0,                           /* tp_as_sequence */
    0,                           /* tp_as_mapping */
    PyObject_HashNotImplemented, /* tp_hash */
    0,                           /* tp_call */
    0,                           /* tp_str */
    const_getattr,               /* tp_getattro */
    0,                           /* tp_setattro */
    0,                           /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_LIST_SUBCLASS, /* tp_flags */
    0,                            /* tp_doc */
    (traverseproc)const_traverse, /* tp_traverse */
    0,                            /* tp_clear */
    const_richcompare,            /* tp_richcompare */
    0,                            /* tp_weaklistoffset */
    0,                            /* tp_iter */
    0,                            /* tp_iternext */
    0,                            /* tp_methods */
    0,                            /* tp_members */
    0,                            /* tp_getset */
    0,                            /* tp_base */
    0,                            /* tp_dict */
    0,                            /* tp_descr_get */
    0,                            /* tp_descr_set */
    0,                            /* tp_dictoffset */
    (initproc)const_init,         /* tp_init */
    PyType_GenericAlloc,          /* tp_alloc */
    const_new,                    /* tp_new */
    PyObject_GC_Del,              /* tp_free */
};

PyObject *
PyConst_New() {
  return const_new();
}

PyObject *
PyConst_GetItem(PyObject *ob) {
  PyConst_CheckExact(ob);
  PyConstObject *co = (PyConstObject *)ob;
  return co->ob_item;
}

int
PyConst_SetItem(PyObject *ob, PyObject *item_ob) {
  PyConst_CheckExact(ob);
  if (Py_TYPE(item_ob) == &PyConst_Type) {
    Py_XDECREF(item_ob);
    PyErr_BadInternalCall();
    return -1;
  }

  PyConstObject *co = (PyConstObject *)ob;
  Py_XSETREF(co->ob_item, item_ob);
  return 0;
}

static int
PyConst_ClearFreeList(void) {
    PyConstObject *co = NULL;
    int ret = numfree;
    while (numfree > 0) {
        co = free_list[--numfree];
        PyObject_GC_Del(co);
    }
    return ret;
}

void
PyConst_Fini(void) {
    PyConst_ClearFreeList();
}
