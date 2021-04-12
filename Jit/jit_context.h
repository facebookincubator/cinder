#ifndef Py_JIT_CONTEXT_H
#define Py_JIT_CONTEXT_H

#include "Python.h"

#include "Jit/code_allocator.h"
#include "Jit/code_gen.h"
#include "Jit/compiler.h"
#include "Jit/pyjit_result.h"
#include "Jit/pyjit_typeslots.h"

#ifndef Py_LIMITED_API

extern PyTypeObject _PyJITContext_Type;

/*
 * A JIT context encapsulates all the state managed by an instance of the
 * JIT.
 *
 */
typedef struct {
  PyObject_HEAD

      /* Code generator used by targeted, one off code generators
       */
      CodeGen* code_gen;

  /* General purpose jit compiler; may be null */
  jit::Compiler* jit_compiler;

  /*
   * A dictionary containing deoptimization information for compiled objects.
   *
   * Keys are weak references to either function or type objects.
   * Values are instances of DeoptInfo.
   */
  PyObject* deopt_info;

  /*
   * A dictionary containing pointers to jit::CompiledFunctions.
   *
   * Keys are weak references to function objects.
   * Values are PyCapsules containing the corresponding jit::CompiledFunction.
   */
  PyObject* jit_functions;
} _PyJITContext;

/*
 * Register the _PyJITContext type with the runtime. This needs to be called
 * before using the type.
 */
int _PyJITContext_Init(void);

/* Clean up any global state needed by JIT contexts */
void _PyJITContext_Finalize(void);

/*
 * Allocate a new JIT context.
 *
 * compiler is a handle to a generate purpose JIT compiler. Ownership is
 * transferred to the JIT context.
 *
 * max_code_size is the total amount of memory to allocate to executable code.
 * It is rounded up to the nearest multiple of the system page size.
 *
 * Returns a new reference.
 *
 */
_PyJITContext* _PyJITContext_New(jit::Compiler* compiler);

void _PyJITContext_Free(_PyJITContext* ctx);

/*
 * Generate specialized functions for type object slots. Calls the other
 * _PyJITContext_Specialize* functions and handles setting up deoptimization
 * support.
 *
 * Returns PYJIT_RESULT_OK on success.
 */
_PyJIT_Result _PyJITContext_SpecializeType(
    _PyJITContext* ctx,
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
    PyFunctionObject* func);

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
PyObject* _PyJITContext_GetCompiledFunctions(void);

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

/*
 * Add type as a dependency for func.
 *
 * func will be de-optimized if type is modified.
 * NB: This is intended to be used by tests only.
 *
 * Returns 0 on success or -1 on error.
 */
int _PyJITContext_AddTypeDependency(
    _PyJITContext* ctx,
    PyFunctionObject* func,
    PyObject* type);

#endif /* Py_LIMITED_API */
#endif /* Py_JIT_H */
