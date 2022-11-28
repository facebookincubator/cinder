// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/lir/function.h"

namespace jit::lir {

// Verifies the following properties of a LIR function:
//
// - Each block has branches to all successors unless a successor is the next
// block
//   in the code layout post register allocation.
//
// Returns true if the function passes all LIR invariants we wish to uphold post
// register allocation.
bool verifyPostRegAllocInvariants(Function* func, std::ostream& err);

} // namespace jit::lir
