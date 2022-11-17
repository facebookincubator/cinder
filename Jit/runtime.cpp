// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/runtime.h"

#include <sys/mman.h>

#include <memory>

namespace jit {

const int64_t CodeRuntime::kPyCodeOffset =
    RuntimeFrameState::codeOffset() + CodeRuntime::frameStateOffset();

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
  // Serialize as we modify the ref-count to obj which may be widely accessible.
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

Runtime* Runtime::s_runtime_{nullptr};

void Runtime::shutdown() {
  _PyJIT_ClearDictCaches();
  delete s_runtime_;
  s_runtime_ = nullptr;
}

void Runtime::mlockProfilerDependencies() {
  for (auto& codert : code_runtimes_) {
    PyCodeObject* code = codert.frameState()->code().get();
    ::mlock(code, sizeof(PyCodeObject));
    ::mlock(code->co_qualname, Py_SIZE(code->co_qualname));
  }
  code_runtimes_.mlock();
}

Ref<> Runtime::pageInProfilerDependencies() {
  ThreadedCompileSerialize guard;
  Ref<> qualnames = Ref<>::steal(PyList_New(0));
  if (qualnames == nullptr) {
    return nullptr;
  }
  // We want to force the OS to page in the memory on the
  // code_rt->code->qualname path and keep the compiler from optimizing away
  // the code to do so. There are probably more efficient ways of doing this
  // but perf isn't a major concern.
  for (auto& code_rt : code_runtimes_) {
    BorrowedRef<> qualname = code_rt.frameState()->code()->co_qualname;
    if (qualname == nullptr) {
      continue;
    }
    if (PyList_Append(qualnames, qualname) < 0) {
      return nullptr;
    }
  }
  return qualnames;
}

GlobalCache Runtime::findGlobalCache(
    PyObject* builtins,
    PyObject* globals,
    PyObject* name) {
  JIT_CHECK(PyUnicode_CheckExact(name), "Name must be a str");
  JIT_CHECK(PyUnicode_CHECK_INTERNED(name), "Name must be interned");
  auto result = global_caches_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(builtins, globals, name),
      std::forward_as_tuple());
  GlobalCache cache(&*result.first);
  if (result.second) {
    cache.init(reinterpret_cast<PyObject**>(pointer_caches_.allocate()));
  }
  return cache;
}

GlobalCache Runtime::findGlobalCache(PyObject* globals, PyObject* name) {
  return findGlobalCache(PyEval_GetBuiltins(), globals, name);
}

GlobalCache Runtime::findDictCache(PyObject* dict, PyObject* name) {
  return findGlobalCache(dict, dict, name);
}

void** Runtime::findFunctionEntryCache(PyFunctionObject* function) {
  auto result = function_entry_caches_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(function),
      std::forward_as_tuple());
  addReference((PyObject*)function);
  if (result.second) {
    result.first->second.ptr = pointer_caches_.allocate();
    if (_PyClassLoader_HasPrimitiveArgs((PyCodeObject*)function->func_code)) {
      result.first->second.arg_info =
          Ref<_PyTypedArgsInfo>::steal(_PyClassLoader_GetTypedArgsInfo(
              (PyCodeObject*)function->func_code, 1));
    }
  }
  return result.first->second.ptr;
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

TypeProfiles& Runtime::typeProfiles() {
  return type_profiles_;
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
  for (auto& code_rt : code_runtimes_) {
    code_rt.releaseReferences();
  }
  references_.clear();
}

} // namespace jit
