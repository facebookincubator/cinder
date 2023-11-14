#include "cinder/cinder.h"
#include <Python.h>

#include "Jit/log.h"

static bool g_was_initialized = false;

static PyObject* init(PyObject * /*self*/, PyObject * /*obj*/) {
  if (g_was_initialized) {
    Py_RETURN_FALSE;
  }
  if (Cinder_Init()) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to initialize CinderX");
    return NULL;
  }
  g_was_initialized = true;
  Py_RETURN_TRUE;
}

static void module_free(void *) {
  if (g_was_initialized) {
    g_was_initialized = false;
    JIT_CHECK(Cinder_Fini() == 0, "Failed to finalize CinderX");
  }
}

static PyMethodDef _cinderx_methods[] = {
    {"init", init, METH_NOARGS,
     "This must be called early. Preferably before any user code is run."},
    {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef _cinderx_module = {
    PyModuleDef_HEAD_INIT,  "_cinderx", "The internal CinderX extension module",
    /*m_size=*/-1, // Doesn't support sub-interpreters
    _cinderx_methods,
    /*m_slots=*/nullptr,
    /*m_traverse=*/nullptr,
    /*m_clear=*/nullptr,
    /*m_free=*/module_free,
};

PyMODINIT_FUNC PyInit__cinderx(void) {
  // Deliberate single-phase initialization.
  return PyModule_Create(&_cinderx_module);
}
