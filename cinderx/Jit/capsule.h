// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"

#include "cinderx/Jit/log.h"
#include "cinderx/Jit/ref.h"

namespace jit {

namespace detail {
template <typename T>
void capsuleDestructor(PyObject* capsule) {
  auto ptr = static_cast<T*>(PyCapsule_GetPointer(capsule, nullptr));
  if (ptr == nullptr) {
    JIT_LOG("ERROR: Couldn't retrieve value from capsule {}", capsule);
    return;
  }
  delete ptr;
}
} // namespace detail

// Create a PyCapsule to hold the given C++ object, with a destructor that
// deletes the object.
template <typename T>
Ref<> makeCapsule(T* ptr) {
  return Ref<>::steal(
      PyCapsule_New(ptr, nullptr, detail::capsuleDestructor<T>));
}

} // namespace jit
