#ifndef Py_PYREADONLY_H
#define Py_PYREADONLY_H
#ifdef __cplusplus
extern "C" {
#endif

#define PYREADONLY_RETURN_READONLY_IS_TRANSITIVE -1
#define PYREADONLY_BUILD_FUNCMASK2(arg1_readonly, arg2_readonly) \
    (((arg1_readonly) != 0 ? 1 : 0) | (((arg2_readonly) != 0 ? 1 : 0) << 1))
#define PYREADONLY_BUILD_FUNCMASK3(arg1_readonly, arg2_readonly, arg3_readonly) \
    (((arg1_readonly) != 0 ? 1 : 0) | (((arg2_readonly) != 0 ? 1 : 0) << 1) | (((arg3_readonly) != 0 ? 1 : 0) << 2))

// This is disabled for now until we can get proper performance measurements
// done to verify the size of the regression.
//#define PYREADONLY_ENABLED

#ifndef PYREADONLY_ENABLED
#define PyReadonly_BeginReadonlyOperation(a) ((void)a, 0)
#define PyReadonly_IsReadonlyOperationValid(a, b, c) ((void)a, (void)b, (void)c, 0)
#define PyReadonly_IsTransitiveReadonlyOperationValid(a, b) ((void)a, (void)b, 0)
#define PyReadonly_CheckReadonlyOperation(a, b) ((void)a, (void)b, 0)
#define PyReadonly_CheckTransitiveReadonlyOperation(a) ((void)a, 0)
#define PyReadonly_CheckReadonlyOperationOnCallable(a) ((void)a, 0)
#define PyReadonly_VerifyReadonlyOperationCompleted() 0
#else
/**
 * Begin a readonly operation with the specified mask.
 */
PyAPI_FUNC(int) PyReadonly_BeginReadonlyOperation(int mask);

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
 * @param argCount The numer of arguments to the operation.
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

#ifdef __cplusplus
}
#endif
#endif /* !Py_PYREADONLY_H */
