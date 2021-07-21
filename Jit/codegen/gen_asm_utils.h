// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/hir/hir.h"
#include "asmjit/asmjit.h"

#include "Python.h"

namespace jit {
namespace codegen {

// Set RBP to "original RBP" value when called in the context of a generator.
void RestoreOriginalGeneratorRBP(asmjit::x86::Emitter* as);


} // namespace codegen
} // namespace jit
