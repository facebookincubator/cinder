// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/ref.h"
#include "Jit/util.h"

#include <unordered_map>

namespace jit {

struct GlobalCacheKey {
  // builtins and globals are weak references; the invalidation code is
  // responsible for erasing any relevant keys when a dict is freed.
  PyObject* builtins;
  PyObject* globals;
  Ref<PyObject> name;

  GlobalCacheKey(PyObject* builtins, PyObject* globals, PyObject* name)
      : builtins(builtins), globals(globals) {
    ThreadedCompileSerialize guard;
    this->name = Ref<>::create(name);
  }

  bool operator==(const GlobalCacheKey& other) const {
    return builtins == other.builtins && globals == other.globals &&
        name == other.name;
  }
};

struct GlobalCacheKeyHash {
  std::size_t operator()(const GlobalCacheKey& key) const {
    std::hash<PyObject*> hasher;
    std::size_t hash = combineHash(hasher(key.builtins), hasher(key.globals));
    return combineHash(hash, hasher(key.name));
  }
};

struct GlobalCacheValue {
  PyObject** ptr;
};

using GlobalCacheMap =
    std::unordered_map<GlobalCacheKey, GlobalCacheValue, GlobalCacheKeyHash>;

// Functions to initialize, update, and disable a global cache. The actual
// cache lives in a GlobalCacheMap, so this is a thin wrapper around a pointer
// to that data.
class GlobalCache {
 public:
  GlobalCache(GlobalCacheMap::value_type* pair) : pair_(pair) {}

  const GlobalCacheKey& key() const {
    return pair_->first;
  }

  PyObject** valuePtr() const {
    return pair_->second.ptr;
  }

  // Initialize the cache: subscribe to both dicts and fill in the current
  // value.
  void init(PyObject** cache) const;

  // Update the cached value after an update to one of the dicts.
  //
  // to_disable collects caches that must be disabled because their builtins
  // dict is unwatchable and the value has been deleted from the globals
  // dict. The caller is responsible for safely disabling any caches in this
  // list.
  void update(
      PyObject* dict,
      PyObject* new_value,
      std::vector<GlobalCache>& to_disable) const;

  // Disable the cache by clearing out its value. Unsubscribing from any
  // watched dicts is left to the caller since it can involve complicated
  // dances with iterators.
  void disable() const;

  bool operator<(const GlobalCache& other) const {
    return pair_ < other.pair_;
  }

 private:
  GlobalCacheMap::value_type* pair_;
};

} // namespace jit
