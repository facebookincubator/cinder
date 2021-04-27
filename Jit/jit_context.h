// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef Py_JIT_CONTEXT_H
#define Py_JIT_CONTEXT_H

#include "Python.h"

#include "Jit/code_gen.h"
#include "Jit/compiler.h"
#include "Jit/pyjit_result.h"
#include "Jit/pyjit_typeslots.h"
#include "Jit/util.h"

#include <functional>
#include <memory>

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

/*
 * A JIT context encapsulates all the state managed by an instance of the
 * JIT.
 */
struct _PyJITContext {
  PyObject_HEAD;

  /*
   * Code generator used by targeted, one off code generators
   */
  CodeGen* code_gen;

  /* General purpose jit compiler; may be null */
  std::unique_ptr<jit::Compiler> jit_compiler;

  /*
   * A dictionary containing deoptimization information for compiled objects.
   *
   * Keys are weak references to either function or type objects.
   * Values are instances of DeoptInfo.
   */
  PyObject* deopt_info;

  /*
   * Compiled code objects, keyed by PyCodeObject* and the globals dict they
   * were compiled with.
   */
  std::unordered_map<CompilationKey, std::unique_ptr<jit::CompiledFunction>>
      compiled_codes;

  PyObject* weakreflist;
};

/*
 * Register the _PyJITContext type with the runtime. This needs to be called
 * before using the type.
 */
int _PyJITContext_Init(void);

/*
 * Allocate a new JIT context.
 *
 * compiler is a handle to a general purpose JIT compiler.
 *
 * Returns a new reference. Any compiled objects will be deoptimized when the
 * _PyJITContext is destroyed; callers must ensure it is kept alive as long as
 * necessary.
 */
_PyJITContext* _PyJITContext_New(std::unique_ptr<jit::Compiler> compiler);

/*
 * Generate specialized functions for type object slots. Calls the other
 * _PyJITContext_Specialize* functions and handles setting up deoptimization
 * support.
 *
 * Returns PYJIT_RESULT_OK on success.
 */
_PyJIT_Result _PyJITContext_SpecializeType(
    BorrowedRef<_PyJITContext> ctx,
    PyTypeObject* type,
    _PyJIT_TypeSlots* slots);

/*
 * JIT compile func and patch its entry point.
 *
 * On success, positional only calls to func will use the JIT compiled version.
 *
 * Returns PYJIT_RESULT_OK on success.
 */
_PyJIT_Result _PyJITContext_CompileFunction(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func);

/*
 * Return whether or not this context compiled the supplied function.jit_
 *
 * Return 1 if so, 0 if not, and -1 if an error occurred.
 */
int _PyJITContext_DidCompile(_PyJITContext* ctx, PyObject* func);

/*
 * Returns the code size in bytes for a specified JIT-compiled function.
 *
 * Returns 0 if the function is not JIT-compiled.
 *
 * Returns -1 if an error occurred.
 */

int _PyJITContext_GetCodeSize(_PyJITContext* ctx, PyObject* func);

/*
 * Returns the stack size in bytes for a specified JIT-compiled function.
 *
 * Returns -1 if an error occurred.
 */

int _PyJITContext_GetStackSize(_PyJITContext* ctx, PyObject* func);

/*
 * Returns the stack size used for spills in bytes for a specified JIT-compiled
 * function.
 *
 * Returns -1 if an error occurred.
 */
int _PyJITContext_GetSpillStackSize(_PyJITContext* ctx, PyObject* func);

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
int _PyJITContext_PrintHIR(_PyJITContext* ctx, PyObject* func);

/*
 * Print the disassembled code for func to stdout if it was JIT-compiled.
 * This function is a no-op if func was not JIT-compiled.
 *
 * Returns -1 if an error occurred or 0 otherwise.
 */
int _PyJITContext_Disassemble(_PyJITContext* ctx, PyObject* func);

#endif /* Py_JIT_H */
