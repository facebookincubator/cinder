// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "frameobject.h"
#include "internal/pycore_shadow_frame_struct.h"

#include "Jit/ref.h"

namespace jit {

class CodeRuntime;

// FrameHeader lives at the beginning of the stack frame for JIT-compiled
// functions. Note these will be garbage in generator objects.
struct FrameHeader {
  JITShadowFrame shadow_frame;
};

// Materialize all the Python frames for the shadow stack associated with
// tstate.
//
// Returns a borrowed reference to top of the Python stack (tstate->frame).
BorrowedRef<PyFrameObject> materializeShadowCallStack(PyThreadState* tstate);

// Materialize a Python frame for the top-most frame for tstate, with the
// expectation that this frame will immediately either be unwound or resumed in
// the interpreter.
//
// NB: This returns a stolen reference to the frame. The caller is responsible
// for ensuring that the frame is unlinked and the reference is destroyed.
Ref<PyFrameObject> materializePyFrameForDeopt(PyThreadState* tstate);

// Materialize a Python frame for gen.
//
// This returns nullptr if gen is completed or a borrowed reference to its
// PyFrameObject otherwise.
BorrowedRef<PyFrameObject> materializePyFrameForGen(
    PyThreadState* tstate,
    PyGenObject* gen);

void assertShadowCallStackConsistent(PyThreadState* tstate);

} // namespace jit
