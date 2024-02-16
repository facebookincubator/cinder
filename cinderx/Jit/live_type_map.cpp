// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/Jit/live_type_map.h"

#include "cinderx/Common/log.h"
#include "cinderx/Common/util.h"

namespace jit {

BorrowedRef<PyTypeObject> LiveTypeMap::get(const std::string& name) const {
  return map_get(name_to_type_, name, nullptr);
}

size_t LiveTypeMap::size() const {
  JIT_DCHECK(
      name_to_type_.size() == type_to_name_.size(),
      "Expected maps to be same size, got {} and {}",
      name_to_type_.size(),
      type_to_name_.size());

  return name_to_type_.size();
}

void LiveTypeMap::insert(BorrowedRef<PyTypeObject> type) {
  std::string name = typeFullname(type);
  if (name.empty()) {
    return;
  }
  auto pair = name_to_type_.emplace(name, type);
  if (!pair.second) {
    // Another type with the same name already exists. This should be rare
    // and our profiling has no way to account for it, so let the newer type
    // win out on the assumption that it's the one to stick around.
    type_to_name_.erase(pair.first->second);
    pair.first->second = type;
  }
  auto pair2 = type_to_name_.emplace(type, name);
  JIT_DCHECK(
      !pair.second || pair2.second,
      "Name and type maps are inconsistent when inserting name {}",
      name);
}

void LiveTypeMap::setPrimedDictKeys(BorrowedRef<PyTypeObject> type) {
  JIT_DCHECK(
      type_to_name_.count(type) != 0,
      "Attempt to set primed dict keys on type that isn't tracked as live");
  primed_dict_keys_.insert(type);
}

bool LiveTypeMap::hasPrimedDictKeys(BorrowedRef<PyTypeObject> type) const {
  return primed_dict_keys_.count(type) != 0;
}

void LiveTypeMap::erase(BorrowedRef<PyTypeObject> type) {
  primed_dict_keys_.erase(type);
  auto it = type_to_name_.find(type);
  if (it == type_to_name_.end()) {
    return;
  }
  JIT_DCHECK(
      map_get(name_to_type_, it->second) == type, "Inconsistent map state");
  name_to_type_.erase(it->second);
  type_to_name_.erase(it);
}

void LiveTypeMap::clear() {
  // Only erase heap types from the maps: static types aren't torn down
  // during Py_Finalize(). This means they're never reinitialized and we
  // wouldn't be notified about their (re-)creation.
  JIT_DCHECK(
      name_to_type_.size() == type_to_name_.size(), "Maps should be same size");
  for (auto it = type_to_name_.begin(); it != type_to_name_.end();) {
    if (PyType_HasFeature(it->first, Py_TPFLAGS_HEAPTYPE)) {
      name_to_type_.erase(it->second);
      it = type_to_name_.erase(it);
    } else {
      ++it;
    }
  }
  JIT_DCHECK(
      name_to_type_.size() == type_to_name_.size(), "Maps should be same size");
}

} // namespace jit
