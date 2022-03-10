#include "Python.h"
#include "immutable_globals.h"

static PyObject * set_immutable_globals_creation(PyObject *self, PyObject *val) {
    if (PyBool_Check(val)) {
        int immutable_globals_creation = val == Py_True;
        set_immutable_globals_immutable_creation(immutable_globals_creation);
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "immutable_globals immutable creation must be bool not: %.400s", val->ob_type->tp_name);
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(set_immutable_globals_creation_doc,
"set_immutable_globals_creation(enable)\n\
\n\
Set to True to automatically create immutable_globals compatible immutable objects."
);


static PyObject * get_immutable_globals_creation() {
    return PyBool_FromLong(__immutable_globals_creation);
}

PyDoc_STRVAR(get_immutable_globals_creation_doc,
"get_immutable_globals_creation()\n\
\n\
Return immutable_globals immutable creation flag."
);

static PyObject * set_immutable_globals_detection(PyObject *self, PyObject *val) {
    if (PyBool_Check(val)) {
        int immutable_globals_detection = val == Py_True;
        set_immutable_globals_immutable_detection(immutable_globals_detection);
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "immutable_globals immutable detection must be bool not: %.400s", val->ob_type->tp_name);
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(set_immutable_globals_detection_doc,
"set_immutable_globals_detection(enable)\n\
\n\
Set to True to automatically detect when immutable_globals compatible immutable objects are modified."
);


static PyObject * get_immutable_globals_detection() {
    return PyBool_FromLong(__immutable_globals_detection);
}

PyDoc_STRVAR(get_immutable_globals_detection_doc,
"get_immutable_globals_detection()\n\
\n\
Return immutable_globals immutable creation flag."
);
static struct PyMethodDef immutable_globals_module_methods[] = {
    {"set_immutable_globals_creation", set_immutable_globals_creation, METH_O, set_immutable_globals_creation_doc},
    {"get_immutable_globals_creation", get_immutable_globals_creation, METH_NOARGS, get_immutable_globals_creation_doc},
    {"set_immutable_globals_detection", set_immutable_globals_detection, METH_O, set_immutable_globals_detection_doc},
    {"get_immutable_globals_detection", get_immutable_globals_detection, METH_NOARGS, get_immutable_globals_detection_doc},
    {NULL, NULL}
};

PyDoc_STRVAR(doc_immutable_globals, "Immutable Globals specific methods");

static struct PyModuleDef immutable_globalsmodule = {
    PyModuleDef_HEAD_INIT,
    "_immutable_globals",
    doc_immutable_globals,
    -1,
    immutable_globals_module_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit__immutable_globals(void)
{
    PyObject *m;
    /* Create the module and add the functions */
    m = PyModule_Create(&immutable_globalsmodule);
    if (m == NULL) {
        return NULL;
    }
		Py_INCREF(&PyIDict_Type);
    PyModule_AddObject(m, "ImmutableDict", (PyObject *)&PyIDict_Type);
    return m;
}
