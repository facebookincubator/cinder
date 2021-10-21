#pragma once

#include "Python.h"

#include <cstdint>

namespace jit {

// Return a crc32 checksum of the bytecode for the given code object.
uint32_t hashBytecode(PyCodeObject* code);

} // namespace jit
