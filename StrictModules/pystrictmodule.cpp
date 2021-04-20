#include "StrictModules/pystrictmodule.h"

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif

static PyObject*
StrictModuleLoaderObject_new(PyTypeObject* type, PyObject*, PyObject*) {
  StrictModuleLoaderObject* self;
  self = (StrictModuleLoaderObject*)type->tp_alloc(type, 0);
  if (self == NULL)
    return NULL;
  self->checker = StrictModuleChecker_New();
  return (PyObject*)self;
}

static int StrictModuleLoaderObject_init(
    StrictModuleLoaderObject* self,
    PyObject* args,
    PyObject*) {
  PyObject* import_paths_obj;
  if (!PyArg_ParseTuple(args, "O", &import_paths_obj))
    return -1;
  if (!PyList_Check(import_paths_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "import_paths is expect to be list, but got %s object",
        import_paths_obj);
    return -1;
  }
  PyObject** items = _PyList_ITEMS(import_paths_obj);
  Py_ssize_t size = PyList_GET_SIZE(import_paths_obj);
  const char* import_paths_arr[size];
  int _i;
  PyObject* elem;
  for (_i = 0; _i < size; _i++) {
    elem = items[_i];
    if (!PyUnicode_Check(elem)) {
      PyErr_Format(
          PyExc_TypeError,
          "import path is expect to be str, but got %s object",
          elem->ob_type->tp_name);
      return -1;
    }
    const char* elem_str = PyUnicode_AsUTF8(elem);
    if (elem_str == NULL) {
      return -1;
    }
    import_paths_arr[_i] = elem_str;
  }
  int paths_set =
      StrictModuleChecker_SetImportPaths(self->checker, import_paths_arr, size);
  if (paths_set < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "failed to set import paths on StrictModuleLoader object");
    return -1;
  }
  return 0;
}

/* StrictModuleLoader methods */

static void StrictModuleLoader_dealloc(StrictModuleLoaderObject* self) {
  StrictModuleChecker_Free(self->checker);
  PyObject_Del(self);
}

static PyObject* StrictModuleLoader_check(
    StrictModuleLoaderObject* self,
    PyObject* args) {
  PyObject* mod_name;
  if (!PyArg_ParseTuple(args, "U", &mod_name)) {
    return NULL;
  }
  int strict = StrictModuleChecker_Check(self->checker, mod_name);
  if (strict == 0) {
    return Py_True;
  }
  return Py_False;
}

static PyMethodDef StrictModuleLoader_methods[] = {
    {"check",
     (PyCFunction)StrictModuleLoader_check,
     METH_VARARGS,
     PyDoc_STR("check(mod_name: str) -> None")},
    {NULL, NULL, 0, NULL} /* sentinel */
};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject StrictModuleLoader_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "strictmodule.StrictModuleLoader",
    .tp_basicsize = sizeof(StrictModuleLoaderObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)StrictModuleLoader_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Cinder implementation of strict module checker",
    .tp_methods = StrictModuleLoader_methods,
    .tp_init = (initproc)StrictModuleLoaderObject_init,
    .tp_new = StrictModuleLoaderObject_new,
};
#pragma GCC diagnostic pop
#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
