// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/global_cache.h"

#include "Jit/dict_watch.h"
#include "Jit/runtime.h"

namespace jit {

void GlobalCache::init(PyObject** cache) const {
  pair_->second.ptr = cache;

  // We want to try and only watch builtins if this is really a
  // builtin.  So we will start only watching globals, and if
  // the value gets deleted from globals then we'll start
  // tracking builtins as well.  Once we start tracking builtins
  // we'll never stop rather than trying to handle all of the
  // transitions.
  watchDictKey(key().globals, key().name, *this);
  PyObject* builtins = key().builtins;

  // We don't need to immediately watch builtins if it's
  // defined as a global
  if (PyObject* globals_value = PyDict_GetItem(key().globals, key().name)) {
    // the dict getitem could have triggered a lazy import with side effects
    // that unwatched the dict
    if (valuePtr()) {
      *valuePtr() = globals_value;
    }
  } else if (_PyDict_HasOnlyUnicodeKeys(builtins)) {
    *valuePtr() = PyDict_GetItem(builtins, key().name);
    if (key().globals != builtins) {
      watchDictKey(builtins, key().name, *this);
    }
  }
}

void GlobalCache::update(
    PyObject* dict,
    PyObject* new_value,
    std::vector<GlobalCache>& to_disable) const {
  PyObject* builtins = key().builtins;
  if (dict == key().globals) {
    if (new_value == nullptr && key().globals != builtins) {
      if (!_PyDict_HasOnlyUnicodeKeys(builtins)) {
        // builtins is no longer watchable. Mark this cache for disabling.
        to_disable.emplace_back(*this);
        return;
      }

      // Fall back to the builtin (which may also be null).
      *valuePtr() = PyDict_GetItem(builtins, key().name);

      // it changed, and it changed from something to nothing, so
      // we weren't watching builtins and need to start now.
      if (!isWatchedDictKey(builtins, key().name, *this)) {
        watchDictKey(builtins, key().name, *this);
      }
    } else {
      *valuePtr() = new_value;
    }
  } else {
    JIT_CHECK(dict == builtins, "Unexpected dict");
    JIT_CHECK(_PyDict_HasOnlyUnicodeKeys(key().globals), "Bad globals dict");
    // Check if this value is shadowed.
    PyObject* globals_value = PyDict_GetItem(key().globals, key().name);
    if (globals_value == nullptr) {
      *valuePtr() = new_value;
    }
  }
}

void GlobalCache::disable() const {
  *valuePtr() = nullptr;
  jit::Runtime::get()->forgetLoadGlobalCache(*this);
}

} // namespace jit
