/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#ifndef Py_SHADOW_FRAME_H
#define Py_SHADOW_FRAME_H

#include "Python.h"
#include "frameobject.h"
#include "pycore_shadow_frame_struct.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PYSF_CODE_RT, PYSF_CODE_OBJ, PYSF_GEN } _PyShadowFrame_PtrKind;

static const unsigned int _PyShadowFrame_NumHasPyFrameBits = 1;
static const uintptr_t _PyShadowFrame_HasPyFrameMask = 1;
static const unsigned int _PyShadowFrame_NumPtrKindBits = 2;
static const uintptr_t _PyShadowFrame_PtrKindMask =
    ((1 << _PyShadowFrame_NumPtrKindBits) - 1)
    << _PyShadowFrame_NumHasPyFrameBits;
static const unsigned int _PyShadowFrame_NumTagBits = 3;
static const uintptr_t _PyShadowFrame_TagMask = (1 << _PyShadowFrame_NumTagBits) - 1;
static const uintptr_t _PyShadowFrame_PtrMask = ~((uintptr_t)_PyShadowFrame_TagMask);

static inline _PyShadowFrame_PtrKind
_PyShadowFrame_GetPtrKind(_PyShadowFrame *shadow_frame) {
  return (_PyShadowFrame_PtrKind)((shadow_frame->data &
                                   _PyShadowFrame_PtrKindMask) >>
                                  _PyShadowFrame_NumHasPyFrameBits);
}

static inline void *_PyShadowFrame_GetPtr(_PyShadowFrame *shadow_frame) {
  return (void *)(shadow_frame->data & _PyShadowFrame_PtrMask);
}

static inline PyGenObject *_PyShadowFrame_GetGen(_PyShadowFrame *shadow_frame) {
  assert(_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_GEN);
  return (PyGenObject *)_PyShadowFrame_GetPtr(shadow_frame);
}

static inline bool _PyShadowFrame_HasPyFrame(_PyShadowFrame *shadow_frame) {
  return (shadow_frame->data & _PyShadowFrame_HasPyFrameMask) ||
         ((_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_GEN) &&
          (_PyShadowFrame_GetGen(shadow_frame)->gi_frame != NULL));
}

static inline void _PyShadowFrame_SetHasPyFrame(_PyShadowFrame *shadow_frame) {
  shadow_frame->data |= _PyShadowFrame_HasPyFrameMask;
}

static inline unsigned int
_PyShadowFrame_MakeTag(_PyShadowFrame_PtrKind ptr_kind, bool has_pyframe) {
  return (ptr_kind << _PyShadowFrame_NumHasPyFrameBits) | has_pyframe;
}

static inline uintptr_t _PyShadowFrame_MakeData(void *ptr,
                                                _PyShadowFrame_PtrKind ptr_kind,
                                                bool has_pyframe) {
  return ((uintptr_t)ptr) | _PyShadowFrame_MakeTag(ptr_kind, has_pyframe);
}

static inline void _PyShadowFrame_PushInterp(PyThreadState *tstate,
                                             _PyShadowFrame *shadow_frame,
                                             PyFrameObject *py_frame) {
  shadow_frame->prev = tstate->shadow_frame;
  tstate->shadow_frame = shadow_frame;
  void *ptr = py_frame->f_code;
  shadow_frame->data = _PyShadowFrame_MakeData(ptr, PYSF_CODE_OBJ, true);
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
