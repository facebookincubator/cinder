#ifndef JIT_RUNTIME_H
#define JIT_RUNTIME_H

#include "Python.h"
#include "classloader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JITRT_CALL_KIND_FUNC = 0,
  JITRT_CALL_KIND_METHOD_DESCR,
  JITRT_CALL_KIND_METHOD_LIKE,
  JITRT_CALL_KIND_WRAPPER_DESCR,
  JITRT_CALL_KIND_OTHER
} JITRT_CallMethodKind;

typedef struct {
  PyTypeObject* type;
  PyObject* value;
  JITRT_CallMethodKind call_kind;
} JITRT_LoadMethodCacheEntry;

// static->static call convention for primitive returns is to return error flag
// in rdx (null means error occurred); for C helpers that need to implement this
// convention, returning this struct will fill the right registers
typedef struct {
  void* rax;
  void* rdx;
} JITRT_StaticCallReturn;

#define LOAD_METHOD_CACHE_SIZE 4

typedef struct {
  JITRT_LoadMethodCacheEntry entries[LOAD_METHOD_CACHE_SIZE];
} JITRT_LoadMethodCache;

/*
 * Allocate a new PyFrameObject and link it into the current thread's
 * call stack.
 *
 * Returns the thread state that the freshly allocated frame was linked to
 * (accessible via ->frame) on success or NULL on error.
 */
PyThreadState* JITRT_AllocateAndLinkFrame(
    PyCodeObject* code,
    PyObject* globals);

/*
 * Allocate a new Tiny frame and link it into the current thread's
 * call stack.
 *
 * This function behaves the same as JITRT_AllocateAndLinkFrame except for that
 * a tiny frame is allocated and linked instead of a PyObjectFrame object, which
 * is much faster.
 */
PyThreadState* JITRT_AllocateAndLinkTinyFrame(
    PyCodeObject* code,
    PyObject* globals);

/*
 * Helper function to unlink a frame.
 *
 * Designed to be used in tandem with JITRT_AllocateAndLinkFrame. This checks
 * if the frame has escaped (> 1 refcount) and tracks it if so.
 */
void JITRT_UnlinkFrame(PyThreadState* tstate);

/*
 * Helper function to unlink a tiny frame.
 *
 * Designed to be used in tiny-frame mode. This function calls
 * JITRT_UnlinkFrame, if the frame object is not a tiny frame.
 */
void JITRT_UnlinkTinyFrame(PyThreadState* tstate);

/*
 * Same as JITRT_UnlinkFrame but also clears tstate->frame->f_back as needed
 * during InitialYield.
 */
void JITRT_InitialYieldUnlinkFrame(PyThreadState* tstate);

/*
 * Currently a dummy function whcih asserts. For use during InitialYield.
 */
void JITRT_InitialYieldUnlinkTinyFrame(PyThreadState* tstate);

/*
 * Handles a call that includes kw arguments or excess tuple arguments
 */
PyObject* JITRT_CallWithKeywordArgs(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames);

PyObject* JITRT_CallWithIncorrectArgcount(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    int argcount);

#define JITRT_CALL_REENTRY_OFFSET (-6)
#define JITRT_GET_REENTRY(entry) \
  ((vectorcallfunc)(((char*)entry) + JITRT_CALL_REENTRY_OFFSET))

PyObject* JITRT_ReportStaticArgTypecheckErrors(
    PyObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames);

/*
 * Mimics the behavior of _PyDict_LoadGlobal except that it raises an error when
 * the name does not exist.
 */
PyObject*
JITRT_LoadGlobal(PyObject* globals, PyObject* builtins, PyObject* name);

/*
 * Perform a positional-only function call
 *
 * args[0] is expected to point to the callable and args[1] through args[nargs -
 * 1] are expected to point to the arguments to the call.
 */
PyObject* JITRT_CallFunction(PyObject* func, PyObject** args, Py_ssize_t nargs);

/*
 * As JITRT_CallFunction but eagerly starts coroutines.
 */
PyObject*
JITRT_CallFunctionAwaited(PyObject* func, PyObject** args, Py_ssize_t nargs);

/*
 * Perform a combined positional and kwargs function call
 *
 * args[0] points to the callable and args[1] - args[nargs - 2] are all argument
 * values, and args[nargs - 1] is a tuple of strings mapping the last
 * len(args[nargs - 1]) args to named positions.
 */
PyObject*
JITRT_CallFunctionKWArgs(PyObject* func, PyObject** args, Py_ssize_t nargs);

/*
 * As JITRT_CallFunctionKWArgs but eagerly starts coroutines.
 */
PyObject* JITRT_CallFunctionKWArgsAwaited(
    PyObject* func,
    PyObject** args,
    Py_ssize_t nargs);

/*
 * Helper to perform a Python call with dynamically determined arguments.
 *
 * pargs will be a possibly empty tuple of positional arguments, kwargs will be
 * null or a dictionary of keyword arguments.
 */
PyObject*
JITRT_CallFunctionEx(PyObject* func, PyObject* pargs, PyObject* kwargs);

/*
 * As JITRT_CallFunctionEx but eagerly starts coroutines.
 */
PyObject*
JITRT_CallFunctionExAwaited(PyObject* func, PyObject* pargs, PyObject* kwargs);

/*
 * Perform a positional-only function call.
 *
 * This is designed to be used in tandem with JITRT_GetMethod to optimize
 * calls that look like instance method calls (e.g. `self.foo()`) to avoid the
 * creation of bound methods.
 *
 * args[0] is expected to point to the receiver of the method lookup (e.g.
 * `self` in the example above) args[1] through args[nargs - 1] are expected to
 * point to the arguments to the call.
 *
 * call_kind indicates the type of thing being called:
 *
 *   - JITRT_CALL_KIND_FUNC  - We're calling a PyFunctionObject that was
 * returned instead of creating a bound method.
 *   - JITRT_CALL_KIND_CFUNC - We're calling a C function (PyMethodDef) that was
 * returned instead of creating a bound method.
 *   - JITRT_CALL_KIND_OTHER - We're calling something else. Bound method
 * creation was not deferred.
 */
PyObject* JITRT_CallMethod(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames,
    JITRT_CallMethodKind call_kind);

/*
 * As JITRT_CallMethod but eagerly starts coroutines.
 */
PyObject* JITRT_CallMethodAwaited(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames,
    JITRT_CallMethodKind call_kind);

/*
 * Perform an attribute lookup.
 *
 * This is used to avoid bound method creation for attribute lookups that
 * correspond to method calls (e.g. `self.foo()`).
 *
 * call_kind indicates whether or not bound method creation was deferred.
 */
PyObject* JITRT_GetMethod(
    PyObject* obj,
    PyObject* name,
    JITRT_LoadMethodCache* cache,
    JITRT_CallMethodKind* call_kind);

/*
 * Perform an attribute lookup in a super class
 *
 * This is used to avoid bound method creation for attribute lookups that
 * correspond to method calls (e.g. `self.foo()`).
 *
 * call_kind indicates whether or not bound method creation was deferred.
 */
PyObject* JITRT_GetMethodFromSuper(
    PyObject* global_super,
    PyObject* type,
    PyObject* self,
    PyObject* name,
    bool no_args_in_super_call,
    JITRT_CallMethodKind* call_kind);

/*
 * Perform an attribute lookup in a super class
 */
PyObject* JITRT_GetAttrFromSuper(
    PyObject* super_globals,
    PyObject* type,
    PyObject* self,
    PyObject* name,
    bool no_args_in_super_call);

// dealloc a PyObject object
void JITRT_Dealloc(PyObject* obj);

/*
 * Mimics the behavior of the UNARY_NOT opcode.
 *
 * Checks if value is truthy, and returns Py_False if it is, or Py_True if
 * it's not.  Returns NULL if the object doesn't support truthyness.
 */
PyObject* JITRT_UnaryNot(PyObject* value);

void JITRT_InitLoadMethodCache(JITRT_LoadMethodCache* cache);

/*
 * Invokes a function stored within the method table for the object.
 * The method table lives off tp_cache in the type object
 */
PyObject* JITRT_InvokeMethod(
    Py_ssize_t slot,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames);

/*
 * Invokes a function that was compiled statically.
 */
PyObject*
JITRT_InvokeFunction(PyObject* func, PyObject** args, Py_ssize_t nargs);

/*
 * As JITRT_InvokeFunction but eagerly starts coroutines.
 */
PyObject*
JITRT_InvokeFunctionAwaited(PyObject* func, PyObject** args, Py_ssize_t nargs);

/*
 * Loads an indirect function, optionally loading it from the descriptor
 * if the indirect cache fails.
 */
PyObject* JITRT_LoadFunctionIndirect(PyObject** func, PyObject* descr);

/*
 * Performs a type check on an object, raising an error if the object is
 * not an instance of the specified type.  The type check is a real type
 * check which doesn't support dynamic behaviors against the type or
 * proxy behaviors against obj.__class__
 */
PyObject* JITRT_Cast(PyObject* obj, PyTypeObject* type);

/*
 * Performs a type check on an object, raising an error if the object is
 * not an instance of the specified type or None.  The type check is a
 * real type check which doesn't support dynamic behaviors against the
 * type or proxy behaviors against obj.__class__.
 */
PyObject* JITRT_CastOptional(PyObject* obj, PyTypeObject* type);

/* Helper methods to implement left shift, which wants its operand in cl */
int64_t JITRT_ShiftLeft64(int64_t x, int64_t y);
int32_t JITRT_ShiftLeft32(int32_t x, int32_t y);

/* Helper methods to implement right shift, which wants its operand in cl */
int64_t JITRT_ShiftRight64(int64_t x, int64_t y);
int32_t JITRT_ShiftRight32(int32_t x, int32_t y);

/* Helper methods to implement unsigned right shift, which wants its operand in
 * cl
 */
uint64_t JITRT_ShiftRightUnsigned64(uint64_t x, uint64_t y);
uint32_t JITRT_ShiftRightUnsigned32(uint32_t x, uint32_t y);

/* Helper methods to implement signed modulus */
int64_t JITRT_Mod64(int64_t x, int64_t y);
int32_t JITRT_Mod32(int32_t x, int32_t y);

/* Helper methods to implement unsigned modulus */
uint64_t JITRT_ModUnsigned64(uint64_t x, uint64_t y);
uint32_t JITRT_ModUnsigned32(uint32_t x, uint32_t y);

PyObject* JITRT_BoxI32(int32_t i);
PyObject* JITRT_BoxU32(uint32_t i);
PyObject* JITRT_BoxBool(uint32_t i);
PyObject* JITRT_BoxI64(int64_t i);
PyObject* JITRT_BoxU64(uint64_t i);
PyObject* JITRT_BoxDouble(double_t d);

uint64_t JITRT_IsNegativeAndErrOccured(int64_t i);

/* Array lookup helpers */
uint64_t JITRT_GetI8_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetU8_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetI16_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetU16_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetI32_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetU32_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetI64_FromArray(char* arr, int64_t idx);
uint64_t JITRT_GetU64_FromArray(char* arr, int64_t idx);
PyObject* JITRT_GetObj_FromArray(char* arr, int64_t idx);

/* Array set helpers */
void JITRT_SetI8_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetU8_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetI16_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetU16_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetI32_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetU32_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetI64_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetU64_InArray(char* arr, uint64_t val, int64_t idx);
void JITRT_SetObj_InArray(char* arr, uint64_t val, int64_t idx);

uint64_t JITRT_UnboxU64(PyObject* obj);
uint32_t JITRT_UnboxU32(PyObject* obj);
int64_t JITRT_UnboxI64(PyObject* obj);
int32_t JITRT_UnboxI32(PyObject* obj);

/*
 * Calls __builtins__.__import__(), with a fast-path if this hasn't been
 * overridden.
 *
 * This is a near verbatim copy of import_name() from ceval.c with minor
 * tweaks. We copy rather than expose to avoid making changes to ceval.c.
 */
PyObject* JITRT_ImportName(
    PyThreadState* tstate,
    PyObject* name,
    PyObject* fromlist,
    PyObject* level);

// Given a code object and an index into f_localsplus, compute which of
// code->co_varnames, code->cellvars, or code->freevars contains the name of
// the variable. Return that tuple and adjust idx as needed.
PyObject* JITRT_GetVarnameTuple(PyCodeObject* code, int* idx);

/*
 * Wrapper around _Py_DoRaise() which handles the case where we re-raise but no
 * active exception is set.
 */
void JITRT_DoRaise(PyThreadState* tstate, PyObject* exc, PyObject* cause);

/*
 * Frees JIT-specific suspend data allocated in JITRT_MakeGenObject().
 */
void JITRT_GenJitDataFree(PyGenObject* gen);

/*
 * Formats a f-string value
 */
PyObject* JITRT_FormatValue(
    PyThreadState* tstate,
    PyObject* fmt_spec,
    PyObject* value,
    int conversion);
/*
 * Concatenate strings from args
 */
PyObject* JITRT_BuildString(
    void* /*unused*/,
    PyObject** args,
    size_t nargsf,
    void* /*unused*/);

// Per-function entry point function to resume a JIT generator. Arguments are:
//   - Generator instance to be resumed.
//   - A value to send in or NULL to raise the current global error on resume.
//   - The current thread-state instance.
//  Returns result of computation which is a "yielded" value unless the state of
//  the generator is _PyJITGenState_Completed, in which case it is a "return"
//  value. If the return is NULL, an exception has been raised.
typedef PyObject* (*GenResumeFunc)(
    PyObject* gen,
    PyObject* send_value,
    PyThreadState* tstate,
    uint64_t finish_yield_from);

/*
 * Create generator instance for use during InitialYield in a JIT generator.
 * There is a variant for each of the different types of generator: iterators,
 * coroutines, and async generators.
 */
PyObject* JITRT_MakeGenObject(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code);

PyObject* JITRT_MakeGenObjectAsyncGen(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code);

PyObject* JITRT_MakeGenObjectCoro(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code);

// Mostly the same implementation as YIELD_FROM in ceval.c with slight tweaks to
// make it stand alone. The argument 'v' is stolen.
//
// The arguments 'gen', 'v', 'tstate', 'finish_yield_from' must match positions
// with JIT resume entry function (GenResumeFunc) so registers with their values
// pass straight through.
struct JITRT_YieldFromRes {
  PyObject* retval;
  uint64_t done;
};
JITRT_YieldFromRes JITRT_YieldFrom(
    PyObject* gen,
    PyObject* v,
    PyThreadState* tstate,
    uint64_t finish_yield_from);

/* Unpack a sequence as in unpack_iterable(), and save the
 * results in a tuple.
 */
PyObject* JITRT_UnpackExToTuple(
    PyThreadState* tstate,
    PyObject* iterable,
    int before,
    int after);

JITRT_StaticCallReturn
JITRT_CompileFunction(PyFunctionObject* func, PyObject** args, bool* compiled);

PyObject* JITRT_CallStaticallyWithPrimitiveSignature(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames,
    _PyTypedArgsInfo* arg_info);

/* Compares if one unicode object is equal to another object.
 * At least one of the objects has to be exactly a unicode
 * object.
 */
int JITRT_UnicodeEquals(PyObject* s1, PyObject* s2, int equals);

/* Inverse form of PySequence_Contains for "not in"
 */
int JITRT_NotContains(PyObject* w, PyObject* v);

/* Perform a rich comparison with integer result.  This wraps
   PyObject_RichCompare(), returning -1 for error, 0 for false, 1 for true.
   Unlike PyObject_RichCompareBool this doesn't perform an object equality
   check, which is incompatible w/ float comparisons. */

int JITRT_RichCompareBool(PyObject* v, PyObject* w, int op);

#ifdef __cplusplus
}
#endif

#endif
