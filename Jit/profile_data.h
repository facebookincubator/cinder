#pragma once

#include "Python.h"

#include <cstdint>

namespace jit {

// Return a crc32 checksum of the bytecode for the given code object.
uint32_t hashBytecode(PyCodeObject* code);

// Load serialize profile data from the given file, returning true on success.
//
// Binary format is defined in Jit/profile_data_format.txt
bool loadProfileData(const char* filename);

} // namespace jit
