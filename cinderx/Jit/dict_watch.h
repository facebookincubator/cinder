// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/global_cache.h"

namespace jit {

// Checks if a given key of a dict is watched by the given cache.
bool isWatchedDictKey(PyObject* dict, PyObject* key, GlobalCache cache);

// Watch the given key of the given dict. The cache's update() method will be
// called when the key's value in the dict is changed or removed. The cache's
// disable() method will be called if the dict becomes unwatchable.
void watchDictKey(PyObject* dict, PyObject* key, GlobalCache cache);

// Unsubscribe from the given key of the given dict.
void unwatchDictKey(PyObject* dict, PyObject* key, GlobalCache cache);

} // namespace jit
