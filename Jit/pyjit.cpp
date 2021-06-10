// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/pyjit.h"

#include "Python.h"
//#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"

#include "Include/internal/pycore_pystate.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

#include "Jit/codegen/gen_asm.h"
#include "Jit/frame.h"
#include "Jit/hir/builder.h"
#include "Jit/jit_context.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/jit_list.h"
#include "Jit/jit_x_options.h"
#include "Jit/log.h"
#include "Jit/perf_jitdump.h"
#include "Jit/ref.h"
#include "Jit/runtime.h"

#define DEFAULT_CODE_SIZE 2 * 1024 * 1024

using namespace jit;

enum InitState { JIT_NOT_INITIALIZED, JIT_INITIALIZED, JIT_FINALIZED };
enum FrameMode { PY_FRAME = 0, NO_FRAME, SHADOW_FRAME };

struct JitConfig {
  InitState init_state{JIT_NOT_INITIALIZED};

  int is_enabled{0};
  FrameMode frame_mode{PY_FRAME};
  int are_type_slots_enabled{0};
  int allow_jit_list_wildcards{0};
  int compile_all_static_functions{0};
  size_t batch_compile_workers{0};
  int test_multithreaded_compile{0};
};
JitConfig jit_config;

namespace {
// Extra information needed to compile a PyCodeObject.
struct CodeData {
  CodeData(PyObject* m, PyObject* g) : module{m}, globals{g} {}

  Ref<> module;
  Ref<PyDictObject> globals;
};
} // namespace

static _PyJITContext* jit_ctx;
static JITList* g_jit_list{nullptr};

// Function and code objects registered for compilation. Every entry that is a
// code object has corresponding entry in jit_code_data.
static std::unordered_set<BorrowedRef<>> jit_reg_units;
static std::unordered_map<BorrowedRef<PyCodeObject>, CodeData> jit_code_data;

// Strong references to every function and code object that were ever
// registered, to keep them alive for batch testing.
static std::vector<Ref<>> test_multithreaded_units;
static std::unordered_map<PyFunctionObject*, std::chrono::duration<double>>
    jit_time_functions;

// Frequently-used strings that we intern at JIT startup and hold references to.
#define INTERNED_STRINGS(X) \
  X(func_fullname)          \
  X(filename)               \
  X(lineno)                 \
  X(count)                  \
  X(reason)                 \
  X(guilty_type)            \
  X(description)

#define DECLARE_STR(s) static PyObject* s_str_##s{nullptr};
INTERNED_STRINGS(DECLARE_STR)
#undef DECLARE_STR

static double total_compliation_time = 0.0;

// This is read directly from ceval.c to minimize overhead.
int g_capture_interp_cost = 0;
static std::unordered_map<std::string, long> g_code_interp_cost;

struct CompilationTimer {
  explicit CompilationTimer(BorrowedRef<PyFunctionObject> f)
      : start(std::chrono::steady_clock::now()), func(f) {}

  ~CompilationTimer() {
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_span =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);

    double time = time_span.count();
    total_compliation_time += time;
    jit::ThreadedCompileSerialize guard;
    jit_time_functions.emplace(func, time_span);
  }

  std::chrono::steady_clock::time_point start;
  BorrowedRef<PyFunctionObject> func{nullptr};
};

static std::atomic<int> g_compile_workers_attempted;
static int g_compile_workers_retries;

// Compile the given compilation unit, returning the result code.
static _PyJIT_Result compileUnit(BorrowedRef<> unit) {
  if (PyFunction_Check(unit)) {
    BorrowedRef<PyFunctionObject> func(unit);
    CompilationTimer t{func};
    return _PyJITContext_CompileFunction(jit_ctx, func);
  }
  JIT_CHECK(PyCode_Check(unit), "Expected function or code object");
  BorrowedRef<PyCodeObject> code(unit);
  const CodeData& data = map_get(jit_code_data, code);
  return _PyJITContext_CompileCode(jit_ctx, data.module, code, data.globals);
}

static void compile_worker_thread() {
  JIT_DLOG("Started compile worker in thread %d", std::this_thread::get_id());
  BorrowedRef<> unit;
  while ((unit = g_threaded_compile_context.nextUnit()) != nullptr) {
    g_compile_workers_attempted++;
    if (compileUnit(unit) == PYJIT_RESULT_RETRY) {
      ThreadedCompileSerialize guard;
      g_compile_workers_retries++;
      g_threaded_compile_context.retryUnit(unit);
      JIT_LOG(
          "Retrying compile of function: %s",
          funcFullname(reinterpret_cast<PyFunctionObject*>(unit.get())));
    }
  }
  JIT_DLOG("Finished compile worker in thread %d", std::this_thread::get_id());
}

static void multithread_compile_all(std::vector<BorrowedRef<>>&& work_units) {
  JIT_CHECK(jit_ctx, "JIT not initialized");

  // Disable checks for using GIL protected data across threads.
  // Conceptually what we're doing here is saying we're taking our own
  // responsibility for managing locking of CPython runtime data structures.
  // Instead of holding the GIL to serialize execution to one thread, we're
  // holding the GIL for a group of co-operating threads which are aware of each
  // other. We still need the GIL as this protects the cooperating threads from
  // unknown other threads. Within our group of cooperating threads we can
  // safely do any read-only operations in parallel, but we grab our own lock if
  // we do a write (e.g. an incref).
  int old_gil_check_enabled = _PyGILState_check_enabled;
  _PyGILState_check_enabled = 0;

  g_threaded_compile_context.startCompile(std::move(work_units));
  std::vector<std::thread> worker_threads;
  JIT_CHECK(jit_config.batch_compile_workers, "Zero workers for compile");
  {
    // Hold a lock while we create threads because IG production has magic to
    // wrap pthread_create() and run Python code before threads are created.
    ThreadedCompileSerialize guard;
    for (size_t i = 0; i < jit_config.batch_compile_workers; i++) {
      worker_threads.emplace_back(compile_worker_thread);
    }
  }
  for (std::thread& worker_thread : worker_threads) {
    worker_thread.join();
  }
  std::vector<BorrowedRef<>> retry_list{
      g_threaded_compile_context.endCompile()};
  for (auto unit : retry_list) {
    compileUnit(unit);
  }
  _PyGILState_check_enabled = old_gil_check_enabled;
}

static PyObject* test_multithreaded_compile(PyObject*, PyObject*) {
  if (!jit_config.test_multithreaded_compile) {
    PyErr_SetString(
        PyExc_NotImplementedError, "test_multithreaded_compile not enabled");
    return NULL;
  }
  g_compile_workers_attempted = 0;
  g_compile_workers_retries = 0;
  JIT_LOG("(Re)compiling %d units", jit_reg_units.size());
  std::chrono::time_point time_start = std::chrono::steady_clock::now();
  multithread_compile_all(
      {test_multithreaded_units.begin(), test_multithreaded_units.end()});
  std::chrono::time_point time_end = std::chrono::steady_clock::now();
  JIT_LOG(
      "Took %d ms, compiles attempted: %d, compiles retried: %d",
      std::chrono::duration_cast<std::chrono::milliseconds>(
          time_end - time_start)
          .count(),
      g_compile_workers_attempted,
      g_compile_workers_retries);
  test_multithreaded_units.clear();
  Py_RETURN_NONE;
}

static PyObject* is_test_multithreaded_compile_enabled(PyObject*, PyObject*) {
  if (jit_config.test_multithreaded_compile) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyObject*
disable_jit(PyObject* /* self */, PyObject* const* args, Py_ssize_t nargs) {
  if (nargs > 1) {
    PyErr_SetString(PyExc_TypeError, "disable expects 0 or 1 arg");
    return NULL;
  } else if (nargs == 1 && !PyBool_Check(args[0])) {
    PyErr_SetString(
        PyExc_TypeError,
        "disable expects bool indicating to compile pending functions");
    return NULL;
  }

  if (nargs == 0 || args[0] == Py_True) {
    // Compile all of the pending functions/codes before shutting down
    if (jit_config.batch_compile_workers > 0) {
      multithread_compile_all({jit_reg_units.begin(), jit_reg_units.end()});
      jit_reg_units.clear();
    } else {
      std::unordered_set<BorrowedRef<>> units;
      units.swap(jit_reg_units);
      for (auto unit : units) {
        compileUnit(unit);
      }
    }
    jit_code_data.clear();
  }

  _PyJIT_Disable();
  Py_RETURN_NONE;
}

static PyObject* force_compile(PyObject* /* self */, PyObject* func) {
  if (!PyFunction_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "force_compile expected a function");
    return NULL;
  }

  if (jit_reg_units.count(func)) {
    _PyJIT_CompileFunction((PyFunctionObject*)func);
    Py_RETURN_TRUE;
  }

  Py_RETURN_FALSE;
}

int _PyJIT_IsCompiled(PyObject* func) {
  if (jit_ctx == nullptr) {
    return 0;
  }
  if (!PyFunction_Check(func)) {
    return 0;
  }

  return _PyJITContext_DidCompile(jit_ctx, func);
}

static PyObject* is_jit_compiled(PyObject* /* self */, PyObject* func) {
  int st = _PyJIT_IsCompiled(func);
  PyObject* res = NULL;
  if (st == 1) {
    res = Py_True;
  } else if (st == 0) {
    res = Py_False;
  }
  Py_XINCREF(res);
  return res;
}

static PyObject* print_hir(PyObject* /* self */, PyObject* func) {
  if (!PyFunction_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "arg 1 must be a function");
    return NULL;
  }

  int st = _PyJITContext_DidCompile(jit_ctx, func);
  if (st == -1) {
    return NULL;
  } else if (st == 0) {
    PyErr_SetString(PyExc_ValueError, "function is not jit compiled");
    return NULL;
  }

  if (_PyJITContext_PrintHIR(jit_ctx, func) < 0) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}

static PyObject* disassemble(PyObject* /* self */, PyObject* func) {
  if (!PyFunction_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "arg 1 must be a function");
    return NULL;
  }

  int st = _PyJITContext_DidCompile(jit_ctx, func);
  if (st == -1) {
    return NULL;
  } else if (st == 0) {
    PyErr_SetString(PyExc_ValueError, "function is not jit compiled");
    return NULL;
  }

  if (_PyJITContext_Disassemble(jit_ctx, func) < 0) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}

static PyObject* get_jit_list(PyObject* /* self */, PyObject*) {
  if (g_jit_list == nullptr) {
    Py_RETURN_NONE;
  } else {
    auto jit_list = Ref<>::steal(g_jit_list->getList());
    return jit_list.release();
  }
}

static PyObject* get_compiled_functions(PyObject* /* self */, PyObject*) {
  return _PyJITContext_GetCompiledFunctions(jit_ctx);
}

static PyObject* get_compilation_time(PyObject* /* self */, PyObject*) {
  PyObject* res =
      PyLong_FromLong(static_cast<long>(total_compliation_time * 1000));
  return res;
}

static PyObject* get_function_compilation_time(
    PyObject* /* self */,
    PyObject* func) {
  auto iter =
      jit_time_functions.find(reinterpret_cast<PyFunctionObject*>(func));
  if (iter == jit_time_functions.end()) {
    Py_RETURN_NONE;
  }

  PyObject* res = PyLong_FromLong(iter->second.count() * 1000);
  return res;
}

static Ref<> make_deopt_stats() {
  Runtime* runtime = codegen::NativeGenerator::runtime();
  auto stats = Ref<>::steal(PyList_New(0));
  if (stats == nullptr) {
    return nullptr;
  }
  for (auto& pair : runtime->deoptStats()) {
    const DeoptMetadata& meta = runtime->getDeoptMetadata(pair.first);
    const DeoptStat& stat = pair.second;
    CodeRuntime& code_rt = *meta.code_rt;
    BorrowedRef<PyCodeObject> code = code_rt.GetCode();

    auto event = Ref<>::steal(PyDict_New());
    if (event == nullptr) {
      return nullptr;
    }

    auto func_fullname = code->co_qualname;
    int lineno_raw = code->co_lnotab != nullptr
        ? PyCode_Addr2Line(code, meta.next_instr_offset)
        : -1;
    auto lineno = Ref<>::steal(PyLong_FromLong(lineno_raw));
    if (lineno == nullptr) {
      return nullptr;
    }
    auto reason =
        Ref<>::steal(PyUnicode_FromString(deoptReasonName(meta.reason)));
    if (reason == nullptr) {
      return nullptr;
    }
    auto description = Ref<>::steal(PyUnicode_FromString(meta.descr));
    if (description == nullptr) {
      return nullptr;
    }
    if (PyDict_SetItem(event, s_str_func_fullname, func_fullname) < 0 ||
        PyDict_SetItem(event, s_str_filename, code->co_filename) < 0 ||
        PyDict_SetItem(event, s_str_lineno, lineno) < 0 ||
        PyDict_SetItem(event, s_str_reason, reason) < 0 ||
        PyDict_SetItem(event, s_str_description, description) < 0) {
      return nullptr;
    }

    Ref<> event_copy;
    // Helper to copy the event dict and stick in a new count value, reusing
    // the original for the first "copy".
    auto append_copy = [&](size_t count_raw, const char* type) {
      if (event_copy == nullptr) {
        event_copy = std::move(event);
      } else {
        event_copy = Ref<>::steal(PyDict_Copy(event_copy));
        if (event_copy == nullptr) {
          return false;
        }
      }
      auto count = Ref<>::steal(PyLong_FromSize_t(count_raw));
      if (count == nullptr ||
          PyDict_SetItem(event_copy, s_str_count, count) < 0) {
        return false;
      }
      auto type_str = Ref<>::steal(PyUnicode_InternFromString(type));
      if (type_str == nullptr ||
          PyDict_SetItem(event_copy, s_str_guilty_type, type_str) < 0) {
        return false;
      }
      return PyList_Append(stats, event_copy) == 0;
    };

    // For deopts with type profiles, add a copy of the dict with counts for
    // each type, including "other".
    if (!stat.types.empty()) {
      for (size_t i = 0; i < stat.types.size && stat.types.types[i] != nullptr;
           ++i) {
        if (!append_copy(stat.types.counts[i], stat.types.types[i]->tp_name)) {
          return nullptr;
        }
      }
      if (stat.types.other > 0 && !append_copy(stat.types.other, "other")) {
        return nullptr;
      }
    } else if (!append_copy(stat.count, "<none>")) {
      return nullptr;
    }
  }

  runtime->clearDeoptStats();

  return stats;
}

static PyObject* get_and_clear_runtime_stats(PyObject* /* self */, PyObject*) {
  auto stats = Ref<>::steal(PyDict_New());
  if (stats == nullptr) {
    return nullptr;
  }

  Ref<> deopt_stats = make_deopt_stats();
  if (deopt_stats == nullptr) {
    return nullptr;
  }
  if (PyDict_SetItemString(stats, "deopt", deopt_stats) < 0) {
    return nullptr;
  }

  return stats.release();
}

static PyObject* clear_runtime_stats(PyObject* /* self */, PyObject*) {
  codegen::NativeGenerator::runtime()->clearDeoptStats();
  Py_RETURN_NONE;
}

static PyObject* get_compiled_size(PyObject* /* self */, PyObject* func) {
  if (jit_ctx == NULL) {
    return PyLong_FromLong(0);
  }

  long size = _PyJITContext_GetCodeSize(jit_ctx, func);
  PyObject* res = PyLong_FromLong(size);
  return res;
}

static PyObject* get_compiled_stack_size(PyObject* /* self */, PyObject* func) {
  if (jit_ctx == NULL) {
    return PyLong_FromLong(0);
  }

  long size = _PyJITContext_GetStackSize(jit_ctx, func);
  PyObject* res = PyLong_FromLong(size);
  return res;
}

static PyObject* get_compiled_spill_stack_size(
    PyObject* /* self */,
    PyObject* func) {
  if (jit_ctx == NULL) {
    return PyLong_FromLong(0);
  }

  long size = _PyJITContext_GetSpillStackSize(jit_ctx, func);
  PyObject* res = PyLong_FromLong(size);
  return res;
}

static PyObject* jit_frame_mode(PyObject* /* self */, PyObject*) {
  return PyLong_FromLong(jit_config.frame_mode);
}

static PyObject* get_supported_opcodes(PyObject* /* self */, PyObject*) {
  auto set = Ref<>::steal(PySet_New(nullptr));
  if (set == nullptr) {
    return nullptr;
  }

  for (auto op : hir::kSupportedOpcodes) {
    auto op_obj = Ref<>::steal(PyLong_FromLong(op));
    if (op_obj == nullptr) {
      return nullptr;
    }
    if (PySet_Add(set, op_obj) < 0) {
      return nullptr;
    }
  }

  return set.release();
}

static PyObject* jit_force_normal_frame(PyObject*, PyObject* func_obj) {
  if (!PyFunction_Check(func_obj)) {
    PyErr_SetString(PyExc_TypeError, "Input must be a function");
    return NULL;
  }
  PyFunctionObject* func = reinterpret_cast<PyFunctionObject*>(func_obj);

  reinterpret_cast<PyCodeObject*>(func->func_code)->co_flags |= CO_NORMAL_FRAME;

  Py_INCREF(func_obj);
  return func_obj;
}

extern "C" {
PyObject* _PyJIT_GetAndClearCodeInterpCost(void) {
  if (!g_capture_interp_cost) {
    Py_RETURN_NONE;
  }
  PyObject* dict = PyDict_New();
  if (dict == NULL) {
    return NULL;
  }
  for (const auto& entry : g_code_interp_cost) {
    PyDict_SetItemString(
        dict, entry.first.c_str(), PyLong_FromLong(entry.second));
  }
  g_code_interp_cost.clear();
  return dict;
}
}

static PyMethodDef jit_methods[] = {
    {"disable",
     (PyCFunction)(void*)disable_jit,
     METH_FASTCALL,
     "Disable the jit."},
    {"disassemble", disassemble, METH_O, "Disassemble JIT compiled functions"},
    {"is_jit_compiled",
     is_jit_compiled,
     METH_O,
     "Check if a function is jit compiled."},
    {"force_compile",
     force_compile,
     METH_O,
     "Force a function to be JIT compiled if it hasn't yet"},
    {"jit_frame_mode",
     jit_frame_mode,
     METH_NOARGS,
     "Get JIT frame mode (0 = normal frames, 1 = no frames, 2 = shadow frames"},
    {"get_jit_list", get_jit_list, METH_NOARGS, "Get the JIT-list"},
    {"print_hir",
     print_hir,
     METH_O,
     "Print the HIR for a jitted function to stdout."},
    {"get_supported_opcodes",
     get_supported_opcodes,
     METH_NOARGS,
     "Return a set of all supported opcodes, as ints."},
    {"get_compiled_functions",
     get_compiled_functions,
     METH_NOARGS,
     "Return a list of functions that are currently JIT-compiled."},
    {"get_compilation_time",
     get_compilation_time,
     METH_NOARGS,
     "Return the total time used for JIT compiling functions in milliseconds."},
    {"get_function_compilation_time",
     get_function_compilation_time,
     METH_O,
     "Return the time used for JIT compiling a given function in "
     "milliseconds."},
    {"get_and_clear_runtime_stats",
     get_and_clear_runtime_stats,
     METH_NOARGS,
     "Returns information about the runtime behavior of JIT-compiled code."},
    {"clear_runtime_stats",
     clear_runtime_stats,
     METH_NOARGS,
     "Clears runtime stats about JIT-compiled code without returning a value."},
    {"get_compiled_size",
     get_compiled_size,
     METH_O,
     "Return code size in bytes for a JIT-compiled function."},
    {"get_compiled_stack_size",
     get_compiled_stack_size,
     METH_O,
     "Return stack size in bytes for a JIT-compiled function."},
    {"get_compiled_spill_stack_size",
     get_compiled_spill_stack_size,
     METH_O,
     "Return stack size in bytes used for register spills for a JIT-compiled "
     "function."},
    {"jit_force_normal_frame",
     jit_force_normal_frame,
     METH_O,
     "Decorator forcing a function to always use normal frame mode when JIT."},
    {"test_multithreaded_compile",
     test_multithreaded_compile,
     METH_NOARGS,
     "Force multi-threaded recompile of still existing JIT functions for test"},
    {"is_test_multithreaded_compile_enabled",
     is_test_multithreaded_compile_enabled,
     METH_NOARGS,
     "Return True if test_multithreaded_compile mode is enabled"},
    {NULL, NULL, 0, NULL}};

static PyModuleDef jit_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "cinderjit",
    .m_doc = NULL,
    .m_size = -1,
    .m_methods = jit_methods,
    .m_slots = nullptr,
    .m_traverse = nullptr,
    .m_clear = nullptr,
    .m_free = nullptr};

static int onJitListImpl(
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<> module,
    BorrowedRef<> qualname) {
  bool is_static = code->co_flags & CO_STATICALLY_COMPILED;
  if (g_jit_list == nullptr ||
      (is_static && jit_config.compile_all_static_functions)) {
    // There's no jit list or the function is static.
    return 1;
  }
  return g_jit_list->lookup(module, qualname);
}

int _PyJIT_OnJitList(PyFunctionObject* func) {
  return onJitListImpl(func->func_code, func->func_module, func->func_qualname);
}

// Is env var set to a value other than "0" or ""?
int _is_env_truthy(const char* name) {
  const char* val = Py_GETENV(name);
  if (val == NULL || val[0] == '\0' || !strncmp(val, "0", 1)) {
    return 0;
  }
  return 1;
}

int _is_flag_set(const char* xoption, const char* envname) {
  if (PyJIT_IsXOptionSet(xoption) || _is_env_truthy(envname)) {
    return 1;
  }
  return 0;
}

// If the given X option is set and is a string, return it. If not, check the
// given environment variable for a nonempty value and return it if
// found. Otherwise, return nullptr.
const char* flag_string(const char* xoption, const char* envname) {
  PyObject* pyobj = nullptr;
  if (PyJIT_GetXOption(xoption, &pyobj) == 0 && pyobj != nullptr &&
      PyUnicode_Check(pyobj)) {
    return PyUnicode_AsUTF8(pyobj);
  }

  auto envval = Py_GETENV(envname);
  if (envval != nullptr && envval[0] != '\0') {
    return envval;
  }

  return nullptr;
}

long flag_long(const char* xoption, const char* envname, long _default) {
  PyObject* pyobj = nullptr;
  if (PyJIT_GetXOption(xoption, &pyobj) == 0 && pyobj != nullptr &&
      PyUnicode_Check(pyobj)) {
    auto val = Ref<PyObject>::steal(PyLong_FromUnicodeObject(pyobj, 10));
    if (val != nullptr) {
      return PyLong_AsLong(val);
    }
    JIT_LOG("Invalid value for %s: %s", xoption, PyUnicode_AsUTF8(pyobj));
  }

  const char* envval = Py_GETENV(envname);
  if (envval != nullptr && envval[0] != '\0') {
    try {
      return std::stol(envval);
    } catch (std::exception const&) {
      JIT_LOG("Invalid value for %s: %s", envname, envval);
    }
  }

  return _default;
}

int _PyJIT_Initialize() {
  if (jit_config.init_state == JIT_INITIALIZED) {
    return 0;
  }

  int use_jit = 0;

  if (_is_flag_set("jit", "PYTHONJIT")) {
    use_jit = 1;
  }

  // Redirect logging to a file if configured.
  const char* log_filename = flag_string("jit-log-file", "PYTHONJITLOGFILE");
  if (log_filename != nullptr) {
    const char* kPidMarker = "{pid}";
    std::string pid_filename = log_filename;
    auto marker_pos = pid_filename.find(kPidMarker);
    if (marker_pos != std::string::npos) {
      pid_filename.replace(
          marker_pos, std::strlen(kPidMarker), fmt::format("{}", getpid()));
    }
    FILE* file = fopen(pid_filename.c_str(), "w");
    if (file == NULL) {
      JIT_LOG(
          "Couldn't open log file %s (%s), logging to stderr",
          pid_filename,
          strerror(errno));
    } else {
      g_log_file = file;
    }
  }

  if (_is_flag_set("jit-debug", "PYTHONJITDEBUG")) {
    JIT_DLOG("Enabling JIT debug and extra logging.");
    g_debug = 1;
    g_debug_verbose = 1;
  }
  if (_is_flag_set("jit-debug-refcount", "PYTHONJITDEBUGREFCOUNT")) {
    JIT_DLOG("Enabling JIT refcount insertion debug mode.");
    g_debug_refcount = 1;
  }
  if (_is_flag_set("jit-dump-hir", "PYTHONJITDUMPHIR")) {
    JIT_DLOG("Enabling JIT dump-hir mode.");
    g_dump_hir = 1;
  }
  if (_is_flag_set("jit-dump-hir-passes", "PYTHONJITDUMPHIRPASSES")) {
    JIT_DLOG("Enabling JIT dump-hir-passes mode.");
    g_dump_hir_passes = 1;
  }
  if (_is_flag_set("jit-dump-final-hir", "PYTHONJITDUMPFINALHIR")) {
    JIT_DLOG("Enabling JIT dump-final-hir mode.");
    g_dump_final_hir = 1;
  }
  if (_is_flag_set("jit-dump-lir", "PYTHONJITDUMPLIR")) {
    JIT_DLOG("Enable JIT dump-lir mode with origin data.");
    g_dump_lir = 1;
  }
  if (_is_flag_set("jit-dump-lir-no-origin", "PYTHONJITDUMPLIRNOORIGIN")) {
    JIT_DLOG("Enable JIT dump-lir mode without origin data.");
    g_dump_lir = 1;
    g_dump_lir_no_origin = 1;
  }
  if (_is_flag_set("jit-dump-c-helper", "PYTHONJITDUMPCHELPER")) {
    JIT_DLOG("Enable JIT dump-c-helper mode.");
    g_dump_c_helper = 1;
  }
  if (_is_flag_set("jit-disas-funcs", "PYTHONJITDISASFUNCS")) {
    JIT_DLOG("Enabling JIT disas-funcs mode.");
    g_disas_funcs = 1;
  }
  if (_is_flag_set("jit-gdb-support", "PYTHONJITGDBSUPPORT")) {
    JIT_DLOG("Enable GDB support and JIT debug mode.");
    g_debug = 1;
    g_gdb_support = 1;
  }
  if (_is_flag_set("jit-gdb-stubs-support", "PYTHONJITGDBSUPPORT")) {
    JIT_DLOG("Enable GDB support for stubs.");
    g_gdb_stubs_support = 1;
  }
  if (_is_flag_set("jit-gdb-write-elf", "PYTHONJITGDBWRITEELF")) {
    JIT_DLOG("Enable GDB support with ELF output, and JIT debug.");
    g_debug = 1;
    g_gdb_support = 1;
    g_gdb_write_elf_objects = 1;
  }
  if (_is_flag_set("jit-dump-stats", "PYTHONJITDUMPSTATS")) {
    JIT_DLOG("Dumping JIT runtime stats at shutdown.");
    g_dump_stats = 1;
  }

  if (_is_flag_set(
          "jit-enable-jit-list-wildcards", "PYTHONJITENABLEJITLISTWILDCARDS")) {
    JIT_LOG("Enabling wildcards in JIT list");
    jit_config.allow_jit_list_wildcards = 1;
  }
  if (_is_flag_set("jit-all-static-functions", "PYTHONJITALLSTATICFUNCTIONS")) {
    JIT_DLOG("JIT-compiling all static functions");
    jit_config.compile_all_static_functions = 1;
  }

  std::unique_ptr<JITList> jit_list;
  const char* jl_fn = flag_string("jit-list-file", "PYTHONJITLISTFILE");
  if (jl_fn != NULL) {
    use_jit = 1;

    if (jit_config.allow_jit_list_wildcards) {
      jit_list = jit::WildcardJITList::create();
    } else {
      jit_list = jit::JITList::create();
    }
    if (jit_list == nullptr) {
      JIT_LOG("Failed to allocate JIT list");
      return -1;
    }
    if (!jit_list->parseFile(jl_fn)) {
      JIT_LOG("Could not parse jit-list, disabling JIT.");
      return 0;
    }
  }

  if (_is_flag_set("jit-capture-interp-cost", "PYTHONJITCAPTUREINTERPCOST")) {
    if (use_jit) {
      use_jit = 0;
      JIT_LOG("Keeping JIT disabled to capture interpreter cost.");
    }
    g_capture_interp_cost = 1;
    // Hack to help mitigate the cost of tracing during normal production. See
    // ceval.c where the cost counting happens for more details.
    _PyRuntime.ceval.tracing_possible++;
  }

  if (use_jit) {
    JIT_DLOG("Enabling JIT.");
  } else {
    return 0;
  }

  if (_PyJITContext_Init() == -1) {
    JIT_LOG("failed initializing jit context");
    return -1;
  }

  jit_ctx = _PyJITContext_New(std::make_unique<jit::Compiler>());
  if (jit_ctx == NULL) {
    JIT_LOG("failed creating global jit context");
    return -1;
  }

  PyObject* mod = PyModule_Create(&jit_module);
  if (mod == NULL) {
    return -1;
  }

  PyObject* modname = PyUnicode_InternFromString("cinderjit");
  if (modname == NULL) {
    return -1;
  }

  PyObject* modules = PyImport_GetModuleDict();
  int st = _PyImport_FixupExtensionObject(mod, modname, modname, modules);
  Py_DECREF(modname);
  if (st == -1) {
    return -1;
  }

#define INTERN_STR(s)                         \
  s_str_##s = PyUnicode_InternFromString(#s); \
  if (s_str_##s == nullptr) {                 \
    return -1;                                \
  }
  INTERNED_STRINGS(INTERN_STR)
#undef INTERN_STR

  jit_config.init_state = JIT_INITIALIZED;
  jit_config.is_enabled = 1;
  g_jit_list = jit_list.release();
  if (_is_flag_set("jit-no-frame", "PYTHONJITNOFRAME")) {
    jit_config.frame_mode = NO_FRAME;
  }
  if (_is_flag_set("jit-shadow-frame", "PYTHONJITSHADOWFRAME")) {
    jit_config.frame_mode = SHADOW_FRAME;
    _PyThreadState_GetFrame =
        reinterpret_cast<PyThreadFrameGetter>(materializeShadowCallStack);
  }
  jit_config.are_type_slots_enabled = !PyJIT_IsXOptionSet("jit-no-type-slots");
  jit_config.batch_compile_workers =
      flag_long("jit-batch-compile-workers", "PYTHONJITBATCHCOMPILEWORKERS", 0);
  if (_is_flag_set(
          "jit-test-multithreaded-compile",
          "PYTHONJITTESTMULTITHREADEDCOMPILE")) {
    jit_config.test_multithreaded_compile = 1;
  }

  total_compliation_time = 0.0;

  return 0;
}

static std::string key_for_py_code_object(PyCodeObject* code) {
  Py_ssize_t name_len;
  PyObject* py_name = code->co_qualname ? code->co_qualname : code->co_name;
  const char* name = PyUnicode_AsUTF8AndSize(py_name, &name_len);
  Py_ssize_t fn_len;
  const char* fn = PyUnicode_AsUTF8AndSize(code->co_filename, &fn_len);
  return fmt::format(
      "{}@{}:{}",
      std::string{name, static_cast<std::string::size_type>(name_len)},
      std::string{fn, static_cast<std::string::size_type>(fn_len)},
      code->co_firstlineno);
}

static std::unordered_map<PyCodeObject*, std::string> g_code_key_cache_;

void _PyJIT_InvalidateCodeKey(PyCodeObject* code) {
  if (!g_capture_interp_cost) {
    return;
  }
  g_code_key_cache_.erase(code);
}

void _PyJIT_BumpCodeInterpCost(PyCodeObject* code, long cost) {
  std::string key;
  auto key_cache_entry = g_code_key_cache_.find(code);
  if (key_cache_entry == g_code_key_cache_.end()) {
    key = key_for_py_code_object(reinterpret_cast<PyCodeObject*>(code));
    g_code_key_cache_[code] = key;
  } else {
    key = key_cache_entry->second;
  }
  auto entry = g_code_interp_cost.find(key);
  if (entry == g_code_interp_cost.end()) {
    entry = g_code_interp_cost.emplace(key, 0).first;
  }
  entry->second += cost;
}

int _PyJIT_IsEnabled() {
  return (jit_config.init_state == JIT_INITIALIZED) && jit_config.is_enabled;
}

void _PyJIT_AfterFork_Child() {
  perf::afterForkChild();
}

int _PyJIT_AreTypeSlotsEnabled() {
  return (jit_config.init_state == JIT_INITIALIZED) &&
      jit_config.are_type_slots_enabled;
}

int _PyJIT_Enable() {
  if (jit_config.init_state != JIT_INITIALIZED) {
    return 0;
  }
  jit_config.is_enabled = 1;
  return 0;
}

int _PyJIT_EnableTypeSlots() {
  if (!_PyJIT_IsEnabled()) {
    return 0;
  }
  jit_config.are_type_slots_enabled = 1;
  return 1;
}

void _PyJIT_Disable() {
  jit_config.is_enabled = 0;
  jit_config.are_type_slots_enabled = 0;
}

_PyJIT_Result _PyJIT_SpecializeType(
    PyTypeObject* type,
    _PyJIT_TypeSlots* slots) {
  return _PyJITContext_SpecializeType(jit_ctx, type, slots);
}

_PyJIT_Result _PyJIT_CompileFunction(PyFunctionObject* func) {
  // Serialize here as we might have been called re-entrantly.
  ThreadedCompileSerialize guard;

  if (jit_ctx == nullptr) {
    return PYJIT_NOT_INITIALIZED;
  }

  if (!_PyJIT_OnJitList(func)) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  CompilationTimer timer(func);
  jit_reg_units.erase(reinterpret_cast<PyObject*>(func));
  return _PyJITContext_CompileFunction(jit_ctx, func);
}

// Recursively search the given co_consts tuple for any code objects that are
// on the current jit-list, using the given module name to form a
// fully-qualified function name.
static std::vector<BorrowedRef<PyCodeObject>> findNestedCodes(
    BorrowedRef<> module,
    BorrowedRef<> root_consts) {
  std::queue<PyObject*> consts_tuples;
  std::unordered_set<PyCodeObject*> visited;
  std::vector<BorrowedRef<PyCodeObject>> result;

  consts_tuples.push(root_consts);
  while (!consts_tuples.empty()) {
    PyObject* consts = consts_tuples.front();
    consts_tuples.pop();

    for (size_t i = 0, size = PyTuple_GET_SIZE(consts); i < size; ++i) {
      BorrowedRef<PyCodeObject> code = PyTuple_GET_ITEM(consts, i);
      if (!PyCode_Check(code) || !visited.insert(code).second ||
          code->co_qualname == nullptr ||
          !onJitListImpl(code, module, code->co_qualname)) {
        continue;
      }

      result.emplace_back(code);
      consts_tuples.emplace(code->co_consts);
    }
  }

  return result;
}

int _PyJIT_RegisterFunction(PyFunctionObject* func) {
  if (!_PyJIT_IsEnabled()) {
    return 0;
  }

  JIT_CHECK(
      !g_threaded_compile_context.compileRunning(),
      "Not intended for using during threaded compilation");
  int result = 0;
  auto register_unit = [](BorrowedRef<> unit) {
    if (jit_config.test_multithreaded_compile) {
      test_multithreaded_units.emplace_back(unit);
    }
    jit_reg_units.emplace(unit);
  };

  if (_PyJIT_OnJitList(func)) {
    register_unit(reinterpret_cast<PyObject*>(func));
    result = 1;
  }

  // If we have an active jit-list, scan this function's code object for any
  // nested functions that might be on the jit-list, and register them as
  // well.
  if (g_jit_list != nullptr) {
    PyObject* module = func->func_module;
    PyObject* globals = func->func_globals;
    for (auto code : findNestedCodes(
             module,
             reinterpret_cast<PyCodeObject*>(func->func_code)->co_consts)) {
      register_unit(reinterpret_cast<PyObject*>(code.get()));
      jit_code_data.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(code),
          std::forward_as_tuple(module, globals));
    }
  }
  return result;
}

void _PyJIT_UnregisterFunction(PyFunctionObject* func) {
  if (_PyJIT_IsEnabled()) {
    jit_reg_units.erase(reinterpret_cast<PyObject*>(func));
  }
}

void _PyJIT_UnregisterCode(PyCodeObject* code) {
  if (_PyJIT_IsEnabled()) {
    jit_reg_units.erase(reinterpret_cast<PyObject*>(code));
    jit_code_data.erase(code);
  }
}

static void dump_jit_stats() {
  auto stats = get_and_clear_runtime_stats(nullptr, nullptr);
  if (stats == nullptr) {
    return;
  }
  auto stats_str = PyObject_Str(stats);
  if (stats_str == nullptr) {
    return;
  }

  JIT_LOG("JIT runtime stats:\n%s", PyUnicode_AsUTF8(stats_str));
}

int _PyJIT_Finalize() {
  if (g_dump_stats) {
    dump_jit_stats();
  }

  // Always release references from Runtime objects: C++ clients may have
  // invoked the JIT directly without initializing a full _PyJITContext.
  jit::codegen::NativeGenerator::runtime()->releaseReferences();
  jit::codegen::NativeGenerator::runtime()->clearDeoptStats();

  if (jit_config.init_state != JIT_INITIALIZED) {
    return 0;
  }

  delete g_jit_list;
  g_jit_list = nullptr;

  jit_config.init_state = JIT_FINALIZED;

  JIT_CHECK(jit_ctx != NULL, "jit_ctx not initialized");
  Py_CLEAR(jit_ctx);

#define CLEAR_STR(s) Py_CLEAR(s_str_##s);
  INTERNED_STRINGS(CLEAR_STR)
#undef CLEAR_STR

  return 0;
}

int _PyJIT_NoFrame() {
  return jit_config.frame_mode == NO_FRAME;
}

int _PyJIT_ShadowFrame() {
  return jit_config.frame_mode == SHADOW_FRAME;
}

PyObject* _PyJIT_GenSend(
    PyGenObject* gen,
    PyObject* arg,
    int exc,
    PyFrameObject* f,
    PyThreadState* tstate,
    int finish_yield_from) {
  auto gen_footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);

  // state should be valid and the generator should not be completed
  JIT_DCHECK(
      gen_footer->state == _PyJitGenState_JustStarted ||
          gen_footer->state == _PyJitGenState_Running,
      "Invalid JIT generator state");

  gen_footer->state = _PyJitGenState_Running;

  // JIT generators use NULL arg to indicate an exception
  if (exc) {
    JIT_DCHECK(
        arg == Py_None, "Arg should be None when injecting an exception");
    Py_DECREF(arg);
    arg = NULL;
  } else {
    if (arg == NULL) {
      arg = Py_None;
      Py_INCREF(arg);
    }
  }

  if (f) {
    // Setup tstate/frame as would be done in PyEval_EvalFrameEx() or
    // prologue of a JITed function.
    tstate->frame = f;
    f->f_executing = 1;
    // This compensates for the decref which occurs in JITRT_UnlinkFrame().
    Py_INCREF(f);
    // This satisfies code which uses f_lasti == -1 or < 0 to check if a
    // generator is not yet started, but still provides a garbage value in case
    // anything tries to actually use f_lasti.
    f->f_lasti = std::numeric_limits<int>::max();
  }

  // Enter generated code.
  JIT_DCHECK(
      gen_footer->yieldPoint != nullptr,
      "Attempting to resume a generator with no yield point");
  PyObject* result =
      gen_footer->resumeEntry((PyObject*)gen, arg, tstate, finish_yield_from);

  if (!result && (gen->gi_jit_data != nullptr)) {
    // Generator jit data (gen_footer) will be freed if the generator
    // deopts
    gen_footer->state = _PyJitGenState_Completed;
  }

  return result;
}

PyFrameObject* _PyJIT_GenMaterializeFrame(PyGenObject* gen) {
  if (gen->gi_frame) {
    PyFrameObject* frame = gen->gi_frame;
    Py_INCREF(frame);
    return frame;
  }
  PyThreadState* tstate = PyThreadState_Get();
  if (gen->gi_running) {
    PyFrameObject* frame = jit::materializePyFrameForGen(tstate, gen);
    Py_INCREF(frame);
    return frame;
  }
  auto gen_footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);
  if (gen_footer->state == _PyJitGenState_Completed) {
    return nullptr;
  }
  jit::Runtime* rt = jit::codegen::NativeGenerator::runtime();
  jit::CodeRuntime* code_rt = rt->getCodeRuntime(gen_footer->code_rt_id);
  PyFrameObject* frame =
      PyFrame_New(tstate, code_rt->GetCode(), code_rt->GetGlobals(), nullptr);
  JIT_CHECK(frame != nullptr, "failed allocating frame");
  // PyFrame_New links the frame into the thread stack.
  Py_CLEAR(frame->f_back);
  frame->f_gen = reinterpret_cast<PyObject*>(gen);
  Py_INCREF(frame);
  gen->gi_frame = frame;
  return frame;
}

int _PyJIT_GenVisitRefs(PyGenObject* gen, visitproc visit, void* arg) {
  auto gen_footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);
  JIT_DCHECK(gen_footer, "Generator missing JIT data");
  if (gen_footer->state != _PyJitGenState_Completed && gen_footer->yieldPoint) {
    return reinterpret_cast<GenYieldPoint*>(gen_footer->yieldPoint)
        ->visitRefs(gen, visit, arg);
  }
  return 0;
}

void _PyJIT_GenDealloc(PyGenObject* gen) {
  auto gen_footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);
  JIT_DCHECK(gen_footer, "Generator missing JIT data");
  if (gen_footer->state != _PyJitGenState_Completed && gen_footer->yieldPoint) {
    reinterpret_cast<GenYieldPoint*>(gen_footer->yieldPoint)->releaseRefs(gen);
  }
  JITRT_GenJitDataFree(gen);
}

PyObject* _PyJIT_GenYieldFromValue(PyGenObject* gen) {
  auto gen_footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);
  JIT_DCHECK(gen_footer, "Generator missing JIT data");
  PyObject* yf = NULL;
  if (gen_footer->state != _PyJitGenState_Completed && gen_footer->yieldPoint) {
    yf = gen_footer->yieldPoint->yieldFromValue(gen_footer);
    Py_XINCREF(yf);
  }
  return yf;
}

PyObject* _PyJIT_GetGlobals(PyThreadState* tstate) {
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;
  if (shadow_frame == nullptr) {
    JIT_CHECK(
        tstate->frame == nullptr,
        "py frame w/out corresponding shadow frame\n");
    return nullptr;
  }
  if (shadow_frame->has_pyframe) {
    return tstate->frame->f_globals;
  }
  jit::Runtime* rt = jit::codegen::NativeGenerator::runtime();
  jit::CodeRuntime* code_rt = rt->getCodeRuntime(shadow_frame->code_rt_id);
  return code_rt->GetGlobals();
}
