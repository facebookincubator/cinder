// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/bytecode.h"

#include "opcode.h"

#include <unordered_set>

namespace jit {

// these must be opcodes whose oparg is a jump target index
const std::unordered_set<int> kBranchOpcodes = {
    FOR_ITER,
    JUMP_ABSOLUTE,
    JUMP_FORWARD,
    JUMP_IF_FALSE_OR_POP,
    JUMP_IF_NONZERO_OR_POP,
    JUMP_IF_TRUE_OR_POP,
    JUMP_IF_ZERO_OR_POP,
    JUMP_IF_NOT_EXC_MATCH,
    POP_JUMP_IF_FALSE,
    POP_JUMP_IF_TRUE,
    POP_JUMP_IF_ZERO,
    POP_JUMP_IF_NONZERO,
};

const std::unordered_set<int> kRelBranchOpcodes = {
    FOR_ITER,
    JUMP_FORWARD,
    SETUP_FINALLY,
};

// we always consider branches block terminators; no need to duplicate them here
const std::unordered_set<int> kBlockTerminatorOpcodes = {
    RETURN_VALUE,
    RETURN_PRIMITIVE,
    RAISE_VARARGS,
    RERAISE,
};

} // namespace jit
