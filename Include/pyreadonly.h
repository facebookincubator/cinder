#ifndef Py_PYREADONLY_H
#define Py_PYREADONLY_H
#ifdef __cplusplus
extern "C" {
#endif

#define PYREADONLY_RETURN_READONLY_IS_TRANSITIVE -1
#define PYREADONLY_BUILD_FUNCMASK1(arg1_readonly) \
    ((arg1_readonly) != 0 ? 1 : 0)
#define PYREADONLY_BUILD_FUNCMASK2(arg1_readonly, arg2_readonly) \
    (((arg1_readonly) != 0 ? 1 : 0) | (((arg2_readonly) != 0 ? 1 : 0) << 1))
#define PYREADONLY_BUILD_FUNCMASK3(arg1_readonly, arg2_readonly, arg3_readonly) \
    (((arg1_readonly) != 0 ? 1 : 0) | (((arg2_readonly) != 0 ? 1 : 0) << 1) | (((arg3_readonly) != 0 ? 1 : 0) << 2))

/* readonly function masks */
#define PYFUNCTION_READONLY_FUNC_MASK (1ULL << 63)
#define PYFUNCTION_READONLY_NONLOCAL_MASK (1ULL << 62)
#define PYFUNCTION_RETURNS_READONLY (1ULL << 61)
#define PYFUNCTION_YIELDS_READONLY_MASK (1ULL << 60)
#define PYFUNCTION_SENDS_READONLY_MASK (1ULL << 59)

#define READONLY_FUNC(x) ((x)&PYFUNCTION_READONLY_FUNC_MASK)
#define READONLY_NONLOCAL(x) ((x)&PYFUNCTION_READONLY_NONLOCAL_MASK)
#define RETURNS_READONLY(x) (!((x)&PYFUNCTION_RETURNS_READONLY))
#define YIELDS_READONLY(x) (((x)&PYFUNCTION_YIELDS_READONLY_MASK))
#define SENDS_READONLY(x) (((x)&PYFUNCTION_SENDS_READONLY_MASK))
#define READONLY_ARG(x, _i) ((x) & (1ULL << (_i)))
#define CLEAR_NONARG_READONLY_MASK(x)                                                   \
  ((x) & ~(PYFUNCTION_READONLY_FUNC_MASK | PYFUNCTION_READONLY_NONLOCAL_MASK | \
           PYFUNCTION_RETURNS_READONLY | PYFUNCTION_YIELDS_READONLY_MASK |     \
           PYFUNCTION_SENDS_READONLY_MASK))
#define GET_NONARG_READONLY_MASK(x)                                           \
  ((x) & (PYFUNCTION_READONLY_FUNC_MASK | PYFUNCTION_READONLY_NONLOCAL_MASK | \
           PYFUNCTION_RETURNS_READONLY | PYFUNCTION_YIELDS_READONLY_MASK |    \
           PYFUNCTION_SENDS_READONLY_MASK))

// This is disabled for now until we can get proper performance measurements
// done to verify the size of the regression.
// #define PYREADONLY_ENABLED

#ifndef PYREADONLY_ENABLED
#define PyReadonly_BeginReadonlyOperation(a) ((void)a, 0)
#define PyReadonly_MaybeBeginReadonlyOperation(a, b, c) ((void)(a), (void)(b), (void)(c), 0)
#define PyReadonly_ReorderCurrentOperationArgs2() 0
#define PyReadonly_ReorderCurrentOperationArgs3(a, b, c) ((void)(a), (void)(b), (void)(c), 0)
#define PyReadonly_SaveCurrentReadonlyOperation(a) ((*(a) = 0),0)
#define PyReadonly_RestoreCurrentReadonlyOperation(a) ((void)(a), 0)
#define PyReadonly_SuspendCurrentReadonlyOperation(a) ((*(a) = 0),0)
#define PyReadonly_IsReadonlyOperationValid(a, b, c) ((void)(a), (void)(b), (void)(c), 0)
#define PyReadonly_IsTransitiveReadonlyOperationValid(a, b) ((void)(a), (void)(b), 0)
#define PyReadonly_CheckReadonlyOperation(a, b) ((void)(a), (void)(b), 0)
#define PyReadonly_CheckTransitiveReadonlyOperation(a) ((void)(a), 0)
#define PyReadonly_CheckReadonlyOperationOnCallable(a) ((void)(a), 0)
#define PyReadonly_VerifyReadonlyOperationCompleted() 0
#else
/**
 * Begin a readonly operation with the specified mask.
 */
PyAPI_FUNC(int) PyReadonly_BeginReadonlyOperation(int mask);

/**
 * Begins a readonly operation with the specified mask, but
 * only if `originalOperation` refers to a readonly operation
 * that was in progress.
 */
PyAPI_FUNC(int) PyReadonly_MaybeBeginReadonlyOperation(int originalOperation, int returnsReadonly, int argMask);

/**
 * Reorder the arguments of the current operation that takes 2 arguments.
 */
PyAPI_FUNC(int) PyReadonly_ReorderCurrentOperationArgs2(void);

/**
 * Reorder the arguments of the current operation that takes 3 arguments.
 */
PyAPI_FUNC(int) PyReadonly_ReorderCurrentOperationArgs3(int newArg1Pos, int newArg2Pos, int newArg3Pos);

/**
 * Saves a copy of the current readonly operation (if any) in `savedOperation`.
 */
PyAPI_FUNC(int) PyReadonly_SaveCurrentReadonlyOperation(int* savedOperation);

/**
 * Restore the current readonly operation from the value saved by PyReadonly_SaveCurrentReadonlyOperation.
 */
PyAPI_FUNC(int) PyReadonly_RestoreCurrentReadonlyOperation(int savedOperation);

/**
 * Stops the current readonly operation (if any) and store it in `suspendedOperation`.
 */
PyAPI_FUNC(int) PyReadonly_SuspendCurrentReadonlyOperation(int* suspendedOperation);

/**
 * Determine if the specified operation is valid without raising any errors.
 * This is intended to be used to verify that specific optimizations are valid.
 */
PyAPI_FUNC(int) PyReadonly_IsReadonlyOperationValid(int operationMask, int functionArgsMask, int functionReturnsReadonly);

/**
 * The same as PyReadonly_IsReadonlyOperationValid except that the return value
 * is expected to be readonly if any of the operation arguments are readonly.
 * The function args are assumed to all be readonly.

 * @param argCount The numer of arguments to the operation.
 */
PyAPI_FUNC(int) PyReadonly_IsTransitiveReadonlyOperationValid(int operationMask, int argCount);

/**
 * If a readonly operation has been started, check that it's valid with the
 * specified masks, and raising appropriate errors or warnings.
 *
 * This function will only return non-zero if enforcement is enabled. If only
 * checking is enabled, it will always return zero, even if errors are
 * encountered.
 */
PyAPI_FUNC(int) PyReadonly_CheckReadonlyOperation(int functionArgsMask, int functionReturnsReadonly);

/**
 * The same as PyReadonly_CheckReadonlyOperation except that the return value
 * is expected to be readonly if any of the operation arguments are readonly.
 * The function args are assumed to all be readonly.
 *
 * @param argCount The number of arguments to the operation.
 */
PyAPI_FUNC(int) PyReadonly_CheckTransitiveReadonlyOperation(int argCount);

/**
 * The same as `PyReadonly_CheckReadonlyOperation`, but the function flags are retrieved from a
 * callable of unknown type.
 */
PyAPI_FUNC(int) PyReadonly_CheckReadonlyOperationOnCallable(PyObject* callable);

/**
 * Verify that the current readonly operation actually completed.
 *
 * The implementation readonly operations requires that each operator
 * actually calls one of the PyReadonly_Check* functions to do the check.
 * If that doesn't ocur, it can leave things in a bad state, so this generates
 * an error, and does the appropriate cleanup to get things back into a
 * known good state.
 *
 * This function will only return non-zero if enforcement is enabled. If only
 * checking is enabled, it will always return zero, even if errors are
 * encountered.
 */
PyAPI_FUNC(int) PyReadonly_VerifyReadonlyOperationCompleted(void);

#endif

/**
 * Check if attribute access violates readonly rules.
 */
PyAPI_FUNC(void) PyReadonly_Check_LoadAttr(PyObject *obj, int check_return, int check_read);

#ifdef __cplusplus
}
#endif
#endif /* !Py_PYREADONLY_H */
