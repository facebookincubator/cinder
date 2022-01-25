// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include "Jit/hir/hir.h"
#include "Jit/lir/instruction.h"

#include <asmjit/asmjit.h>

namespace jit {
namespace codegen {

struct Environ;

// Set RBP to "original RBP" value when called in the context of a generator.
void RestoreOriginalGeneratorRBP(asmjit::x86::Emitter* as);

// Emit a call and record that the program point following the call corresponds
// to the given bytecode offset (or the bytecode offset from which instr was
// lowered).
//
// Use this when emitting calls from custom actions. This will update the JIT's
// internal metadata so that the location in the generated code can be mapped
// back to the bytecode instruction that produced it.
void emitCall(Environ& env, asmjit::Label label, int bc_off);
void emitCall(Environ& env, uint64_t func, const jit::lir::Instruction* instr);

} // namespace codegen
} // namespace jit
