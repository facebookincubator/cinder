// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/runtime.h"

#include <memory>

namespace jit {

const int64_t CodeRuntime::kPyCodeOffset = offsetof(CodeRuntime, py_code_);

int GenYieldPoint::visitRefs(PyGenObject* gen, visitproc visit, void* arg)
    const {
  for (auto offs : pyobj_offs_) {
    PyObject* v = reinterpret_cast<PyObject*>(
        *(reinterpret_cast<uint64_t*>(gen->gi_jit_data) + offs));
    Py_VISIT(v);
  }
  return 0;
}

void GenYieldPoint::releaseRefs(PyGenObject* gen) const {
  for (auto offs : pyobj_offs_) {
    PyObject* v = reinterpret_cast<PyObject*>(
        *(reinterpret_cast<uint64_t*>(gen->gi_jit_data) + offs));
    Py_DECREF(v);
  }
}

void CodeRuntime::releaseReferences() {
  references_.clear();
}

void CodeRuntime::addReference(PyObject* obj) {
  JIT_CHECK(obj != nullptr, "Can't own a reference to nullptr");
  // Serialize as we modify the ref-count to obj which may be widely accesible.
  ThreadedCompileSerialize guard;
  references_.emplace(obj);
}

PyObject* GenYieldPoint::yieldFromValue(GenDataFooter* gen_footer) const {
  if (!isYieldFrom_) {
    return NULL;
  }
  return reinterpret_cast<PyObject*>(
      *(reinterpret_cast<uint64_t*>(gen_footer) + yieldFromOffs_));
}

GlobalCache Runtime::findGlobalCache(PyObject* globals, PyObject* name) {
  JIT_CHECK(PyUnicode_CheckExact(name), "Name must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(name), "Name must be interned");
  auto result = global_caches_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(PyEval_GetBuiltins(), globals, name),
      std::forward_as_tuple());
  GlobalCache cache(&*result.first);
  if (result.second) {
    cache.init();
  }
  JIT_CHECK(_PyDict_IsWatched(globals), "globals dict isn't watched");
  return cache;
}

GlobalCache Runtime::findDictCache(PyObject* dict, PyObject* name) {
  JIT_CHECK(PyUnicode_CheckExact(name), "Name must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(name), "Name must be interned");
  auto result = global_caches_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(dict, dict, name),
      std::forward_as_tuple());
  GlobalCache cache(&*result.first);
  if (result.second) {
    cache.init();
  }
  JIT_CHECK(_PyDict_IsWatched(dict), "dict isn't watched");
  return cache;
}

void** Runtime::findFunctionEntryCache(PyFunctionObject* function) {
  auto result = function_entry_caches_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(function),
      std::forward_as_tuple());
  addReference((PyObject*)function);
  if (result.second) {
    if (_PyClassLoader_HasPrimitiveArgs((PyCodeObject*)function->func_code)) {
      result.first->second.arg_info =
          Ref<_PyTypedArgsInfo>::steal(_PyClassLoader_GetTypedArgsInfo(
              (PyCodeObject*)function->func_code, 1));
    }
  }
  return result.first->second.ptr_.get();
}

_PyTypedArgsInfo* Runtime::findFunctionPrimitiveArgInfo(
    PyFunctionObject* function) {
  auto cache = function_entry_caches_.find(function);
  if (cache == function_entry_caches_.end()) {
    return nullptr;
  }
  return cache->second.arg_info.get();
}

void Runtime::forgetLoadGlobalCache(GlobalCache cache) {
  auto it = global_caches_.find(cache.key());
  orphaned_global_caches_.emplace_back(std::move(it->second));
  global_caches_.erase(it);
}

std::size_t Runtime::addDeoptMetadata(DeoptMetadata&& deopt_meta) {
  // Serialize as the deopt data is shared across compile threads.
  ThreadedCompileSerialize guard;
  deopt_metadata_.emplace_back(std::move(deopt_meta));
  return deopt_metadata_.size() - 1;
}

DeoptMetadata& Runtime::getDeoptMetadata(std::size_t id) {
  // Serialize as the deopt data is shared across compile threads.
  ThreadedCompileSerialize guard;
  return deopt_metadata_[id];
}

void Runtime::recordDeopt(std::size_t idx, PyObject* guilty_value) {
  DeoptStat& stat = deopt_stats_[idx];
  stat.count++;
  if (guilty_value != nullptr) {
    stat.types.recordType(Py_TYPE(guilty_value));
  }
}

const DeoptStats& Runtime::deoptStats() const {
  return deopt_stats_;
}

void Runtime::clearDeoptStats() {
  deopt_stats_.clear();
}

void Runtime::setGuardFailureCallback(Runtime::GuardFailureCallback cb) {
  guard_failure_callback_ = cb;
}

void Runtime::guardFailed(const DeoptMetadata& deopt_meta) {
  if (guard_failure_callback_) {
    guard_failure_callback_(deopt_meta);
  }
}

void Runtime::clearGuardFailureCallback() {
  guard_failure_callback_ = nullptr;
}

void Runtime::addReference(PyObject* obj) {
  JIT_CHECK(obj != nullptr, "Can't own a reference to nullptr");
  references_.emplace(obj);
}

void Runtime::releaseReferences() {
  for (auto& code_rt : runtimes_) {
    code_rt->releaseReferences();
  }
  references_.clear();
}

} // namespace jit
