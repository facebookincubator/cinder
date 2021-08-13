// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/bytecode.h"

#include "opcode.h"

#include <unordered_set>

namespace jit {

// TODO(mpage): Flesh this out
const std::unordered_set<int> kBranchOpcodes = {
    CALL_FINALLY,
    FOR_ITER,
    JUMP_ABSOLUTE,
    JUMP_FORWARD,
    JUMP_IF_FALSE_OR_POP,
    JUMP_IF_NONZERO_OR_POP,
    JUMP_IF_TRUE_OR_POP,
    JUMP_IF_ZERO_OR_POP,
    POP_JUMP_IF_FALSE,
    POP_JUMP_IF_TRUE,
    POP_JUMP_IF_ZERO,
    POP_JUMP_IF_NONZERO,
};

const std::unordered_set<int> kRelBranchOpcodes = {
    CALL_FINALLY,
    FOR_ITER,
    JUMP_FORWARD,
    SETUP_FINALLY,
};

} // namespace jit
