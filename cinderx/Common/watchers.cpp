#include "Python.h"

#include "cinderx/Jit/pyjit.h"
#include "cinderx/Jit/log.h"
#include "cinderx/Shadowcode/shadowcode.h"
#include "cinderx/Common/watchers.h"

static int dict_watcher_id = -1;
static int type_watcher_id = -1;
static int func_watcher_id = -1;
static int code_watcher_id = -1;

static int install_dict_watcher() {
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
  dict_watcher_id = watcher_id;
  return 0;
}

static int install_type_watcher() {
  int watcher_id = PyType_AddWatcher([](PyTypeObject* type) {
    _PyShadow_TypeModified(type);
  _PyJIT_TypeModified(type);
    return 0;
  });
  if (watcher_id < 0) {
    return -1;
  }
  type_watcher_id = watcher_id;
  return 0;
}

static int install_func_watcher() {
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
        if (!_PyJIT_IsCompiled(func)) {
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
  func_watcher_id = watcher_id;
  return 0;
}

static int install_code_watcher() {
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
  code_watcher_id = watcher_id;
  return 0;
}

int Ci_Watchers_Init() {
  if (install_dict_watcher() < 0) {
    return -1;
  }
  if (install_type_watcher() < 0) {
    return -1;
  }
  if (install_func_watcher() < 0) {
    return -1;
  }
  if (install_code_watcher() < 0) {
    return -1;
  }

  return 0;
}

int Ci_Watchers_Fini() {
  if (dict_watcher_id != -1 && PyDict_ClearWatcher(dict_watcher_id)) {
    return -1;
  }
  dict_watcher_id = -1;

  if (type_watcher_id != -1 && PyType_ClearWatcher(type_watcher_id)) {
    return -1;
  }
  type_watcher_id = -1;

  if (func_watcher_id != -1 && PyFunction_ClearWatcher(func_watcher_id)) {
    return -1;
  }
  func_watcher_id = -1;

  if (code_watcher_id != -1 && PyCode_ClearWatcher(code_watcher_id)) {
    return -1;
  }
  code_watcher_id = -1;

  return 0;
}

void Ci_Watchers_WatchDict(PyObject* dict) {
  if (PyDict_Watch(dict_watcher_id, dict) < 0) {
    PyErr_Print();
    JIT_ABORT("Unable to watch dict.");
  }
}

void Ci_Watchers_UnwatchDict(PyObject* dict) {
  if (PyDict_Unwatch(dict_watcher_id, dict) < 0) {
    PyErr_Print();
    JIT_ABORT("Unable to unwatch dict.");
  }
}

void Ci_Watchers_WatchType(PyTypeObject* type) {
  PyType_Watch(type_watcher_id, (PyObject *)type);
}

void Ci_Watchers_UnwatchType(PyTypeObject* type) {
  PyType_Unwatch(type_watcher_id, (PyObject *)type);
}
