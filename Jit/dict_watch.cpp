// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/dict_watch.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/inline_cache.h"
#include "Jit/pyjit.h"

#include <set>
#include <unordered_map>

namespace jit {

namespace {

// For every watched dict, this map contains a map from keys to sets of caches
// that care about that key.
std::unordered_map<
    PyObject*,
    std::unordered_map<PyObject*, std::set<GlobalCache>>>
    g_dict_watchers;

void disableCaches(const std::vector<GlobalCache>& to_disable) {
  for (auto& cache : to_disable) {
    PyObject* name = cache.key().name;
    PyObject* dict = cache.key().globals;
    cache.disable();
    unwatchDictKey(dict, name, cache);
  }
}

} // namespace

bool isWatchedDictKey(PyObject* dict, PyObject* key, GlobalCache cache) {
  auto dict_it = g_dict_watchers.find(dict);
  if (dict_it == g_dict_watchers.end()) {
    return false;
  }
  auto& dict_keys = dict_it->second;
  auto key_it = dict_keys.find(key);
  if (key_it != dict_keys.end()) {
    return key_it->second.count(cache);
  }
  return false;
}

void watchDictKey(PyObject* dict, PyObject* key, GlobalCache cache) {
  JIT_CHECK(PyUnicode_CheckExact(key), "key must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(key), "key must be interned");
  auto& watchers = g_dict_watchers[dict][key];
  bool inserted = watchers.emplace(cache).second;
  JIT_CHECK(inserted, "cache was already watching key");
  _PyDict_Watch(dict);
}

void unwatchDictKey(PyObject* dict, PyObject* key, GlobalCache cache) {
  auto dict_it = g_dict_watchers.find(dict);
  JIT_CHECK(dict_it != g_dict_watchers.end(), "dict has no watchers");
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
      g_dict_watchers.erase(dict_it);
      _PyDict_Unwatch(dict);
    }
  }
}

} // namespace jit

void _PyJIT_NotifyDictKey(PyObject* dict, PyObject* key, PyObject* value) {
  // key is overwhemlingly likely to be interned, since in normal code it comes
  // from co_names. If it's not, we at least know that an interned string with
  // its value exists (because we're watching it), so this should just be a
  // quick lookup.
  JIT_CHECK(PyUnicode_CheckExact(key), "key must be a str");
  if (!PyUnicode_CHECK_INTERNED(key)) {
    Py_INCREF(key);
    PyUnicode_InternInPlace(&key);
    Py_DECREF(key);
  }

  auto dict_it = jit::g_dict_watchers.find(dict);
  // A dict might be watched for Static Python's purposes as well. Return early
  // if no matchers were registered.
  if (dict_it == jit::g_dict_watchers.end()) {
    return;
  }
  auto key_it = dict_it->second.find(key);
  if (key_it == dict_it->second.end()) {
    return;
  }
  std::vector<jit::GlobalCache> to_disable;
  for (auto& cache : key_it->second) {
    cache.update(reinterpret_cast<PyObject*>(dict), value, to_disable);
  }
  jit::disableCaches(to_disable);
}

void _PyJIT_NotifyDictUnwatch(PyObject* dict) {
  auto dict_it = jit::g_dict_watchers.find(dict);
  // A dict might be watched for Static Python's purposes as well. Return early
  // if no matchers were registered.
  if (dict_it == jit::g_dict_watchers.end()) {
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

      cache.disable();
    }
  }
  jit::g_dict_watchers.erase(dict_it);
}

void _PyJIT_NotifyDictClear(PyObject* dict) {
  auto dict_it = jit::g_dict_watchers.find(dict);
  // A dict might be watched for Static Python's purposes as well. Return early
  // if no matchers were registered.
  if (dict_it == jit::g_dict_watchers.end()) {
    return;
  }
  std::vector<jit::GlobalCache> to_disable;
  for (auto& key_pair : dict_it->second) {
    for (auto& cache : key_pair.second) {
      cache.update(dict, nullptr, to_disable);
    }
  }
  jit::disableCaches(to_disable);
}

PyObject**
_PyJIT_GetGlobalCache(PyObject* builtins, PyObject* globals, PyObject* key) {
  try {
    auto cache = jit::Runtime::get()->findGlobalCache(builtins, globals, key);
    return cache.valuePtr();
  } catch (std::bad_alloc& ba) {
    return nullptr;
  }
}

PyObject** _PyJIT_GetDictCache(PyObject* globals, PyObject* key) {
  try {
    auto cache = jit::Runtime::get()->findDictCache(globals, key);
    return cache.valuePtr();
  } catch (std::bad_alloc& ba) {
    return nullptr;
  }
}

void _PyJIT_ClearDictCaches() {
  std::vector<PyObject*> keys;
  for (auto& pair : jit::g_dict_watchers) {
    keys.push_back(pair.first);
  }
  for (auto dict : keys) {
    auto dict_it = jit::g_dict_watchers.find(dict);
    // NotifyDictUnwatch may clear out our dictionary and builtins,
    // so we need to make sure each dictionary is still being watched
    if (dict_it != jit::g_dict_watchers.end()) {
      _PyJIT_NotifyDictUnwatch(dict);
      _PyDict_Unwatch(dict);
    }
  }
}
