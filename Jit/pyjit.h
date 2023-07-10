// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "frameobject.h"
#include "genobject.h"

#include "Jit/pyjit_result.h"
#include "Jit/pyjit_typeslots.h"

#ifdef __cplusplus
#include "Jit/hir/preload.h"
#endif

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif

enum InitStateJitConfig { JIT_NOT_INITIALIZED, JIT_INITIALIZED, JIT_FINALIZED };
enum FrameModeJitConfig { PY_FRAME = 0, SHADOW_FRAME };

/* Accessors for internal jit config */
PyAPI_FUNC(int) _PyJIT_IsJitConfigAllow_jit_list_wildcards(void);
PyAPI_FUNC(int) _PyJIT_IsJitConfigCompile_all_static_functions(void);
PyAPI_FUNC(size_t) _PyJIT_GetJitConfigBatch_compile_workers(void);
PyAPI_FUNC(int) _PyJIT_IsJitConfigMultithreaded_compile_test(void);

/*
 * This defines the global public API for the JIT that is consumed by the
 * runtime.
 *
 * These methods assume that the GIL is held unless it is explicitly stated
 * otherwise.
 */

/*
 * Initialize any global state required by the JIT.
 *
 * This must be called before attempting to use the JIT.
 *
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) _PyJIT_Initialize(void);

/*
 * Initialize per-subinterpreter JIT state.
 *
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) _PyJIT_InitializeSubInterp(void);

/*
 * Enable the global JIT.
 *
 * _PyJIT_Initialize must be called before calling this.
 *
 * Returns 1 if the JIT is enabled and 0 otherwise.
 */
PyAPI_FUNC(int) _PyJIT_Enable(void);

/*
 * Disable the global JIT.
 */
PyAPI_FUNC(void) _PyJIT_Disable(void);

/*
 * Returns 1 if JIT compilation is enabled and 0 otherwise.
 */
PyAPI_FUNC(int) _PyJIT_IsEnabled(void);

/*
 * Returns 1 if the JIT was ever successfully initialized, 0 otherwise.
 *
 * This could be 1 when _PyJIT_IsEnabled() is 0, if the JIT has since been
 * disabled.
 */
PyAPI_FUNC(int) _PyJIT_IsInitialized(void);

/*
 * After-fork callback for child processes. Performs any cleanup necessary for
 * per-process state, including handling of Linux perf pid maps.
 */
PyAPI_FUNC(void) _PyJIT_AfterFork_Child(void);

/*
 * Returns 1 if auto-JIT is enabled and 0 otherwise.
 */
PyAPI_FUNC(int) _PyJIT_IsAutoJITEnabled(void);

/*
 * Returns the threshold if auto-JIT is enabled and 0 otherwise.
 */
PyAPI_FUNC(unsigned int) _PyJIT_AutoJITThreshold(void);

/*
   Enable the HIR inliner.
 */
PyAPI_FUNC(void) _PyJIT_EnableHIRInliner(void);

/*
   Disable the HIR inliner.
 */
PyAPI_FUNC(void) _PyJIT_DisableHIRInliner(void);

/*
 * Returns 1 if the HIR inliner is enabled and 0 otherwise.
 */
PyAPI_FUNC(int) _PyJIT_IsHIRInlinerEnabled(void);

/*
 * Returns 1 if the JIT will split code emission across multiple sections and 0
 * otherwise.
 */
PyAPI_FUNC(int) _PyJIT_MultipleCodeSectionsEnabled(void);

/*
 * Returns the size (in bytes) of the hot code section. Returns 0 if multiple
 * code sections aren't enabled.
 */
PyAPI_FUNC(int) _PyJIT_HotCodeSectionSize(void);

/*
 * Returns the size (in bytes) of the hot code section. Returns 0 if multiple
 * code sections aren't enabled.
 */
PyAPI_FUNC(int) _PyJIT_ColdCodeSectionSize(void);

/*
 * JIT compile func and patch its entry point.
 *
 * On success, positional only calls to func will use the JIT compiled version.
 *
 * Returns PYJIT_RESULT_OK on success.
 */
PyAPI_FUNC(_PyJIT_Result) _PyJIT_CompileFunction(PyFunctionObject* func);

/*
 * Registers a function with the JIT to be compiled in the future.
 *
 * The JIT will still be informed by _PyJIT_CompileFunction before the
 * function executes for the first time.  The JIT can choose to compile
 * the function at some future point.  Currently the JIT will compile
 * the function before it shuts down to make sure all eligable functions
 * were compiled.
 *
 * The JIT will not keep the function alive, instead it will be informed
 * that the function is being de-allocated via _PyJIT_UnregisterFunction
 * before the function goes away.
 *
 * Returns 1 if the function is registered with JIT or is already compiled,
 * and 0 otherwise.
 */
PyAPI_FUNC(int) _PyJIT_RegisterFunction(PyFunctionObject* func);

/*
 * Watch or unwatch a dictionary.
 */
PyAPI_FUNC(void) _PyJIT_WatchDict(PyObject* dict);
PyAPI_FUNC(void) _PyJIT_UnwatchDict(PyObject* dict);

/*
 * Dict watcher callback; called on modifications to any watched dict.
 */
PyAPI_FUNC(int) _PyJIT_DictWatcher(
    PyDict_WatchEvent event,
    PyObject* dict,
    PyObject* key,
    PyObject* new_value);

/*
 * Code watcher callback; called on creation/dealloc of a code object.
 */
PyAPI_FUNC(int) _PyJIT_CodeWatcher(PyCodeEvent event, PyCodeObject* co);

/*
 * Func watcher callback; called on func creation/modification/deallocation.
 */
PyAPI_FUNC(int) _PyJIT_FuncWatcher(
    PyFunction_WatchEvent event,
    PyFunctionObject* func,
    PyObject* new_value);

/*
 * Informs the JIT that a type, function, or code object is being created,
 * modified, or destroyed.
 */
PyAPI_FUNC(void) _PyJIT_TypeCreated(PyTypeObject* type);
PyAPI_FUNC(void) _PyJIT_TypeModified(PyTypeObject* type);
PyAPI_FUNC(void) _PyJIT_TypeNameModified(PyTypeObject* type);
PyAPI_FUNC(void) _PyJIT_TypeDestroyed(PyTypeObject* type);
PyAPI_FUNC(void) _PyJIT_FuncModified(PyFunctionObject* func);
PyAPI_FUNC(void) _PyJIT_FuncDestroyed(PyFunctionObject* func);
PyAPI_FUNC(void) _PyJIT_CodeDestroyed(PyCodeObject* code);

/*
 * Informs the JIT that an instance has had an assignment to its __class__
 * field.
 */
PyAPI_FUNC(void)
    _PyJIT_InstanceTypeAssigned(PyTypeObject* old_ty, PyTypeObject* new_ty);

/*
 * Clean up any resources allocated by the JIT.
 *
 * This is intended to be called at interpreter shutdown in Py_Finalize.
 *
 * Returns 0 on success or -1 on error.
 */
PyAPI_FUNC(int) _PyJIT_Finalize(void);

/*
 * Returns whether the function specified in `func` is on the jit-list.
 *
 * Returns 0 if the given function is not on the jit-list, and non-zero
 * otherwise.
 *
 * Always returns 1 if the JIT list is not specified.
 */
PyAPI_FUNC(int) _PyJIT_OnJitList(PyFunctionObject* func);

/*
 * Returns a boolean indicating whether or not jitted functions should use a
 * shadow frame object by default instead of a full PyFrameObject.
 */
PyAPI_FUNC(int) _PyJIT_ShadowFrame(void);

/* Dict-watching callbacks, invoked by dictobject.c when appropriate. */

/*
 * Called when the value at a key is modified (value will contain the new
 * value) or deleted (value will be NULL).
 */
PyAPI_FUNC(void)
    _PyJIT_NotifyDictKey(PyObject* dict, PyObject* key, PyObject* value);

/*
 * Called when a dict is cleared, rather than sending individual notifications
 * for every key. The dict is still in a watched state, and further callbacks
 * for it will be invoked as appropriate.
 */
PyAPI_FUNC(void) _PyJIT_NotifyDictClear(PyObject* dict);

/*
 * Called when a dict has changed in a way that is incompatible with watching,
 * or is about to be freed. No more callbacks will be invoked for this dict.
 */
PyAPI_FUNC(void) _PyJIT_NotifyDictUnwatch(PyObject* dict);

/*
 * Gets the global cache for the given builtins and globals dictionaries and
 * key.  The global that is pointed to will automatically be updated as
 * builtins and globals change.  The value that is pointed to will be NULL if
 * the dictionaries can no longer be tracked or if the value is no longer
 * defined, in which case the dictionaries need to be consulted.  This will
 * return NULL if the required tracking cannot be initialized.
 */
PyAPI_FUNC(PyObject**)
    _PyJIT_GetGlobalCache(PyObject* builtins, PyObject* globals, PyObject* key);

/*
 * Gets the cache for the given dictionary and key.  The value that is pointed
 * to will automatically be updated as the dictionary changes.  The value that
 * is pointed to will be NULL if the dictionaries can no longer be tracked or if
 * the value is no longer defined, in which case the dictionaries need to be
 * consulted.  This will return NULL if the required tracking cannot be
 * initialized.
 */
PyAPI_FUNC(PyObject**) _PyJIT_GetDictCache(PyObject* dict, PyObject* key);

/*
 * Clears internal caches associated with the JIT.  This may cause a degradation
 * of performance and is only intended for use for detecting memory leaks.
 */
PyAPI_FUNC(void) _PyJIT_ClearDictCaches(void);

/*
 * Send into/resume a suspended JIT generator and return the result.
 */
PyAPI_FUNC(PyObject*) _PyJIT_GenSend(
    PyGenObject* gen,
    PyObject* arg,
    int exc,
    PyFrameObject* f,
    PyThreadState* tstate,
    int finish_yield_from);

/*
 * Materialize the frame for gen. Returns a borrowed reference.
 */
PyAPI_FUNC(PyFrameObject*) _PyJIT_GenMaterializeFrame(PyGenObject* gen);

/*
 * Visit owned references in a JIT-backed generator object.
 */
PyAPI_FUNC(int)
    _PyJIT_GenVisitRefs(PyGenObject* gen, visitproc visit, void* arg);

/*
 * Release any JIT-related data in a PyGenObject.
 */
PyAPI_FUNC(void) _PyJIT_GenDealloc(PyGenObject* gen);

/* Enum shared with the JIT to communicate the current state of a generator.
 * These should be queried via the utility functions below. These may use some
 * other features to determine the overall state of a JIT generator. Notably
 * the yield-point field being null indicates execution is currently active when
 * used in combination with the less specific Ci_JITGenState_Running state.
 */
typedef enum {
  /* Generator has freshly been returned from a call to the function itself.
     Execution of user code has not yet begun. */
  Ci_JITGenState_JustStarted,
  /* Execution is in progress and is currently active or the generator is
     suspended. */
  Ci_JITGenState_Running,
  /* Generator has completed execution and should not be resumed again. */
  Ci_JITGenState_Completed,
  /* An exception/close request is being processed.  */
  Ci_JITGenState_Throwing,
} CiJITGenState;

/* Functions for extracting the state for generator objects known to be JIT
 * controlled. Implemented as inline functions using hard-coded offsets for
 * speed in C code which don't have access to C++ types.
 */

/* Offsets for fields in jit::GenFooterData for fast access from C code.
 * These values are verified by static_assert in runtime.h. */
#define Ci_GEN_JIT_DATA_OFFSET_STATE 32
#define Ci_GEN_JIT_DATA_OFFSET_YIELD_POINT 24

static inline CiJITGenState Ci_GetJITGenState(PyGenObject* gen) {
  return (
      (CiJITGenState)((char*)gen->gi_jit_data)[Ci_GEN_JIT_DATA_OFFSET_STATE]);
}

static inline int Ci_JITGenIsExecuting(PyGenObject* gen) {
  return (Ci_GetJITGenState(gen) == Ci_JITGenState_Running &&
          !(*(uint64_t*)((uintptr_t)gen->gi_jit_data + Ci_GEN_JIT_DATA_OFFSET_YIELD_POINT))) ||
      Ci_GetJITGenState(gen) == Ci_JITGenState_Throwing;
}

static inline int Ci_JITGenIsRunnable(PyGenObject* gen) {
  return Ci_GetJITGenState(gen) == Ci_JITGenState_JustStarted ||
      (Ci_GetJITGenState(gen) == Ci_JITGenState_Running &&
       (*(uint64_t*)((uintptr_t)gen->gi_jit_data + Ci_GEN_JIT_DATA_OFFSET_YIELD_POINT)));
}

static inline void Ci_SetJITGenState(PyGenObject* gen, CiJITGenState state) {
  *((CiJITGenState*)((char*)gen->gi_jit_data + Ci_GEN_JIT_DATA_OFFSET_STATE)) =
      state;
}

static inline void Ci_MarkJITGenCompleted(PyGenObject* gen) {
  Ci_SetJITGenState(gen, Ci_JITGenState_Completed);
}

static inline void Ci_MarkJITGenThrowing(PyGenObject* gen) {
  Ci_SetJITGenState(gen, Ci_JITGenState_Throwing);
}

/* Functions similar to the ones above but also work for generators which may
 * not be JIT controlled.
 */

static inline int Ci_GenIsCompleted(PyGenObject* gen) {
  if (gen->gi_jit_data) {
    return Ci_GetJITGenState(gen) == Ci_JITGenState_Completed;
  }
  return gen->gi_frame == NULL || _PyFrameHasCompleted(gen->gi_frame);
}

static inline int Ci_GenIsJustStarted(PyGenObject* gen) {
  if (gen->gi_jit_data) {
    return Ci_GetJITGenState(gen) == Ci_JITGenState_JustStarted;
  }
  return gen->gi_frame && gen->gi_frame->f_lasti == -1;
}

static inline int Ci_GenIsExecuting(PyGenObject* gen) {
  if (gen->gi_jit_data) {
    return Ci_JITGenIsExecuting(gen);
  }
  return gen->gi_frame != NULL && _PyFrame_IsExecuting(gen->gi_frame);
}

static inline int Ci_GenIsRunnable(PyGenObject* gen) {
  if (gen->gi_jit_data) {
    return Ci_JITGenIsRunnable(gen);
  }
  return gen->gi_frame != NULL && _PyFrame_IsRunnable(gen->gi_frame);
}

/*
 * Return current sub-iterator from JIT generator or NULL if there is none.
 */
PyAPI_FUNC(PyObject*) _PyJIT_GenYieldFromValue(PyGenObject* gen);

/*
 * Specifies the offset from a JITed function entry point where the re-entry
 * point for calling with the correct bound args lives */
#define JITRT_CALL_REENTRY_OFFSET (-6)

/*
 * Fixes the JITed function entry point up to be the re-entry point after
 * binding the args */
#define JITRT_GET_REENTRY(entry) \
  ((vectorcallfunc)(((char*)entry) + JITRT_CALL_REENTRY_OFFSET))

/*
 * Specifies the offset from a JITed function entry point where the static
 * entry point lives */
#define JITRT_STATIC_ENTRY_OFFSET (-11)

/*
 * Fixes the JITed function entry point up to be the static entry point after
 * binding the args */
#define JITRT_GET_STATIC_ENTRY(entry) \
  ((vectorcallfunc)(((char*)entry) + JITRT_STATIC_ENTRY_OFFSET))

/*
 * Checks if the given function is JITed.

 * Returns 1 if the function is JITed, 0 if not.
 */
PyAPI_FUNC(int) _PyJIT_IsCompiled(PyObject* func);

/*
 * Returns a borrowed reference to the globals for the top-most Python function
 * associated with tstate.
 */
PyAPI_FUNC(PyObject*) _PyJIT_GetGlobals(PyThreadState* tstate);

/*
 * Returns a borrowed reference to the builtins for the top-most Python function
 * associated with tstate.
 */
PyAPI_FUNC(PyObject*) _PyJIT_GetBuiltins(PyThreadState* tstate);

/*
 * Record a type profile for the current instruction.
 */
PyAPI_FUNC(void) _PyJIT_ProfileCurrentInstr(
    PyFrameObject* frame,
    PyObject** stack_top,
    int opcode,
    int oparg);

/*
 * Record profiled instructions for the given code object upon exit from a
 * frame, some of which may not have had their types recorded.
 */
PyAPI_FUNC(void)
    _PyJIT_CountProfiledInstrs(PyCodeObject* code, Py_ssize_t count);

/*
 * Get and clear, or just clear, information about the recorded type profiles.
 */
PyAPI_FUNC(PyObject*) _PyJIT_GetAndClearTypeProfiles(void);
PyAPI_FUNC(void) _PyJIT_ClearTypeProfiles(void);

/*
 * Notify the JIT that type has been modified.
 */
PyAPI_FUNC(void) _PyJIT_TypeModified(PyTypeObject* type);

/*
 * Returns a borrowed reference to the top-most frame of tstate.
 *
 * When shadow frame mode is active, calling this function will materialize
 * PyFrameObjects for any jitted functions on the call stack.
 */
PyAPI_FUNC(PyFrameObject*) _PyJIT_GetFrame(PyThreadState* tstate);

/*
 * Set output format for function disassembly. E.g. with -X jit-disas-funcs.
 */
PyAPI_FUNC(void) _PyJIT_SetDisassemblySyntaxATT(void);
PyAPI_FUNC(int) _PyJIT_IsDisassemblySyntaxIntel(void);

PyAPI_FUNC(void) _PyJIT_SetProfileNewInterpThreads(int);
PyAPI_FUNC(int) _PyJIT_GetProfileNewInterpThreads(void);

#ifdef __cplusplus
bool _PyJIT_UseHugePages();
} /* extern "C" */
#endif

#ifdef __cplusplus
namespace jit {
bool isPreloaded(BorrowedRef<PyFunctionObject> func);
const hir::Preloader& getPreloader(BorrowedRef<PyFunctionObject> func);
} // namespace jit
#endif

#endif /* Py_LIMITED_API */
