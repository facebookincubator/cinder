// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "internal/pycore_pystate.h"

#include "Jit/ref.h"
#include "Jit/threaded_compile.h"

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
extern const char* g_dump_hir_passes_json;
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

// fmt doesn't support compile-time checking of printf-style format strings, so
// this wrapper is used to contain any exceptions rather than aborting during a
// JIT_LOG() or unwinding the stack after a failed JIT_CHECK().
template <typename... Args>
void protected_fprintf(std::FILE* file, std::string_view fmt, Args&&... args) {
  try {
    fmt::fprintf(file, fmt, std::forward<Args>(args)...);
  } catch (const fmt::format_error& fe) {
    fmt::fprintf(file, "Bad format string '%s': '%s'", fmt, fe.what());
  }
}

#define JIT_LOG(...)                                                       \
  do {                                                                     \
    ::jit::ThreadedCompileSerialize guard;                                 \
    fmt::fprintf(::jit::g_log_file, "JIT: %s:%d -- ", __FILE__, __LINE__); \
    ::jit::protected_fprintf(::jit::g_log_file, __VA_ARGS__);              \
    fmt::fprintf(::jit::g_log_file, "\n");                                 \
    fflush(::jit::g_log_file);                                             \
  } while (0)

#define JIT_LOGIF(pred, ...) \
  if (pred) {                \
    JIT_LOG(__VA_ARGS__);    \
  }

#define JIT_DLOG(...)             \
  {                               \
    if (::jit::g_debug_verbose) { \
      JIT_LOG(__VA_ARGS__);       \
    }                             \
  }

#define JIT_CHECK(__cond, ...)                    \
  {                                               \
    if (!(__cond)) {                              \
      fmt::fprintf(                               \
          stderr,                                 \
          "JIT: %s:%d -- Assertion failed: %s\n", \
          __FILE__,                               \
          __LINE__,                               \
          #__cond);                               \
      JIT_ABORT_IMPL(__VA_ARGS__);                \
    }                                             \
  }

#define JIT_ABORT(...)                                                 \
  {                                                                    \
    fmt::fprintf(stderr, "JIT: %s:%d -- Abort\n", __FILE__, __LINE__); \
    JIT_ABORT_IMPL(__VA_ARGS__);                                       \
  }

#define JIT_ABORT_IMPL(...)                              \
  {                                                      \
    ::jit::protected_fprintf(stderr, __VA_ARGS__);       \
    fmt::fprintf(stderr, "\n");                          \
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
#define JIT_DCHECK(__cond, ...) JIT_CHECK(__cond, __VA_ARGS__)
#else
#define JIT_DCHECK(__cond, ...)     \
  if (0) {                          \
    JIT_CHECK(__cond, __VA_ARGS__); \
  }
#endif

} // namespace jit
