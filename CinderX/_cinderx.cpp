#include <Python.h>

static PyMethodDef _cinderx_methods[] = {
  {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef _cinderxmodule = {
  PyModuleDef_HEAD_INIT,
  "_cinderx",
  "The internal CinderX extension module",
  /*m_size=*/0,
  _cinderx_methods,
  /*m_slots=*/nullptr,
  /*m_traverse=*/nullptr,
  /*m_clear=*/nullptr,
  /*m_free=*/nullptr,
};

PyMODINIT_FUNC PyInit__cinderx(void) {
  return PyModuleDef_Init(&_cinderxmodule);
}
