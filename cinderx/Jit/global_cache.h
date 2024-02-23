// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"
#include "cinderx/Common/ref.h"
#include "cinderx/Common/util.h"

#include "cinderx/Jit/slab_arena.h"
#include "cinderx/Jit/threaded_compile.h"

#include <set>
#include <unordered_map>

namespace jit {

struct GlobalCacheKey {
  // builtins and globals are weak references; the invalidation code is
  // responsible for erasing any relevant keys when a dict is freed.
  BorrowedRef<PyDictObject> builtins;
  BorrowedRef<PyDictObject> globals;
  Ref<PyUnicodeObject> name;

  GlobalCacheKey(PyObject* builtins, PyObject* globals, PyObject* name)
      : builtins(builtins), globals(globals) {
    ThreadedCompileSerialize guard;
    this->name = Ref<>::create(name);
  }

  ~GlobalCacheKey() {
    ThreadedCompileSerialize guard;
    name.reset();
  }

  bool operator==(const GlobalCacheKey& other) const {
    return builtins == other.builtins && globals == other.globals &&
        name == other.name;
  }
};

struct GlobalCacheKeyHash {
  std::size_t operator()(const GlobalCacheKey& key) const {
    std::hash<PyObject*> hasher;
    return combineHash(
        hasher(key.builtins), hasher(key.globals), hasher(key.name));
  }
};

using GlobalCacheMap =
    std::unordered_map<GlobalCacheKey, PyObject**, GlobalCacheKeyHash>;

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
    return pair_->second;
  }

  // Set the global cache pointer.
  void init(PyObject** cache) const;

  // Clear the cache's value. Unsubscribing from any watched dicts is left to
  // the caller since it can involve complicated dances with iterators.
  void clear();

  bool operator<(const GlobalCache& other) const {
    return pair_ < other.pair_;
  }

 private:
  GlobalCacheMap::value_type* pair_;
};

// Manages all memory and data structures for global cache values.
class GlobalCacheManager {
 public:
  // Create or look up a cache for the global with the given name, in the
  // context of the given globals and builtins dicts.  The cache will fall back
  // to builtins if the value isn't defined in the globals dict.
  GlobalCache findGlobalCache(
      BorrowedRef<PyDictObject> builtins,
      BorrowedRef<PyDictObject> globals,
      BorrowedRef<PyUnicodeObject> key);

  // Called when the value at a key is modified (value will contain the new
  // value) or deleted (value will be nullptr).
  void notifyDictUpdate(
      BorrowedRef<PyDictObject> dict,
      BorrowedRef<PyUnicodeObject> key,
      BorrowedRef<> value);

  // Called when a dict is cleared, rather than sending individual notifications
  // for every key. The dict is still in a watched state, and further callbacks
  // for it will be invoked as appropriate.
  void notifyDictClear(BorrowedRef<PyDictObject> dict);

  // Called when a dict has changed in a way that is incompatible with watching,
  // or is about to be freed.  No more callbacks will be invoked for this dict.
  void notifyDictUnwatch(BorrowedRef<PyDictObject> dict);

  // Clear internal caches for global values.  This may cause a degradation of
  // performance and is intended for detecting memory leaks and general cleanup.
  void clear();

 private:
  // Check if a given key of a dict is watched by the given cache.
  bool isWatchedDictKey(
      BorrowedRef<PyDictObject> dict,
      BorrowedRef<PyUnicodeObject> key,
      GlobalCache cache);

  // Watch the given key of the given dict. updateCache() will be called when
  // the key's value in the dict is changed or removed.  disableCache() will be
  // called if the dict becomes unwatchable.
  void watchDictKey(
      BorrowedRef<PyDictObject> dict,
      BorrowedRef<PyUnicodeObject> key,
      GlobalCache cache);

  // Unsubscribe from the given key of the given dict.
  void unwatchDictKey(
      BorrowedRef<PyDictObject> dict,
      BorrowedRef<PyUnicodeObject> key,
      GlobalCache cache);

  // Initialize a global value cache. Subscribe to both globals and builtins
  // dicts and fill in the current value.
  void initCache(GlobalCache cache);

  // Update the cached value after an update to one of the dicts.
  //
  // Return true iff the cache should be disabled because its builtins dict is
  // unwatchable and the value has been deleted from the globals dict.  The
  // caller is responsible for safely disabling any such caches.
  [[nodiscard]] bool updateCache(
      GlobalCache cache,
      BorrowedRef<PyDictObject> dict,
      BorrowedRef<> new_value);

  // Forget given cache(s). Note that for now, this only removes bookkeeping,
  // each cache is not freed from the arena and may still be reachable from
  // compiled code.
  void disableCaches(const std::vector<GlobalCache>& caches);
  void disableCache(GlobalCache cache);

  // Arena where all the global value caches are allocated.
  SlabArena<PyObject*> arena_;

  // Map of all global value caches, keyed by (globals, builtins, name).
  GlobalCacheMap map_;

  // Two-level map keeping track of which global value caches are subscribed to
  // which keys in which dicts.
  std::unordered_map<
      BorrowedRef<PyDictObject>,
      std::unordered_map<BorrowedRef<PyUnicodeObject>, std::set<GlobalCache>>>
      watch_map_;
};

} // namespace jit
