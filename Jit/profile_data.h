#pragma once

#include "Python.h"

#include "Jit/runtime.h"

#include <cstdint>
#include <istream>
#include <ostream>

namespace jit {

// Return a crc32 checksum of the bytecode for the given code object.
uint32_t hashBytecode(PyCodeObject* code);

// Load serialized profile data from the given filename or stream, returning
// true on success.
//
// Binary format is defined in Jit/profile_data_format.txt
bool readProfileData(const std::string& filename);
bool readProfileData(std::istream& stream);

// Write profile data from the current process to the given filename or stream,
// returning true on success.
bool writeProfileData(const std::string& filename);
bool writeProfileData(std::ostream& stream);

// Clear any loaded profile data.
void clearProfileData();

// Map from bytecode offset within a code object to vector of string type
// names.
using CodeProfileData = UnorderedMap<BytecodeOffset, std::vector<std::string>>;

// Look up the profile data for the given code object, returning nullptr if
// there is none.
const CodeProfileData* getProfileData(PyCodeObject* code);

// Return a list types materialized from a CodeProfileData and a
// BytecodeOffset. The result will be empty if there's no data for bc_off.
std::vector<BorrowedRef<PyTypeObject>> getProfiledTypes(
    const CodeProfileData& data,
    BytecodeOffset bc_off);

// A CodeKey is an opaque value that uniquely identifies a specific code
// object. It may include information about the name, file path, and contents
// of the code object.
using CodeKey = std::string;

// Return the code key for the given code object.
CodeKey codeKey(PyCodeObject* code);

// Return the qualname of the given code object, falling back to its name or
// "<unknown>" if not set.
std::string codeQualname(PyCodeObject* code);

// Inform the profiling code that a type has been created.
void registerProfiledType(PyTypeObject* type);

// Inform the profiling code that a type is about to be destroyed.
void unregisterProfiledType(PyTypeObject* type);

} // namespace jit
