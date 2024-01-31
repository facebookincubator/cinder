// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "Python.h"
#include "internal/pycore_pystate.h"

#include "cinderx/Jit/ref.h"
#include "cinderx/Jit/threaded_compile.h"

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/printf.h>

#include <cstdio>
#include <iterator>
#include <string_view>

namespace jit {

template <typename... Args>
auto format_to(
    std::string& s,
    fmt::format_string<Args...> format,
    Args&&... args) {
  return fmt::format_to(
      std::back_inserter(s), format, std::forward<Args>(args)...);
}

extern int g_debug;
extern int g_debug_refcount;
extern int g_debug_verbose;
extern int g_dump_hir;
extern int g_dump_hir_passes;
extern std::string g_dump_hir_passes_json;
extern int g_dump_final_hir;
extern int g_dump_lir;
extern int g_dump_lir_no_origin;
extern int g_dump_c_helper;
extern int g_dump_asm;
extern int g_symbolize_funcs;
extern int g_dump_stats;
extern int g_collect_inline_cache_stats;
extern FILE* g_log_file;

// Use PyObject_Repr() to get a string representation of a PyObject. Use with
// caution - this can end up executing arbitrary Python code. Always succeeds
// but may return a description of an error in string e.g.
// "<failed to get UTF8 from Python string>"
std::string repr(BorrowedRef<> obj);

#define JIT_LOG(...)                                                     \
  {                                                                      \
    ::jit::ThreadedCompileSerialize guard;                               \
    fmt::print(::jit::g_log_file, "JIT: {}:{} -- ", __FILE__, __LINE__); \
    fmt::print(::jit::g_log_file, __VA_ARGS__);                          \
    fmt::print(::jit::g_log_file, "\n");                                 \
    std::fflush(::jit::g_log_file);                                      \
  }

#define JIT_LOGIF(PRED, ...) \
  if (PRED) {                \
    JIT_LOG(__VA_ARGS__);    \
  }

#define JIT_DLOG(...) JIT_LOGIF(::jit::g_debug_verbose, __VA_ARGS__)

#define JIT_CHECK(COND, ...)                      \
  {                                               \
    if (!(COND)) {                                \
      fmt::print(                                 \
          stderr,                                 \
          "JIT: {}:{} -- Assertion failed: {}\n", \
          __FILE__,                               \
          __LINE__,                               \
          #COND);                                 \
      JIT_ABORT_IMPL(__VA_ARGS__);                \
    }                                             \
  }

#define JIT_ABORT(...)                                               \
  {                                                                  \
    fmt::print(stderr, "JIT: {}:{} -- Abort\n", __FILE__, __LINE__); \
    JIT_ABORT_IMPL(__VA_ARGS__);                                     \
  }

#define JIT_ABORT_IMPL(...)                              \
  {                                                      \
    fmt::print(stderr, __VA_ARGS__);                     \
    fmt::print(stderr, "\n");                            \
    std::fflush(stderr);                                 \
    PyThreadState* tstate = _PyThreadState_GET();        \
    if (tstate != NULL && tstate->curexc_type != NULL) { \
      PyErr_Display(                                     \
          tstate->curexc_type,                           \
          tstate->curexc_value,                          \
          tstate->curexc_traceback);                     \
    }                                                    \
    std::abort();                                        \
  }

#ifdef Py_DEBUG
#define JIT_DABORT(...) JIT_ABORT(__VA_ARGS__)
#define JIT_DCHECK(COND, ...) JIT_CHECK((COND), __VA_ARGS__)
#else
#define JIT_DABORT(...)     \
  if (0) {                  \
    JIT_ABORT(__VA_ARGS__); \
  }
#define JIT_DCHECK(COND, ...)       \
  if (0) {                          \
    JIT_CHECK((COND), __VA_ARGS__); \
  }
#endif

} // namespace jit
