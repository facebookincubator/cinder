// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef JIT_LOG_H
#define JIT_LOG_H

#include "Jit/ref.h"
#include "Python.h"
#include "internal/pycore_pystate.h"

#include <fmt/format.h>
#include <fmt/printf.h>

#include <stdio.h>
#include <iterator>

#include "Jit/threaded_compile.h"

namespace jit {

template <typename Fmt, typename... Args>
auto format_to(std::string& s, const Fmt& format, Args&&... args) {
  return fmt::format_to(
      std::back_inserter(s), format, std::forward<Args>(args)...);
}

extern int g_debug;
extern int g_debug_refcount;
extern int g_debug_verbose;
extern int g_dump_hir;
extern int g_dump_hir_passes;
extern int g_dump_final_hir;
extern int g_dump_lir;
extern int g_dump_lir_no_origin;
extern int g_dump_c_helper;
extern int g_dump_asm;
extern int g_dump_stats;
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
void protected_fprintf(std::FILE* file, const char* fmt, Args&&... args) {
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

#define JIT_CHECK(__cond, ...)                             \
  {                                                        \
    if (!(__cond)) {                                       \
      fmt::fprintf(                                        \
          stderr,                                          \
          "JIT: %s:%d -- assertion failed: %s\n",          \
          __FILE__,                                        \
          __LINE__,                                        \
          #__cond);                                        \
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
      abort();                                             \
    }                                                      \
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

#endif /* JIT_LOG_H */
