// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/codegen/gen_asm_utils.h"

#include "Jit/codegen/environ.h"

namespace jit {
namespace codegen {

namespace {

void recordIPtoBCMapping(Environ& env, int bc_off) {
  asmjit::Label label = env.as->newLabel();
  env.as->bind(label);
  env.pending_ip_to_bc_offs.emplace_back(label, bc_off);
}

} // namespace

void emitCall(Environ& env, asmjit::Label label, int bc_off) {
  env.as->call(label);
  recordIPtoBCMapping(env, bc_off);
}

void emitCall(Environ& env, uint64_t func, const jit::lir::Instruction* instr) {
  env.as->call(func);
  const jit::hir::Instr* origin = instr->origin();
  if (origin == nullptr) {
    // Origin might be null if we've parsed the LIR.
    return;
  }
  recordIPtoBCMapping(env, origin->bytecodeOffset());
}

} // namespace codegen
} // namespace jit
