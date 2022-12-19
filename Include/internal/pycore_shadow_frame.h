/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#ifndef Py_SHADOW_FRAME_H
#define Py_SHADOW_FRAME_H

#include "Python.h"
#include "frameobject.h"
#include "internal/pycore_shadow_frame_struct.h"

#include <stdint.h>

#include "cinder/port-assert.h"

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

// TODO(mpage) - Generalize bit setting/getting into helpers?
static const unsigned int _PyShadowFrame_NumTagBits = 4;
static const uintptr_t _PyShadowFrame_TagMask =
    (1 << _PyShadowFrame_NumTagBits) - 1;
static const uintptr_t _PyShadowFrame_PtrMask = ~_PyShadowFrame_TagMask;

#define TAG_MASK(num_bits, off) ((1 << num_bits) - 1) << off

static const unsigned int kShadowFrameSize = sizeof(_PyShadowFrame);
#define SHADOW_FRAME_FIELD_OFF(field) (int{(offsetof(_PyShadowFrame, field))})
static const unsigned int kJITShadowFrameSize = sizeof(JITShadowFrame);
#define JIT_SHADOW_FRAME_FIELD_OFF(field)                                      \
  (int{(offsetof(JITShadowFrame, field))})
static const unsigned int _PyShadowFrame_NumPtrKindBits = 2;
static const unsigned int _PyShadowFrame_PtrKindOff = 0;
static const uintptr_t _PyShadowFrame_PtrKindMask =
    TAG_MASK(_PyShadowFrame_NumPtrKindBits, _PyShadowFrame_PtrKindOff);

static const unsigned int _PyShadowFrame_NumOwnerBits = 1;
static const unsigned int _PyShadowFrame_OwnerOff =
    _PyShadowFrame_PtrKindOff + _PyShadowFrame_NumPtrKindBits;
static const uintptr_t _PyShadowFrame_OwnerMask =
    TAG_MASK(_PyShadowFrame_NumOwnerBits, _PyShadowFrame_OwnerOff);

#undef TAG_MASK

static inline _PyShadowFrame_PtrKind
_PyShadowFrame_GetPtrKind(_PyShadowFrame *shadow_frame) {
  return (_PyShadowFrame_PtrKind)(shadow_frame->data &
                                  _PyShadowFrame_PtrKindMask);
}

static inline void _PyShadowFrame_SetOwner(_PyShadowFrame *shadow_frame,
                                           _PyShadowFrame_Owner owner) {
  uintptr_t data = shadow_frame->data & ~_PyShadowFrame_OwnerMask;
  shadow_frame->data = data | (owner << _PyShadowFrame_OwnerOff);
}

static inline _PyShadowFrame_Owner
_PyShadowFrame_GetOwner(_PyShadowFrame *shadow_frame) {
  uintptr_t data = shadow_frame->data & _PyShadowFrame_OwnerMask;
  return (_PyShadowFrame_Owner)(data >> _PyShadowFrame_OwnerOff);
}

static inline void *_PyShadowFrame_GetPtr(_PyShadowFrame *shadow_frame) {
  return (void *)(shadow_frame->data & _PyShadowFrame_PtrMask);
}

static inline PyFrameObject *
_PyShadowFrame_GetPyFrame(_PyShadowFrame *shadow_frame) {
  assert(_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME);
  return (PyFrameObject *)_PyShadowFrame_GetPtr(shadow_frame);
}

int _PyShadowFrame_HasGen(_PyShadowFrame *shadow_frame);
PyGenObject *_PyShadowFrame_GetGen(_PyShadowFrame *shadow_frame);
void _PyShadowFrame_DumpStack(PyThreadState* state);

static inline uintptr_t _PyShadowFrame_MakeData(void *ptr,
                                                _PyShadowFrame_PtrKind ptr_kind,
                                                _PyShadowFrame_Owner owner) {
  assert(((uintptr_t)ptr & _PyShadowFrame_PtrKindMask) == 0);
  return (uintptr_t)ptr | (owner << _PyShadowFrame_OwnerOff) |
         (ptr_kind << _PyShadowFrame_PtrKindOff);
}

static inline void _PyShadowFrame_PushInterp(PyThreadState *tstate,
                                             _PyShadowFrame *shadow_frame,
                                             PyFrameObject *py_frame) {
  shadow_frame->prev = tstate->shadow_frame;
  tstate->shadow_frame = shadow_frame;
  shadow_frame->data =
      _PyShadowFrame_MakeData(py_frame, PYSF_PYFRAME, PYSF_INTERP);
}

static inline void _PyShadowFrame_Pop(PyThreadState *tstate,
                                      _PyShadowFrame *shadow_frame) {
  assert(tstate->shadow_frame == shadow_frame);
  tstate->shadow_frame = shadow_frame->prev;
  shadow_frame->prev = NULL;
}

static inline void *JITShadowFrame_GetOrigPtr(JITShadowFrame *jit_sf) {
  return (void *)(jit_sf->orig_data & _PyShadowFrame_PtrMask);
}

static inline _PyShadowFrame_PtrKind
JITShadowFrame_GetOrigPtrKind(JITShadowFrame *jit_sf) {
  return (_PyShadowFrame_PtrKind)(jit_sf->orig_data &
                                  _PyShadowFrame_PtrKindMask);
}

/*
 * Return the kind of runtime pointer (PYSF_RTFS or PYSF_CODE_RT) held by
 * jit_sf.
 */
static inline _PyShadowFrame_PtrKind
JITShadowFrame_GetRTPtrKind(JITShadowFrame *jit_sf) {
  _PyShadowFrame_PtrKind kind = _PyShadowFrame_GetPtrKind(&jit_sf->sf);
  if (kind == PYSF_PYFRAME) {
    return JITShadowFrame_GetOrigPtrKind(jit_sf);
  }
  return kind;
}

/*
 * Return the runtime pointer (jit::CodeRuntime* or jit::RuntimeFrameState*)
 * held by jit_sf.
 */
static inline void *JITShadowFrame_GetRTPtr(JITShadowFrame *jit_sf) {
  _PyShadowFrame_PtrKind kind = _PyShadowFrame_GetPtrKind(&jit_sf->sf);
  if (kind == PYSF_PYFRAME) {
    return JITShadowFrame_GetOrigPtr(jit_sf);
  }
  return _PyShadowFrame_GetPtr((_PyShadowFrame *)jit_sf);
}

/* Return a borrowed reference to the code object for shadow_frame */
PyCodeObject *_PyShadowFrame_GetCode(_PyShadowFrame *shadow_frame);


/* Returns the fully qualified name of code running in the frame */
PyObject *_PyShadowFrame_GetFullyQualifiedName(_PyShadowFrame *shadow_frame);


/* Populates codeobject pointers in the given arrays. Meant to only be called
  from profiling frameworks. Both async_stack and sync_stack contain borrowed
  references.

  `async_stack` - Contains pointers to PyCodeObject on successful run. If errors
  occur, the contents should not be used.

  `async_linenos` - Is populated with the line numbers (corresponding to each) entry
  in `async_stack`

  `sync_stack` - Similar to `async_stack`, but contains sync stack entries.

  `sync_linenos` - Similar to `async_linenos`, but contains for the sync stack.

  `array_len` - Assumes that all the above arrays are of the same length, which is
  specified by this parameter.

  `async_stack_len_out`, `sync_stack_len_out` - These are out parameters. On successful
  run, they contain the lengths of the async and sync stacks respectively.
*/
PyAPI_FUNC(int) _PyShadowFrame_WalkAndPopulate(
    PyCodeObject** async_stack,
    int* async_linenos,
    PyCodeObject** sync_stack,
    int* sync_linenos,
    int array_capacity,
    int* async_stack_len_out,
    int* sync_stack_len_out);

/* Looks up the awaiter shadow frame (if any) from the given shadow frame */
_PyShadowFrame* _PyShadowFrame_GetAwaiterFrame(_PyShadowFrame *shadow_frame);

#ifdef __cplusplus
}
#endif

#endif /* Py_LIMITED_API */
#endif /* !Py_SHADOW_FRAME_H */
