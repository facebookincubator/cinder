/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#ifndef Py_SHADOW_FRAME_H
#define Py_SHADOW_FRAME_H

#include "Python.h"
#include <stdint.h>

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

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
   * If `has_pyframe` is 0, this is set to the id of the CodeRuntime of the
   * corresponding JIT-compiled function. If `has_pyframe` is 1, this value is
   * unspecified.
   */
  uint32_t code_rt_id;

  /*
   * Set to 1 if there is an active Python frame associated with this (a
   * PyFrameObject reachable from tstate->frame) or 0 otherwise.
   */
  uint32_t has_pyframe;
} _PyShadowFrame;

static inline void _PyShadowFrame_PushInterp(PyThreadState *tstate,
                                             _PyShadowFrame *frame) {
  frame->prev = tstate->shadow_frame;
  tstate->shadow_frame = frame;
  frame->has_pyframe = 1;
}

static inline void _PyShadowFrame_Pop(PyThreadState *tstate,
                                      _PyShadowFrame *frame) {
  assert(tstate->shadow_frame == frame);
  tstate->shadow_frame = frame->prev;
}

#ifdef __cplusplus
}
#endif

#endif /* Py_LIMITED_API */
#endif /* !Py_SHADOW_FRAME_H */
