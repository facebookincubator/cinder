// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/Jit/global_cache.h"

#include "cinderx/Common/watchers.h"

namespace jit {

void GlobalCache::init(PyObject** cache) const {
  pair_->second = cache;
}

void GlobalCache::clear() {
  *valuePtr() = nullptr;
}

GlobalCache GlobalCacheManager::findGlobalCache(
    BorrowedRef<PyDictObject> builtins,
    BorrowedRef<PyDictObject> globals,
    BorrowedRef<PyUnicodeObject> key) {
  JIT_CHECK(PyUnicode_CheckExact(key), "Key must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(key.get()), "Key must be interned");
  auto result = map_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(builtins, globals, key),
      std::forward_as_tuple());
  GlobalCache cache(&*result.first);
  if (result.second) {
    initCache(cache);
  }
  return cache;
}

void GlobalCacheManager::notifyDictUpdate(
    BorrowedRef<PyDictObject> dict,
    BorrowedRef<PyUnicodeObject> key,
    BorrowedRef<> value) {
  JIT_CHECK(PyUnicode_CheckExact(key), "key must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(key.get()), "Key must be interned");

  auto dict_it = watch_map_.find(dict);
  // Something else in Cinderx could be watching this dict. Return early if no
  // matchers were registered.
  if (dict_it == watch_map_.end()) {
    return;
  }
  auto key_it = dict_it->second.find(key);
  if (key_it == dict_it->second.end()) {
    return;
  }
  std::vector<GlobalCache> to_disable;
  for (GlobalCache cache : key_it->second) {
    if (updateCache(cache, dict, value)) {
      to_disable.emplace_back(cache);
    }
  }
  disableCaches(to_disable);
}

void GlobalCacheManager::notifyDictClear(BorrowedRef<PyDictObject> dict) {
  auto dict_it = watch_map_.find(dict);
  // Something else in Cinderx could be watching this dict. Return early if no
  // matchers were registered.
  if (dict_it == watch_map_.end()) {
    return;
  }
  std::vector<GlobalCache> to_disable;
  for (auto& key_pair : dict_it->second) {
    for (GlobalCache cache : key_pair.second) {
      if (updateCache(cache, dict, nullptr)) {
        to_disable.emplace_back(cache);
      }
    }
  }
  disableCaches(to_disable);
}

void GlobalCacheManager::notifyDictUnwatch(BorrowedRef<PyDictObject> dict) {
  auto dict_it = watch_map_.find(dict);
  // Something else in Cinderx could be watching this dict. Return early if no
  // matchers were registered.
  if (dict_it == watch_map_.end()) {
    return;
  }
  for (auto& pair : dict_it->second) {
    for (auto cache : pair.second) {
      // Unsubscribe from the corresponding globals/builtins dict if needed.
      PyObject* globals = cache.key().globals;
      PyObject* builtins = cache.key().builtins;
      if (globals != builtins) {
        if (dict == globals) {
          // when shutting down builtins goes away and we won't be
          // watching builtins if the value we are watching was defined
          // globally at the module level but was never deleted.
          if (isWatchedDictKey(builtins, cache.key().name, cache)) {
            unwatchDictKey(builtins, cache.key().name, cache);
          }
        } else {
          unwatchDictKey(globals, cache.key().name, cache);
        }
      }

      disableCache(cache);
    }
  }
  watch_map_.erase(dict_it);
}

void GlobalCacheManager::clear() {
  std::vector<PyObject*> keys;
  for (auto& pair : watch_map_) {
    keys.push_back(pair.first);
  }
  for (auto dict : keys) {
    auto dict_it = watch_map_.find(dict);
    // notifyDictUnwatch may clear out our dictionary and builtins,
    // so we need to make sure each dictionary is still being watched
    if (dict_it != watch_map_.end()) {
      notifyDictUnwatch(dict);
      Ci_Watchers_UnwatchDict(dict);
    }
  }
}

bool GlobalCacheManager::isWatchedDictKey(
    BorrowedRef<PyDictObject> dict,
    BorrowedRef<PyUnicodeObject> key,
    GlobalCache cache) {
  auto dict_it = watch_map_.find(dict);
  if (dict_it == watch_map_.end()) {
    return false;
  }
  auto& dict_keys = dict_it->second;
  auto key_it = dict_keys.find(key);
  if (key_it != dict_keys.end()) {
    return key_it->second.count(cache);
  }
  return false;
}

void GlobalCacheManager::watchDictKey(
    BorrowedRef<PyDictObject> dict,
    BorrowedRef<PyUnicodeObject> key,
    GlobalCache cache) {
  JIT_CHECK(PyUnicode_CheckExact(key), "key must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(key.get()), "key must be interned");
  auto& watchers = watch_map_[dict][key];
  bool inserted = watchers.emplace(cache).second;
  JIT_CHECK(inserted, "cache was already watching key");
  Ci_Watchers_WatchDict(dict);
}

void GlobalCacheManager::unwatchDictKey(
    BorrowedRef<PyDictObject> dict,
    BorrowedRef<PyUnicodeObject> key,
    GlobalCache cache) {
  auto dict_it = watch_map_.find(dict);
  JIT_CHECK(dict_it != watch_map_.end(), "dict has no watchers");
  auto& dict_keys = dict_it->second;
  auto key_it = dict_keys.find(key);
  JIT_CHECK(key_it != dict_it->second.end(), "key has no watchers");
  auto& key_watchers = key_it->second;
  auto cache_it = key_watchers.find(cache);
  JIT_CHECK(cache_it != key_watchers.end(), "cache was not watching key");
  key_watchers.erase(cache_it);
  if (key_watchers.empty()) {
    dict_keys.erase(key_it);
    if (dict_keys.empty()) {
      watch_map_.erase(dict_it);
      Ci_Watchers_UnwatchDict(dict);
    }
  }
}

void GlobalCacheManager::initCache(GlobalCache cache) {
  cache.init(arena_.allocate());

  BorrowedRef<PyDictObject> globals = cache.key().globals;
  BorrowedRef<PyDictObject> builtins = cache.key().builtins;
  BorrowedRef<PyUnicodeObject> key = cache.key().name;

  JIT_DCHECK(
      _PyDict_HasOnlyUnicodeKeys(globals),
      "Should have already checked that globals dict was watchable");

  // We want to try and only watch builtins if this is really a builtin.  So we
  // will start only watching globals, and if the value gets deleted from
  // globals then we'll start tracking builtins as well.  Once we start tracking
  // builtins we'll never stop rather than trying to handle all of the
  // transitions.
  watchDictKey(globals, key, cache);

  // We don't need to immediately watch builtins if the value is found in
  // globals.
  if (PyObject* globals_value = PyDict_GetItem(globals, key)) {
    // The dict getitem could have triggered a lazy import with side effects
    // that unwatched the dict.
    if (cache.valuePtr()) {
      *cache.valuePtr() = globals_value;
    }
    return;
  }

  // The getitem on globals might have had side effects and made this dict
  // unwatchable, so it needs to be checked again.
  if (_PyDict_HasOnlyUnicodeKeys(builtins)) {
    *cache.valuePtr() = PyDict_GetItem(builtins, key);
    if (globals != builtins) {
      watchDictKey(builtins, key, cache);
    }
  }
}

bool GlobalCacheManager::updateCache(
    GlobalCache cache,
    BorrowedRef<PyDictObject> dict,
    BorrowedRef<> new_value) {
  if (new_value && PyLazyImport_CheckExact(new_value)) {
    return true;
  }

  BorrowedRef<PyDictObject> globals = cache.key().globals;
  BorrowedRef<PyDictObject> builtins = cache.key().builtins;
  BorrowedRef<PyUnicodeObject> name = cache.key().name;

  if (dict == globals) {
    if (new_value == nullptr && globals != builtins) {
      if (!_PyDict_HasOnlyUnicodeKeys(builtins)) {
        // builtins is no longer watchable. Mark this cache for disabling.
        return true;
      }

      // Fall back to the builtin (which may also be null).
      *cache.valuePtr() = PyDict_GetItem(builtins, name);

      // it changed, and it changed from something to nothing, so
      // we weren't watching builtins and need to start now.
      if (!isWatchedDictKey(builtins, name, cache)) {
        watchDictKey(builtins, name, cache);
      }
    } else {
      *cache.valuePtr() = new_value;
    }
  } else {
    JIT_CHECK(dict == builtins, "Unexpected dict");
    JIT_CHECK(_PyDict_HasOnlyUnicodeKeys(globals), "Bad globals dict");
    // Check if this value is shadowed.
    PyObject* globals_value = PyDict_GetItem(globals, name);
    if (globals_value == nullptr) {
      *cache.valuePtr() = new_value;
    }
  }

  return false;
}

void GlobalCacheManager::disableCache(GlobalCache cache) {
  cache.clear();
  map_.erase(cache.key());
}

void GlobalCacheManager::disableCaches(const std::vector<GlobalCache>& caches) {
  for (GlobalCache cache : caches) {
    BorrowedRef<PyDictObject> dict = cache.key().globals;
    BorrowedRef<PyUnicodeObject> name = cache.key().name;
    disableCache(cache);
    unwatchDictKey(dict, name, cache);
  }
}

} // namespace jit
