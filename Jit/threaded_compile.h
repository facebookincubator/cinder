// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/ref.h"

#include <cassert>
#include <mutex>
#include <vector>

namespace jit {

// Threaded-compile state for the whole process.
class ThreadedCompileContext {
 public:
  void startCompile(std::vector<BorrowedRef<>>&& work_queue) {
    // Can't use JIT_CHECK because we're included by log.h
    assert(!compile_running_);
    work_queue_ = std::move(work_queue);
    compile_running_ = true;
  }

  std::vector<BorrowedRef<>>&& endCompile() {
    compile_running_ = false;
    return std::move(retry_list_);
  }

  BorrowedRef<> nextUnit() {
    lock();
    if (work_queue_.empty()) {
      unlock();
      return nullptr;
    }
    BorrowedRef<> unit = std ::move(work_queue_.back());
    work_queue_.pop_back();
    unlock();
    return unit;
  }

  // Assumes we have the lock
  void retryUnit(BorrowedRef<> unit) {
    lock();
    retry_list_.emplace_back(unit);
    unlock();
  }

  bool compileRunning() const {
    return compile_running_;
  }

 private:
  friend class ThreadedCompileSerialize;

  void lock() {
    if (compile_running_) {
      mutex_.lock();
    }
  }

  void unlock() {
    if (compile_running_) {
      mutex_.unlock();
    }
  }

  bool compile_running_{false};
  // This needs to be recursive because we allow recursive compilation via
  // jit::hir::tryRecursiveCompile
  std::recursive_mutex mutex_;
  std::vector<BorrowedRef<>> work_queue_;
  std::vector<BorrowedRef<>> retry_list_;
};

extern ThreadedCompileContext g_threaded_compile_context;

// RAII device for acquiring the global threaded-compile lock.
class ThreadedCompileSerialize {
 public:
  ThreadedCompileSerialize() {
    g_threaded_compile_context.lock();
  }

  ~ThreadedCompileSerialize() {
    g_threaded_compile_context.unlock();
  }
};

// Acquire the global threaded-compile lock for the execution of an expression.
#define THREADED_COMPILE_SERIALIZED_CALL(expr) \
  ([&]() {                                     \
    jit::ThreadedCompileSerialize guard;       \
    return expr;                               \
  })()

} // namespace jit
