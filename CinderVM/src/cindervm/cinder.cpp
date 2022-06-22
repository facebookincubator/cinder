#include <Python.h>

static PyObject *hello(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(arg)) {
  Py_RETURN_NONE;
}

static PyMethodDef cindervm_methods[] = {
    {"hello", reinterpret_cast<PyCFunction>(hello), METH_NOARGS, "Say hello"},
    {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef cindervmmodule = {
    PyModuleDef_HEAD_INIT,
    "cindervm",
    "A sample Cinder extension module",
    /*m_size=*/0,
    cindervm_methods,
    /*m_slots=*/nullptr,
    /*m_traverse=*/nullptr,
    /*m_clear=*/nullptr,
    /*m_free=*/nullptr,
};

PyMODINIT_FUNC PyInit_cindervm(void) {
  PyObject *module = PyState_FindModule(&cindervmmodule);
  if (module != nullptr) {
    Py_INCREF(module);
    return module;
  }
  module = PyModule_Create(&cindervmmodule);
  if (module == nullptr) {
    return nullptr;
  }
  PyState_AddModule(module, &cindervmmodule);
  return module;
}
