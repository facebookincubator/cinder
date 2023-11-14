// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/ref.h"

#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>
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

  // Returns true if it's safe for the current thread to access data protected
  // by the threaded compile lock, either because no threaded compile is active
  // or the current thread holds the lock. May return true erroneously, but
  // shouldn't return false erroneously.
  bool canAccessSharedData() const {
    return !compileRunning() ||
        mutex_holder_.load(std::memory_order_relaxed) ==
        std::this_thread::get_id();
  }

 private:
  friend class ThreadedCompileSerialize;

  void lock() {
    if (compileRunning()) {
      mutex_.lock();
      mutex_holder_.store(
          std::this_thread::get_id(), std::memory_order_relaxed);
    }
  }

  void unlock() {
    if (compileRunning()) {
      mutex_holder_.store(std::thread::id{}, std::memory_order_relaxed);
      mutex_.unlock();
    }
  }

  // This is only written by the main thread, and only when no worker threads
  // exist. While worker threads exist, it is only read (mostly by the worker
  // threads).
  bool compile_running_{false};

  // This needs to be recursive because we allow recursive compilation via
  // jit::hir::tryRecursiveCompile
  std::recursive_mutex mutex_;

  // mutex_holder_ is used only in assertions, to protect against one thread
  // accessing data it shouldn't while a threaded compile is active. False
  // negatives in these assertions are OK, and can't be prevented without
  // additional locking that wouldn't be worth the overhead.
  //
  // False positives are not OK, and would be caused either by a thread reading
  // compile_running_ == true after the threaded compile has finished, or by a
  // thread reading someone else's id from mutex_holder_ while the first thread
  // has the lock. The former shouldn't happen because all stores to
  // compile_running_ happen while no worker threads exist, so there's no
  // opportunity for a data race. The latter shouldn't be possible because a
  // thread writes its own id to mutex_holder_, and within that thread the
  // write is sequenced before any reads of mutex_holder_ while doing work
  // later.
  std::atomic<std::thread::id> mutex_holder_;

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
