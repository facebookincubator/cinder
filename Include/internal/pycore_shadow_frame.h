/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#ifndef Py_SHADOW_FRAME_H
#define Py_SHADOW_FRAME_H

#include "Python.h"
#include "frameobject.h"
#include "internal/pycore_shadow_frame_struct.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

static const unsigned int _PyShadowFrame_NumPtrKindBits = 1;
static const uintptr_t _PyShadowFrame_PtrKindMask =
    (1 << _PyShadowFrame_NumPtrKindBits) - 1;
static const uintptr_t _PyShadowFrame_PtrMask = ~_PyShadowFrame_PtrKindMask;

static inline _PyShadowFrame_PtrKind
_PyShadowFrame_GetPtrKind(_PyShadowFrame *shadow_frame) {
  return (_PyShadowFrame_PtrKind)(shadow_frame->data &
                                  _PyShadowFrame_PtrKindMask);
}

static inline void *_PyShadowFrame_GetPtr(_PyShadowFrame *shadow_frame) {
  return (void *)(shadow_frame->data & _PyShadowFrame_PtrMask);
}

static inline PyFrameObject *
_PyShadowFrame_GetPyFrame(_PyShadowFrame *shadow_frame) {
  assert(_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME);
  return (PyFrameObject *)_PyShadowFrame_GetPtr(shadow_frame);
}

PyGenObject *_PyShadowFrame_GetGen(_PyShadowFrame *shadow_frame);

static inline uintptr_t
_PyShadowFrame_MakeData(void *ptr, _PyShadowFrame_PtrKind ptr_kind) {
  assert(((uintptr_t)ptr & _PyShadowFrame_PtrKindMask) == 0);
  return (uintptr_t)ptr | ptr_kind;
}

static inline void _PyShadowFrame_PushInterp(PyThreadState *tstate,
                                             _PyShadowFrame *shadow_frame,
                                             PyFrameObject *py_frame) {
  shadow_frame->prev = tstate->shadow_frame;
  tstate->shadow_frame = shadow_frame;
  shadow_frame->data = _PyShadowFrame_MakeData(py_frame, PYSF_PYFRAME);
}

static inline void _PyShadowFrame_Pop(PyThreadState *tstate,
                                      _PyShadowFrame *shadow_frame) {
  assert(tstate->shadow_frame == shadow_frame);
  tstate->shadow_frame = shadow_frame->prev;
}

#ifdef __cplusplus
}
#endif

#endif /* Py_LIMITED_API */
#endif /* !Py_SHADOW_FRAME_H */
