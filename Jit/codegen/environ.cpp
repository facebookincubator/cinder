// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "Jit/codegen/environ.h"

#include "Jit/hir/hir.h"

namespace jit {
namespace codegen {

void Environ::addIPToBCMapping(
    asmjit::Label label,
    const jit::lir::Instruction* instr) {
  // TODO(emacs): Support fetching code object and line number for inlined
  // frames in the JIT.
  return;
  const jit::hir::Instr* origin = instr->origin();
  if (origin == nullptr) {
    // Origin might be null if we've parsed the LIR.
    return;
  }
  pending_ip_to_bc_offs.emplace_back(label, origin->bytecodeOffset());
}

} // namespace codegen
} // namespace jit
