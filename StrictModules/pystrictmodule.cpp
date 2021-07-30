// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
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

static int
PyListToCharArray(PyObject* pyList, const char** arr, Py_ssize_t size) {
  PyObject** items = _PyList_ITEMS(pyList);
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
    arr[_i] = elem_str;
  }
  return 0;
}

static int StrictModuleLoaderObject_init(
    StrictModuleLoaderObject* self,
    PyObject* args,
    PyObject*) {
  PyObject* import_paths_obj;
  PyObject* stub_import_path_obj;
  PyObject* allow_list_obj;
  PyObject* allow_list_exact_obj;
  PyObject* load_strictmod_builtin;
  load_strictmod_builtin = Py_True;
  if (!PyArg_ParseTuple(
          args,
          "OOOO|O",
          &import_paths_obj,
          &stub_import_path_obj,
          &allow_list_obj,
          &allow_list_exact_obj,
          &load_strictmod_builtin)) {
    return -1;
  }

  if (!PyList_Check(import_paths_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "import_paths is expect to be list, but got %s object",
        import_paths_obj);
    return -1;
  }
  if (!PyList_Check(allow_list_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "allow_list is expect to be list, but got %s object",
        allow_list_obj);
    return -1;
  }
  if (!PyList_Check(allow_list_exact_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "allow_list_exact is expect to be list, but got %s object",
        allow_list_exact_obj);
    return -1;
  }
  if (!PyUnicode_Check(stub_import_path_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "stub_import_path is expect to be str, but got %s object",
        stub_import_path_obj);
    return -1;
  }
  // Import paths
  Py_ssize_t import_size = PyList_GET_SIZE(import_paths_obj);
  const char* import_paths_arr[import_size];
  if (PyListToCharArray(import_paths_obj, import_paths_arr, import_size) < 0) {
    return -1;
  }

  if (StrictModuleChecker_SetImportPaths(
          self->checker, import_paths_arr, import_size) < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "failed to set import paths on StrictModuleLoader object");
    return -1;
  }
  // allowlist for module names
  Py_ssize_t allow_list_size = PyList_GET_SIZE(allow_list_obj);
  const char* allow_list_arr[allow_list_size];
  if (PyListToCharArray(allow_list_obj, allow_list_arr, allow_list_size) < 0) {
    return -1;
  }

  if (StrictModuleChecker_SetAllowListPrefix(
          self->checker, allow_list_arr, allow_list_size) < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "failed to set allowlist on StrictModuleLoader object");
    return -1;
  }
  // allowlist for exact module names
  Py_ssize_t allow_list_exact_size = PyList_GET_SIZE(allow_list_exact_obj);
  const char* allow_list_exact_arr[allow_list_exact_size];
  if (PyListToCharArray(
          allow_list_exact_obj, allow_list_exact_arr, allow_list_exact_size) <
      0) {
    return -1;
  }

  if (StrictModuleChecker_SetAllowListExact(
          self->checker, allow_list_exact_arr, allow_list_exact_size) < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "failed to set allowlist on StrictModuleLoader object");
    return -1;
  }

  // stub paths
  const char* stub_str = PyUnicode_AsUTF8(stub_import_path_obj);
  if (stub_str == NULL) {
    return -1;
  }
  int stub_path_set =
      StrictModuleChecker_SetStubImportPath(self->checker, stub_str);
  if (stub_path_set < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "failed to set the stub import path on StrictModuleLoader object");
    return -1;
  }

  // load strict module builtins
  int should_load = PyObject_IsTrue(load_strictmod_builtin);
  if (should_load < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "error checking 'should_load_builtin' on StrictModuleLoader");
    return -1;
  }
  if (should_load) {
    int loaded = StrictModuleChecker_LoadStrictModuleBuiltins(self->checker);
    if (loaded < 0) {
      PyErr_SetString(
          PyExc_RuntimeError,
          "failed to load the strict module builtins on StrictModuleLoader "
          "object");
      return -1;
    }
  }
  return 0;
}

/* StrictModuleLoader methods */

static void StrictModuleLoader_dealloc(StrictModuleLoaderObject* self) {
  StrictModuleChecker_Free(self->checker);
  PyObject_Del(self);
}

/* Returns new reference */
static PyObject* errorInfoToTuple(ErrorInfo* info) {
  PyObject* py_lineno = NULL;
  PyObject* py_col = NULL;
  PyObject* result = NULL;
  py_lineno = PyLong_FromLong(info->lineno);
  if (!py_lineno) {
    goto err_cleanup;
  }
  py_col = PyLong_FromLong(info->col);
  if (!py_col) {
    goto err_cleanup;
  }
  result = PyTuple_Pack(4, info->msg, info->filename, py_lineno, py_col);
  if (!result) {
    goto err_cleanup;
  }
  Py_DECREF(py_lineno);
  Py_DECREF(py_col);
  return result;
err_cleanup:
  Py_XDECREF(py_lineno);
  Py_XDECREF(py_col);
  return NULL;
}

static PyObject* StrictModuleLoader_check(
    StrictModuleLoaderObject* self,
    PyObject* args) {
  PyObject* mod_name;
  PyObject* py_is_strict;
  PyObject* py_result_tuple;
  PyObject* errors;
  if (!PyArg_ParseTuple(args, "U", &mod_name)) {
    return NULL;
  }
  int error_count = 0;
  int is_strict = 0;
  StrictAnalyzedModule* mod = StrictModuleChecker_Check(
      self->checker, mod_name, &error_count, &is_strict);
  errors = PyList_New(error_count);
  ErrorInfo error_infos[error_count];
  if (error_count > 0 && mod != NULL) {
    if (StrictModuleChecker_GetErrors(mod, error_infos, error_count) < 0) {
      goto err_cleanup;
    }
    for (int i = 0; i < error_count; ++i) {
      PyObject* py_err_tuple = errorInfoToTuple(&(error_infos[i]));
      if (!py_err_tuple) {
        goto err_cleanup;
      }
      PyList_SET_ITEM(errors, i, py_err_tuple);
    }
  }
  for (int i = 0; i < error_count; ++i) {
    ErrorInfo_Clean(&(error_infos[i]));
  }

  py_is_strict = PyBool_FromLong(is_strict);
  py_result_tuple = PyTuple_Pack(2, py_is_strict, errors);

  return py_result_tuple;
err_cleanup:
  for (int i = 0; i < error_count; ++i) {
    ErrorInfo_Clean(&(error_infos[i]));
  }
  Py_XDECREF(mod_name);
  Py_XDECREF(errors);
  return NULL;
}

static PyObject* StrictModuleLoader_set_force_strict(
    StrictModuleLoaderObject* self,
    PyObject* args) {
  PyObject* force_strict;
  if (!PyArg_ParseTuple(args, "O", &force_strict)) {
    return NULL;
  }
  int ok = StrictModuleChecker_SetForceStrict(self->checker, force_strict);
  if (ok == 0) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyObject* StrictModuleLoader_get_analyzed_count(
    StrictModuleLoaderObject* self) {
  int count = StrictModuleChecker_GetAnalyzedModuleCount(self->checker);
  return PyLong_FromLong(count);
}

static PyMethodDef StrictModuleLoader_methods[] = {
    {"check",
     (PyCFunction)StrictModuleLoader_check,
     METH_VARARGS,
     PyDoc_STR("check(mod_name: str) -> Tuple[int, List[Tuple[str, str, int, "
               "int]]]")},
    {"set_force_strict",
     (PyCFunction)StrictModuleLoader_set_force_strict,
     METH_VARARGS,
     PyDoc_STR("force_strict(force: bool) -> bool")},
    {"get_analyzed_count",
     (PyCFunction)StrictModuleLoader_get_analyzed_count,
     METH_NOARGS,
     PyDoc_STR("get_analyzed_count() -> int")},
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

const char* MUTABLE_DEC = "<mutable>";
const char* LOOSE_SLOTS_DEC = "<loose_slots>";
const char* EXTRA_SLOTS_DEC = "<extra_slots>";
const char* ENABLE_SLOTS_DEC = "<enable_slots>";
const char* CACHED_PROP_DEC = "<cached_property>";

#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
