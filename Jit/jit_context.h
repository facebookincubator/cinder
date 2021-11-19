// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include "Jit/compiler.h"
#include "Jit/containers.h"
#include "Jit/hir/preload.h"
#include "Jit/pyjit_result.h"
#include "Jit/pyjit_typeslots.h"
#include "Jit/ref.h"
#include "Jit/slot_gen.h"
#include "Jit/util.h"

#include <functional>
#include <memory>
#include <vector>

// Lookup key for _PyJITContext::compiled_codes: a code object and a globals
// dict it was JIT-compiled with.
struct CompilationKey {
  // These are both weak references; the values are kept alive by strong
  // references in the corresponding jit::CodeRuntime.
  PyObject* code;
  PyObject* globals;

  CompilationKey(PyObject* code, PyObject* globals)
      : code(code), globals(globals) {}

  bool operator==(const CompilationKey& other) const {
    return code == other.code && globals == other.globals;
  }
};

template <>
struct std::hash<CompilationKey> {
  std::size_t operator()(const CompilationKey& key) const {
    std::hash<PyObject*> hasher;
    return jit::combineHash(hasher(key.code), hasher(key.globals));
  }
};

/* Deoptimization information for a compiled type. */
struct TypeDeoptInfo {
  explicit TypeDeoptInfo(PyTypeObject* type)
      : orig_tp_call{type->tp_call},
        orig_tp_init{type->tp_init},
        orig_tp_repr{type->tp_repr},
        orig_tp_str{type->tp_str},
        orig_tp_getattro{type->tp_getattro},
        orig_tp_descr_get{type->tp_descr_get} {}

  // Original values for compiled type objects.
  ternaryfunc orig_tp_call;
  initproc orig_tp_init;
  reprfunc orig_tp_repr;
  reprfunc orig_tp_str;
  getattrofunc orig_tp_getattro;
  descrgetfunc orig_tp_descr_get;
};

/*
 * A JIT context encapsulates all the state managed by an instance of the JIT.
 */
struct _PyJITContext {
  ~_PyJITContext();

  jit::SlotGen slot_gen;

  /* General purpose jit compiler */
  jit::Compiler jit_compiler;

  /*
   * Map from compiled types to deoptimization information.
   */
  jit::UnorderedMap<BorrowedRef<PyTypeObject>, TypeDeoptInfo> type_deopt;

  /*
   * Set of which functions have JIT-compiled entrypoints.
   */
  jit::UnorderedSet<BorrowedRef<PyFunctionObject>> compiled_funcs;

  /*
   * Compiled code objects, keyed by PyCodeObject* and the globals dict they
   * were compiled with.
   */
  jit::UnorderedMap<CompilationKey, std::unique_ptr<jit::CompiledFunction>>
      compiled_codes;

  /*
   * Code which is being kept alive in case it was in use when
   * _PyJITContext_ClearCache was called. Only intended to be used during
   * multithreaded_compile_test.
   */
  std::vector<std::unique_ptr<jit::CompiledFunction>> orphaned_compiled_codes;
};

/*
 * Clear cache of compiled code such that subsequent compilations are always
 * full rather than just re-binding pre-compiled code. Only intended to be used
 * during multithreaded_compile_test.
 */
void _PyJITContext_ClearCache(_PyJITContext* ctx);

/*
 * Generate specialized functions for type object slots. Calls the other
 * _PyJITContext_Specialize* functions and handles setting up deoptimization
 * support.
 *
 * Returns PYJIT_RESULT_OK on success.
 */
_PyJIT_Result _PyJITContext_SpecializeType(
    _PyJITContext* ctx,
    BorrowedRef<PyTypeObject> type,
    _PyJIT_TypeSlots* slots);

/*
 * JIT compile func and patch its entry point.
 *
 * On success, positional only calls to func will use the JIT compiled version.
 *
 * Returns PYJIT_RESULT_OK on success, or if the function was already compiled.
 */
_PyJIT_Result _PyJITContext_CompileFunction(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * JIT compile code and store the result in ctx.
 *
 * This does not patch the entry point of any functions; it is primarily useful
 * to pre-compile the code object for a nested function so it's available for
 * use after disabling the JIT.
 *
 * Returns 1 on success, 0 on failure.
 */
_PyJIT_Result _PyJITContext_CompileCode(
    _PyJITContext* ctx,
    BorrowedRef<> module,
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<PyDictObject> globals);

/*
 * JIT compile function/code-object from Preloader.
 *
 * Patches func entrypoint if Preloader contains a func.
 */
_PyJIT_Result _PyJITContext_CompilePreloader(
    _PyJITContext* ctx,
    const jit::hir::Preloader& preloader);

/*
 * Attach already-compiled code to the given function, if it exists.
 *
 * Intended for (but not limited to) use with nested functions after the JIT is
 * disabled.
 *
 * Returns PYJIT_RESULT_OK on success or if the given function already had
 * compiled code attached.
 */
_PyJIT_Result _PyJITContext_AttachCompiledCode(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Callbacks invoked by the runtime when a PyFunctionObject is modified or
 * destroyed.
 */
void _PyJITContext_FuncModified(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);
void _PyJITContext_FuncDestroyed(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Callbacks invoked by the runtime when a PyTypeObject is modified or
 * destroyed.
 */
void _PyJITContext_TypeModified(
    _PyJITContext* ctx,
    BorrowedRef<PyTypeObject> type);
void _PyJITContext_TypeDestroyed(
    _PyJITContext* ctx,
    BorrowedRef<PyTypeObject> type);

/*
 * Return whether or not this context compiled the supplied function.
 *
 * Return 1 if so, 0 if not, and -1 if an error occurred.
 */
int _PyJITContext_DidCompile(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Returns the code size in bytes for a specified JIT-compiled function.
 *
 * Returns 0 if the function is not JIT-compiled.
 *
 * Returns -1 if an error occurred.
 */

int _PyJITContext_GetCodeSize(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Returns the stack size in bytes for a specified JIT-compiled function.
 *
 * Returns -1 if an error occurred.
 */

int _PyJITContext_GetStackSize(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Returns the stack size used for spills in bytes for a specified JIT-compiled
 * function.
 *
 * Returns -1 if an error occurred.
 */
int _PyJITContext_GetSpillStackSize(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Return a list of functions that are currently JIT-compiled.
 *
 * Returns a new reference.
 *
 */
PyObject* _PyJITContext_GetCompiledFunctions(_PyJITContext* ctx);

/*
 * Print the HIR for func to stdout if it was JIT-compiled.
 * This function is a no-op if func was not JIT-compiled.
 *
 * Returns -1 if an error occurred or 0 otherwise.
 */
int _PyJITContext_PrintHIR(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Print the disassembled code for func to stdout if it was JIT-compiled.
 * This function is a no-op if func was not JIT-compiled.
 *
 * Returns -1 if an error occurred or 0 otherwise.
 */
int _PyJITContext_Disassemble(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);
