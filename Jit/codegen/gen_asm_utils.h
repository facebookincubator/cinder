// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/hir/hir.h"
#include "asmjit/asmjit.h"

#include "Python.h"

namespace jit {
namespace codegen {

// Set RBP to "original RBP" value when called in the context of a generator.
void RestoreOriginalGeneratorRBP(asmjit::x86::Emitter* as);

// Generate code to unlink the current frame.
void EmitEpilogueUnlinkFrame(
    asmjit::x86::Builder* as,
    asmjit::x86::Gp tstate_r,
    void (*unlink_frame_func)(PyThreadState*),
    void (*unlink_tiny_frame_func)(PyThreadState*),
    jit::hir::FrameMode frameMode);

} // namespace codegen
} // namespace jit
