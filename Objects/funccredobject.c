#include "Python.h"
#include "object.h"
#include "pycore_object.h"
#include "funccredobject.h"
#include "tupleobject.h"

#ifndef PyFunctionCredentialObject_MAXFREELIST
#define PyFunctionCredentialObject_MAXFREELIST 512
#endif
static PyFunctionCredentialObject *free_list[PyFunctionCredentialObject_MAXFREELIST];
static int numfree = 0;


static void
func_cred_dealloc(PyFunctionCredentialObject *fc) {
    Py_XDECREF(fc->module_name);
    Py_XDECREF(fc->class_name);
    Py_XDECREF(fc->function_name);
    if (numfree < PyFunctionCredentialObject_MAXFREELIST && PyFunctionCredential_CheckExact(fc)) {
        free_list[numfree++] = fc;
    } else {
        Py_TYPE(fc)->tp_free((PyObject *)fc);
    }
}

PyObject *
func_cred_new(PyObject *tuple) {
    PyFunctionCredentialObject *fc = NULL;

    // tuple should never be created by user code. It must be a tuple
    // with three elements.
    assert(PyTuple_CheckExact(tuple));
    assert(PyTuple_GET_SIZE(tuple) = 3);

    if (numfree == 0) {
        fc = PyObject_New(PyFunctionCredentialObject, &PyFunctionCredential_Type);
        if (fc == NULL) {
            return NULL;
        }
    } else {
        fc = free_list[--numfree];
        _Py_NewReference((PyObject *)fc);
    }

    fc->module_name = PyTuple_GetItem(tuple, 0);
    Py_INCREF(fc->module_name);
    fc->class_name = PyTuple_GetItem(tuple, 1);
    Py_INCREF(fc->class_name);
    fc->function_name = PyTuple_GetItem(tuple, 2);
    Py_INCREF(fc->function_name);

    return (PyObject *)fc;
}

static PyObject *
func_cred_repr(PyFunctionCredentialObject *fc) {
  PyObject *repr =
      PyUnicode_FromFormat("<Function Credential %U:%U:%U>", fc->module_name,
                           fc->class_name, fc->function_name);
  return repr;
}

static PyObject *
func_cred_richcompare(PyObject *v, PyObject *w, int op) {
    if (!PyFunctionCredential_CheckExact(v) || !PyFunctionCredential_CheckExact(w)) {
      Py_RETURN_NOTIMPLEMENTED;
    }

    PyFunctionCredentialObject *lhs = (PyFunctionCredentialObject *)v;
    PyFunctionCredentialObject *rhs = (PyFunctionCredentialObject *)w;
    PyObject *lhs_repr = func_cred_repr(lhs);
    PyObject *rhs_repr = func_cred_repr(rhs);

    return Py_TYPE(lhs_repr)->tp_richcompare(lhs_repr, rhs_repr, op);
}

static Py_hash_t
func_cred_hash(PyObject *self) {
    PyFunctionCredentialObject *fc = (PyFunctionCredentialObject *)self;
    Py_uhash_t x = PyUnicode_Type.tp_hash(fc->module_name);
    x ^= PyUnicode_Type.tp_hash(fc->class_name);
    x ^= PyUnicode_Type.tp_hash(fc->function_name);
    return x;
}

PyTypeObject PyFunctionCredential_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "function_credential",
    sizeof(PyFunctionCredentialObject),
    0,
    (destructor)func_cred_dealloc, /* tp_dealloc */
    0,                             /* tp_vectorcall_offset */
    0,                             /* tp_getattr */
    0,                             /* tp_setattr */
    0,                             /* tp_as_async */
    (reprfunc)func_cred_repr,      /* tp_repr */
    0,                             /* tp_as_number */
    0,                             /* tp_as_sequence */
    0,                             /* tp_as_mapping */
    func_cred_hash,                /* tp_hash */
    0,                             /* tp_call */
    0,                             /* tp_str */
    PyObject_GenericGetAttr,       /* tp_getattro */
    0,                             /* tp_setattro */
    0,                             /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,     /* tp_flags */
    0,                                /* tp_doc */
    0,          /* tp_traverse */
    0,                                /* tp_clear */
    func_cred_richcompare,            /* tp_richcompare */
    0,                                /* tp_weaklistoffset */
    0,                                /* tp_iter */
    0,                                /* tp_iternext */
    0,                                /* tp_methods */
    0,                                /* tp_members */
    0,                                /* tp_getset */
    0,                                /* tp_base */
    0,                                /* tp_dict */
    0,                                /* tp_descr_get */
    0,                                /* tp_descr_set */
    0,                                /* tp_dictoffset */
    (initproc)func_cred_new,          /* tp_init */
    PyType_GenericAlloc,              /* tp_alloc */
    0,                                /* tp_new */
    PyObject_Del,                  /* tp_free */
};
