// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinderhooks.h"
#include "cinder/cinder.h"
#include "cinder/hooks.h"
#include "Jit/pyjit.h"
#include "StaticPython/classloader.h"
#include "StaticPython/descrobject_vectorcall.h"
#include "Shadowcode/shadowcode.h"
#include "StaticPython/methodobject_vectorcall.h"

static int cinder_dict_watcher_id = -1;
static int cinder_type_watcher_id = -1;
static int cinder_func_watcher_id = -1;
static int cinder_code_watcher_id = -1;

static int cinder_install_dict_watcher() {
  int watcher_id = PyDict_AddWatcher([](
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
          _PyClassLoader_NotifyDictChange((PyDictObject *)dict, key);
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
  });
  if (watcher_id < 0) {
    return -1;
  }
  cinder_dict_watcher_id = watcher_id;
  return 0;
}

void Cinder_WatchDict(PyObject* dict) {
  if (PyDict_Watch(cinder_dict_watcher_id, dict) < 0) {
    PyErr_Print();
    JIT_ABORT("Unable to watch dict.");
  }
}

void Cinder_UnwatchDict(PyObject* dict) {
  if (PyDict_Unwatch(cinder_dict_watcher_id, dict) < 0) {
    PyErr_Print();
    JIT_ABORT("Unable to unwatch dict.");
  }
}

static int cinder_install_type_watcher() {
  int watcher_id = PyType_AddWatcher([](PyTypeObject* type) {
    _PyShadow_TypeModified(type);
    _PyJIT_TypeModified(type);
    return 0;
  });
  if (watcher_id < 0) {
    return -1;
  }
  cinder_type_watcher_id = watcher_id;
  return 0;
}

void Cinder_WatchType(PyTypeObject* type) {
  PyType_Watch(cinder_type_watcher_id, (PyObject *)type);
}

void Cinder_UnwatchType(PyTypeObject* type) {
  PyType_Unwatch(cinder_type_watcher_id, (PyObject *)type);
}

static int cinder_install_func_watcher() {
  int watcher_id = PyFunction_AddWatcher([](
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
  });
  if (watcher_id < 0) {
    return -1;
  }
  cinder_func_watcher_id = watcher_id;
  return 0;
}

static void init_already_existing_funcs() {
  PyUnstable_GC_VisitObjects([](PyObject* obj, void*){
    if (PyFunction_Check(obj)) {
      PyEntry_init((PyFunctionObject*)obj);
    }
    return 1;
  }, nullptr);
}

static int cinder_install_code_watcher() {
  int watcher_id = PyCode_AddWatcher([](PyCodeEvent event, PyCodeObject* co) {
    if (event == PY_CODE_EVENT_DESTROY) {
      _PyShadow_ClearCache((PyObject *)co);
      _PyJIT_CodeDestroyed(co);
    }
    return 0;
  });
  if (watcher_id < 0) {
    return -1;
  }
  cinder_code_watcher_id = watcher_id;
  return 0;
}

static void init_already_existing_types() {
  PyUnstable_GC_VisitObjects([](PyObject* obj, void*) {
    if (PyType_Check(obj) && PyType_HasFeature((PyTypeObject*)obj, Py_TPFLAGS_READY)) {
      _PyJIT_TypeCreated((PyTypeObject*)obj);
    }
    return 1;
  }, nullptr);
}

int Cinder_Init() {
  Ci_hook_type_created = _PyJIT_TypeCreated;
  Ci_hook_type_destroyed = _PyJIT_TypeDestroyed;
  Ci_hook_type_name_modified = _PyJIT_TypeNameModified;
  Ci_hook_type_pre_setattr = _PyClassLoader_InitTypeForPatching;
  Ci_hook_type_setattr = _PyClassLoader_UpdateSlot;
  Ci_hook_JIT_GetProfileNewInterpThread = _PyJIT_GetProfileNewInterpThreads;
  Ci_hook_JIT_GetFrame = _PyJIT_GetFrame;
  Ci_hook_PyCMethod_New = Ci_PyCMethod_New_METH_TYPED;
  Ci_hook_PyDescr_NewMethod = Ci_PyDescr_NewMethod_METH_TYPED;
  Ci_hook_WalkStack = Ci_WalkStack;
  Ci_hook_code_sizeof_shadowcode = Ci_code_sizeof_shadowcode;

  // This should be the very last hook installed
  Ci_cinderx_initialized = 1;

  init_already_existing_types();

  // Prevent the linker from omitting the object file containing the parallel
  // GC implementation. This is the only reference from another translation
  // unit to symbols defined in the file. Without it the linker will omit the
  // object file when linking the libpython archive into the main python
  // binary.
  //
  // TODO(T168696266): Remove this once we migrate to cinderx.
  PyObject *res = Cinder_GetParallelGCSettings();
  if (res == nullptr) {
    return -1;
  }
  Py_DECREF(res);
  if (cinder_install_dict_watcher() < 0) {
    return -1;
  }
  if (cinder_install_type_watcher() < 0) {
    return -1;
  }
  if (cinder_install_func_watcher() < 0) {
    return -1;
  }
  if (cinder_install_code_watcher() < 0) {
    return -1;
  }
  init_already_existing_funcs();
  return _PyJIT_Initialize();
}

int Cinder_Fini() {
  _PyClassLoader_ClearCache();
  return _PyJIT_Finalize();
}
