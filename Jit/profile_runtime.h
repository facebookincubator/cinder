// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/bytecode_offsets.h"
#include "Jit/containers.h"
#include "Jit/hir/type.h"
#include "Jit/ref.h"
#include "Jit/type_profiler.h"
#include "Jit/util.h"

#include <iosfwd>
#include <regex>

namespace jit {

// Profiling information for a PyCodeObject. Includes the total number of
// bytecodes executed and type profiles for certain opcodes, keyed by bytecode
// offset.
struct CodeProfile {
  UnorderedMap<BCOffset, std::unique_ptr<TypeProfiler>> typed_hits;
  int64_t total_hits{0};
};

class ProfileRuntime {
 public:
  using ProfileMap = std::unordered_map<Ref<PyCodeObject>, CodeProfile>;

  using iterator = ProfileMap::iterator;
  using const_iterator = ProfileMap::const_iterator;

  ProfileRuntime() = default;

  // For a given code object and bytecode offset, get the types that the runtime
  // believes are the most likely to show up.  Will be empty if there is not
  // enough profiling data, or if the bytecode is polymorphic.
  //
  // Types that do not have enough profiling information will be returned as
  // TTop.
  std::vector<hir::Type> getProfiledTypes(
      BorrowedRef<PyCodeObject> code,
      BCOffset bc_off) const;

  // Record a type profile for an instruction and its current Python stack.
  void profileInstr(
      BorrowedRef<PyFrameObject> frame,
      PyObject** stack_top,
      int opcode,
      int oparg);

  // Record profiled instructions for the given code object upon exit from a
  // frame, some of which may not have had their types recorded.
  void countProfiledInstrs(BorrowedRef<PyCodeObject> code, Py_ssize_t count);

  // Check whether the given type has split dict keys primed from profile data,
  // which implies that they are unlikely to change at runtime.
  bool hasPrimedDictKeys(BorrowedRef<PyTypeObject> type) const;

  // Get the number of cached split dict keys in the given type.
  int numCachedKeys(BorrowedRef<PyTypeObject> type) const;

  // Call `callback' 0 or more times, once for each split dict key in the given
  // type.
  void enumerateCachedKeys(
      BorrowedRef<PyTypeObject> type,
      std::function<void(BorrowedRef<>)> callback) const;

  // Inform the profiling code that a type has been created.
  void registerType(BorrowedRef<PyTypeObject> type);

  // Inform the profiling code that a type is about to be destroyed.
  void unregisterType(BorrowedRef<PyTypeObject> type);

  // Set a regex to strip file paths when computing code keys.
  void setStripPattern(std::regex pattern);

  // Write profile data from the current process to the given filename or
  // stream, returning true on success.
  bool serialize(const std::string& filename) const;
  bool serialize(std::ostream& stream) const;

  // Load serialized profile data from the given filename or stream, returning
  // true on success.
  //
  // This will prevent the runtime from profiling instructions further.  The
  // loaded profile is expected to be kept the same despite further Python
  // execution.
  //
  // Binary format is defined in Jit/profile_data_format.txt
  bool deserialize(const std::string& filename);
  bool deserialize(std::istream& stream);

  // Clear any loaded or collected profile data.
  void clear();

  // Get an iterator range through all profile data.
  iterator begin();
  iterator end();
  const_iterator begin() const;
  const_iterator end() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProfileRuntime);

  // Profiles captured while executing code.
  ProfileMap profiles_;
  bool can_profile_{true};
};

} // namespace jit
