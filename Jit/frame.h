// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __FRAME_H__
#define __FRAME_H__

#include "Python.h"
#include "frameobject.h"

#include "Jit/ref.h"

namespace jit {

class CodeRuntime;

// Materialize all the Python frames for the shadow stack associated with tstate
//
// Returns a borrowed reference to top of the Python stack (tstate->frame).
BorrowedRef<PyFrameObject> materializeShadowCallStack(PyThreadState* tstate);

// Materialize a Python frame for the JIT frame starting at base, with the
// expectation that this frame will immediately either be unwound or resumed in
// the interpreter.
//
// NB: This returns a stolen reference to the frame. The caller is responsible
// for ensuring that the frame is unlinked and the reference is destroyed.
Ref<PyFrameObject> materializePyFrameForDeopt(
    PyThreadState* tstate,
    void** base);

// Find the shadow frame associated with the running generator gen and
// materialize a Python frame for it.
//
// Returns a borrowed reference to the materialized frame.
BorrowedRef<PyFrameObject> materializePyFrameForGen(
    PyThreadState* tstate,
    PyGenObject* gen);

// Unlink the shadow frame for the JIT frame starting at base
void unlinkShadowFrame(
    PyThreadState* tstate,
    void** base,
    CodeRuntime& code_rt);

} // namespace jit

#endif // !__FRAME_H__
