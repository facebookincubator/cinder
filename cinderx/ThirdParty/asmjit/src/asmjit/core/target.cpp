// This file is part of AsmJit project <https://asmjit.com>
//
// See asmjit.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include "cinderx/ThirdParty/asmjit/src/asmjit/core/api-build_p.h"
#include "cinderx/ThirdParty/asmjit/src/asmjit/core/target.h"

ASMJIT_BEGIN_NAMESPACE

Target::Target() noexcept
  : _environment{},
    _cpuFeatures{} {}
Target::~Target() noexcept {}

ASMJIT_END_NAMESPACE
