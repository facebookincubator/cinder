// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __FRAME_H__
#define __FRAME_H__

#include "Python.h"
#include "frameobject.h"

// this struct is for JIT use only.
// As JIT functions does not rely on PyFrameObject, the lightweight
// and fixed-size TinyFrame is used instead in order to save alloc/dealloc
// overhead. TinyFrame objects link together with the frames currently on stack
// as a normal PyFrameObject to preserve the order of the stack. Once
// certain operations do need the information on a regular frame, such as
// stack traceback, sys.getframe, etc, we patch(materialize) parts or the whole
// frame stack by replacing TinyFrame with regular PyFrameObject. TinyFrame
// struct should only save information that is needed to materialize a
// PyFrameObject.
typedef struct _TinyFrame {
  PyObject_HEAD;

  PyThreadState* tstate;
  PyCodeObject* code;
  PyObject* globals;
  PyObject* gen;

  union {
    PyFrameObject* f_back;
    struct _TinyFrame* tf_back;
  } t;
} TinyFrame;

PyAPI_DATA(PyTypeObject) PyTinyFrame_Type;

#define PyTinyFrame_Check(op) (Py_TYPE(op) == &PyTinyFrame_Type)

#ifdef __cplusplus
extern "C" {
#endif

// Check if the frame is a tiny frame
int JIT_IsTinyFrame(void* frame);

// allocate a tiny frame
TinyFrame* JIT_TinyFrameAllocate(
    PyThreadState* tstate,
    PyCodeObject* code,
    PyObject* globals);

// deallocate a tiny frame
void JIT_TinyFrameDeallocate(TinyFrame* frame);

// materilize all the frames in the frame stack up to the provided frame.
// the argument frame is the frame before the frame stack is materialized.
// this function returns an after-materialized frame corresponding to the input
// frame. specially,
//   - If the frame is a regular PyFrameObject, return the same frame.
//   - If the frame is a TinyFrame object, return the materialized frame.
//   - If the frame is not in the frame stack, return the same frame.
PyFrameObject* JIT_MaterializeToFrame(
    PyThreadState* tstate,
    PyFrameObject* frame);

// Materializes the top-most frame.
PyFrameObject* JIT_MaterializeTopFrame(PyThreadState* tstate);

// Materializes the frame before this frame.  The passed in frame
// must already be materialized.  Typically this is done via
// calling JIT_MaterializeTopFrame and then walking the frame
// chain and materializing backwards as necessary.
// Returns the materialized f_back frame or the existing already
// materialized frame.
PyFrameObject* JIT_MaterializePrevFrame(PyFrameObject* cur);

#ifdef __cplusplus
}
#endif

#endif
