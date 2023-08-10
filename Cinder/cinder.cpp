// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinder/cinder.h"
#include "Jit/pyjit.h"

static int cinder_dict_watcher_id = -1;

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

int Cinder_Init() {
  if (cinder_install_dict_watcher() < 0) {
    return -1;
  }
  return _PyJIT_Initialize();
}

int Cinder_Fini() {
  return _PyJIT_Finalize();
}

int Cinder_InitSubInterp() {
  // HACK: for now we assume we are the only dict watcher out there, so that we
  // can just keep track of a single dict watcher ID rather than one per
  // interpreter.
  int prev_watcher_id = cinder_dict_watcher_id;
  JIT_CHECK(
      prev_watcher_id >= 0,
      "Initializing sub-interpreter without main interpreter?");
  if (cinder_install_dict_watcher() < 0) {
    return -1;
  }
  JIT_CHECK(
      cinder_dict_watcher_id == prev_watcher_id,
      "Somebody else watching dicts?");
  return 0;
}
