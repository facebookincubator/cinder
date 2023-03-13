// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/pystrictmodule.h"

#include "StrictModules/pycore_dependencies.h"
#include "structmember.h"

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif

// Analysis Result
static PyObject* AnalysisResult_new(PyTypeObject* type, PyObject*, PyObject*) {
  StrictModuleAnalysisResult* self;
  self = (StrictModuleAnalysisResult*)type->tp_alloc(type, 0);
  if (self == NULL)
    return NULL;
  self->valid_module = 0;
  self->module_name = NULL;
  self->file_name = NULL;
  self->module_kind = 0;
  self->stub_kind = 0;
  self->ast = NULL;
  self->ast_preprocessed = NULL;
  self->symtable = NULL;
  self->errors = NULL;
  return (PyObject*)self;
}

static int AnalysisResult_init(
    StrictModuleAnalysisResult* self,
    PyObject* args,
    PyObject*) {
  PyObject* module_name;
  PyObject* file_name;
  int module_kind;
  int stub_kind;
  PyObject* ast;
  PyObject* ast_preprocessed;
  PyObject* symtable;
  PyObject* errors;

  if (!PyArg_ParseTuple(
          args,
          "UUiiOOOO",
          &module_name,
          &file_name,
          &module_kind,
          &stub_kind,
          &ast,
          &ast_preprocessed,
          &symtable,
          &errors)) {
    return -1;
  }
  self->valid_module = 1;
  self->module_name = module_name;
  Py_INCREF(self->module_name);
  self->file_name = file_name;
  Py_INCREF(self->file_name);
  self->module_kind = module_kind;
  self->stub_kind = stub_kind;
  self->ast = ast;
  Py_INCREF(self->ast);
  self->ast_preprocessed = ast_preprocessed;
  Py_INCREF(self->ast_preprocessed);
  self->symtable = symtable;
  Py_INCREF(self->symtable);
  self->errors = errors;
  Py_INCREF(self->errors);

  return 0;
}

static PyObject* create_AnalysisResult_Helper(
    int valid_module,
    PyObject* module_name,
    PyObject* file_name,
    int module_kind,
    int stub_kind,
    PyObject* ast,
    PyObject* ast_preprocessed,
    PyObject* symtable,
    PyObject* errors) {
  StrictModuleAnalysisResult* self;
  self = (StrictModuleAnalysisResult*)PyObject_GC_New(
      StrictModuleAnalysisResult, &StrictModuleAnalysisResult_Type);
  self->valid_module = valid_module;
  self->module_name = module_name;
  self->file_name = file_name;
  self->module_kind = module_kind;
  self->stub_kind = stub_kind;
  self->ast = ast;
  self->ast_preprocessed = ast_preprocessed;
  self->symtable = symtable;
  self->errors = errors;
  PyObject_GC_Track(self);
  return (PyObject*)self;
}

static PyObject* create_AnalysisResult(
    StrictAnalyzedModule* mod,
    PyObject* module_name,
    PyObject* errors,
    PyArena* arena) {
  if (mod == NULL) {
    Py_INCREF(module_name);
    Py_INCREF(errors);
    return create_AnalysisResult_Helper(
        0, module_name, NULL, 0, 0, NULL, NULL, NULL, errors);
  }
  // all interface functions return new references
  PyObject* filename = StrictAnalyzedModule_GetFilename(mod);
  int mod_kind = StrictAnalyzedModule_GetModuleKind(mod);
  int stub_kind = StrictAnalyzedModule_GetStubKind(mod);
  PyObject* ast = StrictAnalyzedModule_GetAST(mod, arena, 0);
  PyObject* ast_preprocessed = StrictAnalyzedModule_GetAST(mod, arena, 1);
  PyObject* symtable = StrictAnalyzedModule_GetSymtable(mod);
  Py_INCREF(module_name);
  Py_INCREF(errors);
  return create_AnalysisResult_Helper(
      1,
      module_name,
      filename,
      mod_kind,
      stub_kind,
      ast,
      ast_preprocessed,
      symtable,
      errors);
}

static void AnalysisResult_dealloc(StrictModuleAnalysisResult* self) {
  if (self != NULL) {
    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->module_name);
    Py_XDECREF(self->file_name);
    Py_XDECREF(self->ast);
    Py_XDECREF(self->ast_preprocessed);
    Py_XDECREF(self->symtable);
    Py_XDECREF(self->errors);
    PyObject_GC_Del(self);
  }
}

static int AnalysisResult_traverse(
    StrictModuleAnalysisResult* self,
    visitproc visit,
    void* arg) {
  Py_VISIT(self->module_name);
  Py_VISIT(self->file_name);
  Py_VISIT(self->ast);
  Py_VISIT(self->ast_preprocessed);
  Py_VISIT(self->symtable);
  Py_VISIT(self->errors);
  return 0;
}

static int AnalysisResult_clear(StrictModuleAnalysisResult* self) {
  Py_CLEAR(self->module_name);
  Py_CLEAR(self->file_name);
  Py_CLEAR(self->ast);
  Py_CLEAR(self->ast_preprocessed);
  Py_CLEAR(self->symtable);
  Py_CLEAR(self->errors);
  return 0;
}

static PyMemberDef AnalysisResult_members[] = {
    {"is_valid",
     T_BOOL,
     offsetof(StrictModuleAnalysisResult, valid_module),
     READONLY,
     "whether the analyzed module is found or valid"},
    {"module_name",
     T_OBJECT,
     offsetof(StrictModuleAnalysisResult, module_name),
     READONLY,
     "module name"},
    {"file_name",
     T_OBJECT,
     offsetof(StrictModuleAnalysisResult, file_name),
     READONLY,
     "file name"},
    {"module_kind",
     T_INT,
     offsetof(StrictModuleAnalysisResult, module_kind),
     READONLY,
     "whether module is strict (1), static (2) or neither (0)"},
    {"stub_kind",
     T_INT,
     offsetof(StrictModuleAnalysisResult, stub_kind),
     READONLY,
     "stub kind represented as a bit mask."},
    {"ast",
     T_OBJECT,
     offsetof(StrictModuleAnalysisResult, ast),
     READONLY,
     "original AST of the module"},
    {"ast_preprocessed",
     T_OBJECT,
     offsetof(StrictModuleAnalysisResult, ast_preprocessed),
     READONLY,
     "preprocessed AST of the module"},
    {"symtable",
     T_OBJECT,
     offsetof(StrictModuleAnalysisResult, symtable),
     READONLY,
     "symbol table of the module"},
    {"errors",
     T_OBJECT,
     offsetof(StrictModuleAnalysisResult, errors),
     0,
     "list of errors"},
    {NULL, 0, 0, 0, NULL} /* Sentinel */
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject StrictModuleAnalysisResult_Type = {
    PyVarObject_HEAD_INIT(
        NULL,
        0) "strictmodule.StrictModuleAnalysisResult", /* tp_name */
    sizeof(StrictModuleAnalysisResult), /* tp_basicsize */
    0, /* tp_itemsize */
    (destructor)AnalysisResult_dealloc, /* tp_dealloc */
    0, /* tp_vectorcall_offset */
    0, /* tp_getattr */
    0, /* tp_setattr */
    0, /* tp_as_async */
    0, /* tp_repr */
    0, /* tp_as_number */
    0, /* tp_as_sequence */
    0, /* tp_as_mapping */
    0, /* tp_hash */
    0, /* tp_call */
    0, /* tp_str */
    0, /* tp_getattro */
    0, /* tp_setattro */
    0, /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, /* tp_flags */
    "Analysis result of strict module loader", /* tp_doc */
    (traverseproc)AnalysisResult_traverse, /* tp_traverse */
    (inquiry)AnalysisResult_clear, /* tp_clear */
    0, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    0, /* tp_iter */
    0, /* tp_iternext */
    0, /* tp_methods */
    AnalysisResult_members, /* tp_members */
    0, /* tp_getset */
    0, /* tp_base */
    0, /* tp_dict */
    0, /* tp_descr_get */
    0, /* tp_descr_set */
    0, /* tp_dictoffset */
    (initproc)AnalysisResult_init, /* tp_init */
    0, /* tp_alloc */
    AnalysisResult_new, /* tp_new */
};
#pragma GCC diagnostic pop

// Module Loader
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
  int _i;
  PyObject* elem;
  for (_i = 0; _i < size; _i++) {
    elem = PyList_GetItem(pyList, _i);
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
  PyObject* allow_list_regex_obj = nullptr;
  load_strictmod_builtin = Py_True;
  PyObject* verbose_logging = Py_False;
  PyObject* disable_analysis = Py_False;
  if (!PyArg_ParseTuple(
          args,
          "OOOO|OOOO",
          &import_paths_obj,
          &stub_import_path_obj,
          &allow_list_obj,
          &allow_list_exact_obj,
          &load_strictmod_builtin,
          &allow_list_regex_obj,
          &verbose_logging,
          &disable_analysis)) {
    return -1;
  }

  if (!PyList_Check(import_paths_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "import_paths is expect to be list, but got %S object",
        import_paths_obj);
    return -1;
  }
  if (!PyList_Check(allow_list_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "allow_list is expect to be list, but got %S object",
        allow_list_obj);
    return -1;
  }
  if (!PyList_Check(allow_list_exact_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "allow_list_exact is expect to be list, but got %S object",
        allow_list_exact_obj);
    return -1;
  }
  if (!PyUnicode_Check(stub_import_path_obj)) {
    PyErr_Format(
        PyExc_TypeError,
        "stub_import_path is expect to be str, but got %S object",
        stub_import_path_obj);
    return -1;
  }

  if (!PyBool_Check(verbose_logging)) {
    PyErr_Format(
        PyExc_TypeError,
        "verbose_logging is expect to be bool, but got %S object",
        stub_import_path_obj);
    return -1;
  }

  if (!PyBool_Check(disable_analysis)) {
    PyErr_Format(
        PyExc_TypeError,
        "disable_analysis is expect to be bool, but got %S object",
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

  if (allow_list_regex_obj != nullptr) {
    Py_ssize_t allow_list_regex_size = PyList_GET_SIZE(allow_list_regex_obj);
    const char* allow_list_regex_arr[allow_list_regex_size];
    if (PyListToCharArray(
          allow_list_regex_obj, allow_list_regex_arr, allow_list_regex_size) <
        0) {
      return -1;
    }
    if (StrictModuleChecker_SetAllowListRegex(
            self->checker, allow_list_regex_arr, allow_list_regex_size) < 0) {
      PyErr_SetString(
           PyExc_RuntimeError,
          "failed to set the regex allowlist on StrictModuleLoader object");
      return -1;
    }
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
  int should_enable_verbose_logging = PyObject_IsTrue(verbose_logging);
  if (should_enable_verbose_logging < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "error checking 'verbose_logging' on StrictModuleLoader");
    return -1;
  }
  if (should_enable_verbose_logging) {
    StrictModuleChecker_EnableVerboseLogging(self->checker);
  }

  int should_disable_analysis = PyObject_IsTrue(disable_analysis);
  if (should_disable_analysis < 0) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "error checking 'disable_analysis' on StrictModuleLoader");
    return -1;
  }
  if (should_disable_analysis) {
    StrictModuleChecker_DisableAnalysis(self->checker);
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
  PyObject* errors;
  PyArena* arena;
  PyObject* result;
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
  if (PyErr_Occurred()) {
    goto err_cleanup;
  }
  for (int i = 0; i < error_count; ++i) {
    ErrorInfo_Clean(&(error_infos[i]));
  }
  arena = StrictModuleChecker_GetArena(self->checker);
  result = create_AnalysisResult(mod, mod_name, errors, arena);
  Py_XDECREF(errors);
  return result;

err_cleanup:
  for (int i = 0; i < error_count; ++i) {
    ErrorInfo_Clean(&(error_infos[i]));
  }
  Py_XDECREF(errors);
  return NULL;
}

static PyObject* StrictModuleLoader_check_source(
    StrictModuleLoaderObject* self,
    PyObject* args) {
  // args to parse, do not decref since these are borrowed
  PyObject* source; // str or bytes
  PyObject* file_name;
  PyObject* mod_name;
  PyObject* submodule_search_locations; // list of string

  // outputs
  PyObject* errors;
  PyArena* arena;
  PyObject* result;

  // parameter parsing
  if (!PyArg_ParseTuple(
          args,
          "OUUO",
          &source,
          &file_name,
          &mod_name,
          &submodule_search_locations)) {
    return NULL;
  }
  // source str
  const char* source_str;
  // verify search locations
  if (!PyList_Check(submodule_search_locations)) {
    PyErr_Format(
        PyExc_TypeError,
        "submodule_search_locations is expect to be list, but got %S object",
        submodule_search_locations);
    return NULL;
  }
  Py_ssize_t search_list_size = PyList_GET_SIZE(submodule_search_locations);
  const char* search_list[search_list_size];
  if (PyListToCharArray(
          submodule_search_locations, search_list, search_list_size) < 0) {
    return NULL;
  }

  // verify source
  PyCompilerFlags cf = _PyCompilerFlags_INIT;
  // buffer containing source str
  PyObject* source_copy;
  source_str =
      _Py_SourceAsString(source, "parse", "str or bytes", &cf, &source_copy);
  if (source_str == NULL) {
    return NULL;
  }

  int error_count = 0;
  int is_strict = 0;
  StrictAnalyzedModule* mod = StrictModuleChecker_CheckSource(
      self->checker,
      source_str,
      mod_name,
      file_name,
      search_list,
      search_list_size,
      &error_count,
      &is_strict);

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
  if (PyErr_Occurred()) {
    goto err_cleanup;
  }
  for (int i = 0; i < error_count; ++i) {
    ErrorInfo_Clean(&(error_infos[i]));
  }
  arena = StrictModuleChecker_GetArena(self->checker);
  result = create_AnalysisResult(mod, mod_name, errors, arena);
  Py_XDECREF(errors);
  Py_XDECREF(source_copy);
  return result;
err_cleanup:
  for (int i = 0; i < error_count; ++i) {
    ErrorInfo_Clean(&(error_infos[i]));
  }
  Py_XDECREF(errors);
  Py_XDECREF(source_copy);
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

static PyObject* StrictModuleLoader_set_force_strict_by_name(
    StrictModuleLoaderObject* self,
    PyObject* args) {
  const char* forced_strict_module;
  if (!PyArg_ParseTuple(args, "s", &forced_strict_module)) {
    return NULL;
  }
  int ok = StrictModuleChecker_SetForceStrictByName(
      self->checker, forced_strict_module);
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

static PyObject* StrictModuleLoader_delete_module(
    StrictModuleLoaderObject* self,
    PyObject* args) {
  PyObject* name;
  if (!PyArg_ParseTuple(args, "U", &name)) {
    return NULL;
  }
  int ok =
      StrictModuleChecker_DeleteModule(self->checker, PyUnicode_AsUTF8(name));
  if (ok == 0) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyMethodDef StrictModuleLoader_methods[] = {
    {"check",
     (PyCFunction)StrictModuleLoader_check,
     METH_VARARGS,
     PyDoc_STR("check(mod_name: str) -> StrictAnalysisResult")},
    {"check_source",
     (PyCFunction)StrictModuleLoader_check_source,
     METH_VARARGS,
     PyDoc_STR("check_source("
               "source:str | bytes, file_name: str, mod_name: str, "
               "submodule_search_locations:List[str])"
               " -> StrictAnalysisResult")},
    {"set_force_strict",
     (PyCFunction)StrictModuleLoader_set_force_strict,
     METH_VARARGS,
     PyDoc_STR("set_force_strict(force: bool) -> bool")},
    {"set_force_strict_by_name",
     (PyCFunction)StrictModuleLoader_set_force_strict_by_name,
     METH_VARARGS,
     PyDoc_STR("set_force_strict(modname: str) -> bool")},
    {"get_analyzed_count",
     (PyCFunction)StrictModuleLoader_get_analyzed_count,
     METH_NOARGS,
     PyDoc_STR("get_analyzed_count() -> int")},
    {"delete_module",
     (PyCFunction)StrictModuleLoader_delete_module,
     METH_VARARGS,
     PyDoc_STR("delete_module(name: str) -> bool")},
    {NULL, NULL, 0, NULL} /* sentinel */
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject StrictModuleLoader_Type = {
    PyVarObject_HEAD_INIT(
        NULL,
        0) "strictmodule.StrictModuleLoader", /* tp_name */
    sizeof(StrictModuleLoaderObject), /* tp_basicsize */
    0, /* tp_itemsize */
    (destructor)StrictModuleLoader_dealloc, /* tp_dealloc */
    0, /* tp_vectorcall_offset */
    0, /* tp_getattr */
    0, /* tp_setattr */
    0, /* tp_as_async */
    0, /* tp_repr */
    0, /* tp_as_number */
    0, /* tp_as_sequence */
    0, /* tp_as_mapping */
    0, /* tp_hash */
    0, /* tp_call */
    0, /* tp_str */
    0, /* tp_getattro */
    0, /* tp_setattro */
    0, /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT, /* tp_flags */
    "Cinder implementation of strict module checker", /* tp_doc */
    0, /* tp_traverse */
    0, /* tp_clear */
    0, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    0, /* tp_iter */
    0, /* tp_iternext */
    StrictModuleLoader_methods, /* tp_methods */
    0, /* tp_members */
    0, /* tp_getset */
    0, /* tp_base */
    0, /* tp_dict */
    0, /* tp_descr_get */
    0, /* tp_descr_set */
    0, /* tp_dictoffset */
    (initproc)StrictModuleLoaderObject_init, /* tp_init */
    0, /* tp_alloc */
    StrictModuleLoaderObject_new, /* tp_new */
};
#pragma GCC diagnostic pop

#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
