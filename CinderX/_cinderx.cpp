#include <Python.h>

static PyObject *hello(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(arg)) {
  Py_RETURN_NONE;
}

static PyMethodDef _cinderx_methods[] = {
    {"hello", reinterpret_cast<PyCFunction>(hello), METH_NOARGS, "Say hello"},
    {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef _cinderxmodule = {
    PyModuleDef_HEAD_INIT,
    "_cinderx",
    "A sample Cinder extension module",
    /*m_size=*/0,
    _cinderx_methods,
    /*m_slots=*/nullptr,
    /*m_traverse=*/nullptr,
    /*m_clear=*/nullptr,
    /*m_free=*/nullptr,
};

PyMODINIT_FUNC PyInit__cinderx(void) {
  PyObject *module = PyState_FindModule(&_cinderxmodule);
  if (module != nullptr) {
    Py_INCREF(module);
    return module;
  }
  module = PyModule_Create(&_cinderxmodule);
  if (module == nullptr) {
    return nullptr;
  }
  PyState_AddModule(module, &_cinderxmodule);
  return module;
}
