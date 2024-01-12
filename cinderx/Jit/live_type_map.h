// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/containers.h"
#include "Jit/ref.h"

#include <string>

namespace jit {

// LiveTypeMap maintains a mapping from type name to PyTypeObject* to resolve
// names from offline profile data. It also supports erasing elements by
// PyTypeObject*. This is to handle the fact that a type's name may change
// after creation, so typeFullname(type) is not constant for a given type
// (assignment to ty.__name__ or ty.__module__ are the two big ones).
//
// Rather than delicately hooking every situation that could change a type's
// name, we assume (for now) that any types with names that do change after
// creation aren't relevant to the code we want to type-specialize and simply
// erase destroyed types by pointer and not name.
struct LiveTypeMap {
  // Look up a PyTypeObject for the given name, returning nullptr if none
  // exists.
  BorrowedRef<PyTypeObject> get(const std::string& name) const;

  // Return the number of items in the map.
  size_t size() const;

  // Insert the given type into the map, keyed by typeFullname(type).
  void insert(BorrowedRef<PyTypeObject> type);

  // Mark that the given type has split dict keys primed from profile data.
  void setPrimedDictKeys(BorrowedRef<PyTypeObject> type);

  // Check whether the given type has split dict keys primed from profile data.
  bool hasPrimedDictKeys(BorrowedRef<PyTypeObject> type) const;

  // Erase the given type from the map, if it's present.
  void erase(BorrowedRef<PyTypeObject> type);

  void clear();

 private:
  UnorderedMap<std::string, BorrowedRef<PyTypeObject>> name_to_type_;
  UnorderedMap<BorrowedRef<PyTypeObject>, std::string> type_to_name_;

  UnorderedSet<BorrowedRef<PyTypeObject>> primed_dict_keys_;
};

} // namespace jit
