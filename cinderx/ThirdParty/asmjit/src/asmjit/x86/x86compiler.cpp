// This file is part of AsmJit project <https://asmjit.com>
//
// See asmjit.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include "cinderx/ThirdParty/asmjit/src/asmjit/core/api-build_p.h"
#if !defined(ASMJIT_NO_X86) && !defined(ASMJIT_NO_COMPILER)

#include "cinderx/ThirdParty/asmjit/src/asmjit/x86/x86assembler.h"
#include "cinderx/ThirdParty/asmjit/src/asmjit/x86/x86compiler.h"
#include "cinderx/ThirdParty/asmjit/src/asmjit/x86/x86instapi_p.h"
#include "cinderx/ThirdParty/asmjit/src/asmjit/x86/x86rapass_p.h"

ASMJIT_BEGIN_SUB_NAMESPACE(x86)

// x86::Compiler - Construction & Destruction
// ==========================================

Compiler::Compiler(CodeHolder* code) noexcept : BaseCompiler() {
  _archMask = (uint64_t(1) << uint32_t(Arch::kX86)) |
              (uint64_t(1) << uint32_t(Arch::kX64)) ;
  assignEmitterFuncs(this);

  if (code)
    code->attach(this);
}
Compiler::~Compiler() noexcept {}

// x86::Compiler - Events
// ======================

Error Compiler::onAttach(CodeHolder* code) noexcept {
  ASMJIT_PROPAGATE(Base::onAttach(code));
  Error err = addPassT<X86RAPass>();

  if (ASMJIT_UNLIKELY(err)) {
    onDetach(code);
    return err;
  }

  return kErrorOk;
}

Error Compiler::onDetach(CodeHolder* code) noexcept {
  return Base::onDetach(code);
}

// x86::Compiler - Finalize
// ========================

Error Compiler::finalize() {
  ASMJIT_PROPAGATE(runPasses());
  Assembler a(_code);
  a.addEncodingOptions(encodingOptions());
  a.addDiagnosticOptions(diagnosticOptions());
  return serializeTo(&a);
}

ASMJIT_END_SUB_NAMESPACE

#endif // !ASMJIT_NO_X86 && !ASMJIT_NO_COMPILER
