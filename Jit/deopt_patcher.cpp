// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/deopt_patcher.h"

#include "Jit/log.h"
#include "Jit/util.h"

#include <cstring>

namespace jit {

namespace {

// Size of the jump that will be written to the patchpoint
constexpr int kJmpSize = 5;

} // namespace

void DeoptPatcher::patch() {
  JIT_CHECK(patchpoint_ != nullptr, "not linked!");
  // 32 bit relative jump - https://www.felixcloutier.com/x86/jmp
  patchpoint_[0] = 0xe9;
  std::memcpy(patchpoint_ + 1, &jmp_disp_, sizeof(jmp_disp_));
}

void DeoptPatcher::link(uintptr_t patchpoint, uintptr_t deopt_exit) {
  JIT_CHECK(patchpoint_ == nullptr, "already linked!");
  JIT_CHECK(
      fitsInt32(deopt_exit - (patchpoint + kJmpSize)),
      "can't encode jump as relative");
  init();
  jmp_disp_ = deopt_exit - (patchpoint + kJmpSize);
  patchpoint_ = reinterpret_cast<uint8_t*>(patchpoint);
}

void DeoptPatcher::emitPatchpoint(asmjit::x86::Builder& as) {
  // 5-byte nop - https://www.felixcloutier.com/x86/nop
  //
  // Asmjit supports multi-byte nops but for whatever reason I can't get it to
  // emit the 5-byte version.
  as.db(0x0f);
  as.db(0x1f);
  as.db(0x44);
  as.db(0x00);
  as.db(0x00);
}

} // namespace jit
