/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#ifndef Py_SHADOW_FRAME_H
#define Py_SHADOW_FRAME_H

#include "Python.h"
#include "frameobject.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PYSF_CODE_RT, PYSF_CODE_OBJ, PYSF_GEN } _PyShadowFrame_PtrKind;

const unsigned int _PyShadowFrame_NumHasPyFrameBits = 1;
const uintptr_t _PyShadowFrame_HasPyFrameMask = 1;
const unsigned int _PyShadowFrame_NumPtrKindBits = 2;
const uintptr_t _PyShadowFrame_PtrKindMask =
    ((1 << _PyShadowFrame_NumPtrKindBits) - 1)
    << _PyShadowFrame_NumHasPyFrameBits;
const unsigned int _PyShadowFrame_NumTagBits = 3;
const uintptr_t _PyShadowFrame_TagMask = (1 << _PyShadowFrame_NumTagBits) - 1;
const uintptr_t _PyShadowFrame_PtrMask = ~((uintptr_t)_PyShadowFrame_TagMask);

/*
 * Shadow frames are an optimization used by the JIT to avoid allocating
 * PyFrameObjects unless absolutely necessary (e.g. when a user calls
 * sys._getframe()).
 *
 * Shadow frames are stack allocated by both the interpreter and JIT-compiled
 * functions and are linked into a call stack. The top of the stack is stored in
 * PyThreadState.
 *
 * When a user requests a Python frame for a JIT-compiled function, the runtime
 * will allocate one and insert it into the appropriate place in chain of
 * PyFrameObjects. If the JIT-compiled function corresponded to a generator, the
 * newly allocated PyFrameObject will be linked to the corresponding generator
 * for the rest of its execution.
 */
typedef struct _PyShadowFrame {
  struct _PyShadowFrame *prev;

  /*
   * Shadow frames need to support two main use cases: materializing
   * PyFrameObjects and introspecting the active call stack.
   *
   * When materializing a PyFrameObject, we need the following:
   *
   * 1. An indicator for whether or not we need to materialize anything.  We
   *    don't need to materialize anything if the shadow frame corresponds to
   *    an interpreted function or we already materialized a frame for it.
   * 2. If (1) is true, we need access to enough metadata about the
   *    corresponding function to allow us to create a `PyFrameObject` for
   *    it. (A pointer to the associated `jit::CodeRuntime` is enough.)
   * 3. If (1) is true, and the function is a generator, we also need access
   *    to the generator so that we can attach the newly materialized frame to
   *    it.
   *
   * For stack introspection, we'll want to walk either the synchronous call
   * stack or the "await stack" and retrieve the PyCodeObject for each member.
   * The synchronous call stack is represented by the linked-list of shadow
   * frames that begins at the top-most shadow frame of the current thread.
   * The "await stack" consists of the chain of coroutines that are
   * transitively awaiting on the top-most coroutine of the current
   * thread. This chain is threaded through the coroutine object; to recover it
   * from a shadow frame, we must be able to go from a shadow frame to its
   * associated coroutine object.
   *
   * We encode the necessary information in a tagged pointer with the following
   * format:
   *
   * [pointer][pointer_type][has_pyframe]
   *  61 bits  2 bits        1 bit
   *
   * For non-generator functions, the `has_pyframe` bit indicates whether or
   * not there is a corresponding `PyFrameObject` in the linked-list of active
   * Python frames. For generator functions the `gi_frame` field on the
   * associated generator is the source of truth. The `has_pyframe` bit is
   * treated as an optimization. A corresponding `PyFrameObject` will exist if
   * it's set, however, one may also exist even if it is unset.
   *
   * The `pointer_type` bits specify the type of `pointer`.
   * - PYSF_CODE_RT: `jit::CodeRuntime`, whose `py_code_` field points to a
   * `PyCodeObject`.
   * - PYSF_CODE_OBJ: `PyCodeObject`
   * - PYSF_GEN: A subtype of `PyGenObject`
   *
   * Mask off the bottom 3 bits to retrieve the raw pointer.
   *
   * For interpreted functions, the `has_pyframe` bit of `data` will always be
   * set.  If the function being executing is not a generator, `pointer_type`
   * will be `PYSF_CODE_OBJ` and `pointer` will point to a
   * `PyCodeObject`. Otherwise, `pointer_type` will be `PYSF_GEN` and `pointer`
   * will point to subtype of `PyGenObject`.
   *
   * For JIT-compiled functions, the value of the `has_pyframe` bit will depend
   * on whether or not a frame has been materialized. If the function is not a
   * generator, `pointer_type` will be `PYSF_CODE_RT` and `pointer` will point
   * to a `jit::CodeRuntime`. Otherwise, `pointer_type` be `PYSF_GEN` and
   * `pointer` will point to a subtype of `PyGenObject` (the same as the
   * interpreted case).
   *
   */
  uintptr_t data;
} _PyShadowFrame;

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
  _PyShadowFrame_PtrKind ptr_kind = PYSF_CODE_OBJ;
  if (py_frame->f_gen != NULL) {
    ptr = py_frame->f_gen;
    ptr_kind = PYSF_GEN;
  }
  shadow_frame->data = _PyShadowFrame_MakeData(ptr, ptr_kind, true);
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
