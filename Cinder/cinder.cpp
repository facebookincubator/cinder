// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinder/cinder.h"
#include "Jit/pyjit.h"

static int cinder_dict_watcher_id = -1;
static int cinder_func_watcher_id = -1;

static int cinder_dict_watcher(
    PyDict_WatchEvent event,
    PyObject* dict,
    PyObject* key,
    PyObject* new_value) {
  switch (event) {
    case PyDict_EVENT_ADDED:
    case PyDict_EVENT_MODIFIED:
    case PyDict_EVENT_DELETED:
      if (!PyUnicode_CheckExact(key)) {
        _PyJIT_NotifyDictUnwatch(dict);
      } else {
        _PyJIT_NotifyDictKey(dict, key, new_value);
      }
      break;
    case PyDict_EVENT_CLEARED:
      _PyJIT_NotifyDictClear(dict);
      break;
    case PyDict_EVENT_CLONED:
    case PyDict_EVENT_DEALLOCATED:
      _PyJIT_NotifyDictUnwatch(dict);
      break;
  }
  return 0;
}

static int cinder_install_dict_watcher() {
  int watcher_id = PyDict_AddWatcher(cinder_dict_watcher);
  if (watcher_id < 0) {
    return -1;
  }
  cinder_dict_watcher_id = watcher_id;
  return 0;
}

void Cinder_WatchDict(PyObject* dict) {
  if (PyDict_Watch(cinder_dict_watcher_id, dict) < 0) {
    PyErr_Print();
    JIT_ABORT("Cinder: unable to watch dict.");
  }
}

void Cinder_UnwatchDict(PyObject* dict) {
  if (PyDict_Unwatch(cinder_dict_watcher_id, dict) < 0) {
    PyErr_Print();
    JIT_ABORT("Unable to unwatch dict.");
  }
}

static int cinder_func_watcher(
    PyFunction_WatchEvent event,
    PyFunctionObject* func,
    PyObject* new_value) {
  switch (event) {
    case PyFunction_EVENT_CREATE:
      PyEntry_init(func);
      break;
    case PyFunction_EVENT_MODIFY_CODE:
      _PyJIT_FuncModified(func);
      // having deopted the func, we want to immediately consider recompiling.
      // func_set_code will assign this again later, but we do it early so
      // PyEntry_init can consider the new code object now
      Py_INCREF(new_value);
      Py_XSETREF(func->func_code, new_value);
      PyEntry_init(func);
      break;
    case PyFunction_EVENT_MODIFY_DEFAULTS:
      break;
    case PyFunction_EVENT_MODIFY_KWDEFAULTS:
      break;
    case PyFunction_EVENT_MODIFY_QUALNAME:
      // allow reconsideration of whether this function should be compiled
      if (!_PyJIT_IsCompiled((PyObject*)func)) {
        // func_set_qualname will assign this again, but we need to assign it
        // now so that PyEntry_init can consider the new qualname
        Py_INCREF(new_value);
        Py_XSETREF(func->func_qualname, new_value);
        PyEntry_init(func);
      }
      break;
    case PyFunction_EVENT_DESTROY:
      _PyJIT_FuncDestroyed(func);
      break;
  }
  return 0;
}

static int init_funcs_visitor(PyObject* obj, void*) {
  if (PyFunction_Check(obj)) {
    PyEntry_init((PyFunctionObject*)obj);
  }
  return 1;
}

static void init_already_existing_funcs() {
  PyUnstable_GC_VisitObjects(init_funcs_visitor, NULL);
}

static int cinder_install_func_watcher() {
  int watcher_id = PyFunction_AddWatcher(cinder_func_watcher);
  if (watcher_id < 0) {
    return -1;
  }
  cinder_func_watcher_id = watcher_id;
  return 0;
}

int Cinder_Init() {
  if (cinder_install_dict_watcher() < 0) {
    return -1;
  }
  if (cinder_install_func_watcher() < 0) {
    return -1;
  }
  init_already_existing_funcs();
  return _PyJIT_Initialize();
}

int Cinder_Fini() {
  return _PyJIT_Finalize();
}

int Cinder_InitSubInterp() {
  // HACK: for now we assume we are the only watcher out there, so that we can
  // just keep track of a single watcher ID rather than one per interpreter.
  int prev_dict_watcher_id = cinder_dict_watcher_id;
  JIT_CHECK(
      prev_dict_watcher_id >= 0,
      "Initializing sub-interpreter without main interpreter?");
  if (cinder_install_dict_watcher() < 0) {
    return -1;
  }
  JIT_CHECK(
      cinder_dict_watcher_id == prev_dict_watcher_id,
      "Somebody else watching dicts?");

  int prev_func_watcher_id = cinder_func_watcher_id;
  JIT_CHECK(
      prev_func_watcher_id >= 0,
      "Initializing sub-interpreter without main interpreter?");
  if (cinder_install_func_watcher() < 0) {
    return -1;
  }
  JIT_CHECK(
      cinder_func_watcher_id == prev_func_watcher_id,
      "Somebody else watching functions?");

  return 0;
}
