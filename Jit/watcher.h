// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef JIT_WATCHER_H
#define JIT_WATCHER_H

#include "Jit/util.h"
#include "Python.h"
#include "switchboard.h"

#include <functional>
#include <unordered_set>

namespace jit {

// Helper class for any code that needs to subscribe to changes to type objects
// and receive a callback when the type is changed.
//
// Subclasses must implement a typeChanged method with the following signature:
//
//   void typeChanged(PyTypeObject* type)
//
// that will be called whenever a watched type is modified. It may be called
// with `nullptr` if a watched type was gc-ed.
//
// The watcher automatically stops watching the type modification immediately
// after the type has been modified. Call `watchType` again if you want to
// continue watching the type.
template <typename T>
class TypeWatcher {
 protected:
  // Watch `type` for modifications.
  //
  // typeChanged() will be called with `type` when it is modified.
  bool watchType(PyTypeObject* type) {
    PyObject* capsule = PyCapsule_New(this, nullptr, nullptr);
    if (capsule == nullptr) {
      return false;
    }
    Switchboard* sb = reinterpret_cast<Switchboard*>(_PyType_GetSwitchboard());
    PyObject* typeobj = reinterpret_cast<PyObject*>(type);
    PyObject* handle =
        Switchboard_Subscribe(sb, typeobj, TypeWatcher<T>::notify, capsule);
    Py_XDECREF(handle);
    Py_DECREF(capsule);
    return handle != nullptr;
  }

 private:
  static void
  notify(PyObject* handle, PyObject* capsule, PyObject* modified_type_weakref) {
    T* watcher = static_cast<T*>(PyCapsule_GetPointer(capsule, nullptr));
    JIT_CHECK(watcher != nullptr, "capsule empty");
    PyObject* obj = PyWeakref_GetObject(modified_type_weakref);
    if (obj == Py_None) {
      watcher->typeChanged(nullptr);
    } else {
      watcher->typeChanged(reinterpret_cast<PyTypeObject*>(obj));
    }
    auto sb = reinterpret_cast<Switchboard*>(_PyType_GetSwitchboard());
    Switchboard_Unsubscribe(sb, handle);
  }
};

} // namespace jit
#endif
