#ifndef Py_IMMUTABLE_ERRORS_H
#define Py_IMMUTABLE_ERRORS_H
#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
 * warn_code: enums describing the kind of error detected
 * warning: the warning message, a format string
 * nargs: number of arguments
 */
typedef struct PyImmutableErrorEntry {
    int err_code;
    const char *err_format;
    int nargs;
} PyImmutableErrorEntry;

#define _PyErr_IMMUTABLE_ERR(err, ...) \
    (_PyErr_RaiseImmutableWarningV(&(err), ## __VA_ARGS__))

PyAPI_FUNC(int) _PyErr_RaiseImmutableWarningV(PyImmutableErrorEntry* err_entry, ...);

#ifdef _CINDER_DEFINE_IMMUTABLE_ERRORS
#define DEF_ERROR(name, code, fmt_string) PyImmutableErrorEntry name = {code, fmt_string, -1};
#else
#define DEF_ERROR(name, code, fmt_string) extern PyImmutableErrorEntry name;
#endif

DEF_ERROR(ImmutableDictError, 0, "%U on immutable dict");
DEF_ERROR(ReadonlyFunctionCallError, 1,"A mutable function cannot be called in a readonly function.")
DEF_ERROR(ReadonlyNonlocalError, 2, "A function decorated with @readonly_closure cannot call another function without the @readonly_closure decoration.")
DEF_ERROR(ReadonlyAssignmentError, 3, "Cannot assign a readonly value to a mutable variable.")
DEF_ERROR(ReadonlyArgumentError, 4, "Passing a readonly variable to Argument %S, which is mutable.")
DEF_ERROR(ReadonlyYieldError, 5, "Generator yields a readonly value, but expected it to yield a mutable value.")
DEF_ERROR(ReadonlySendError, 6, "Cannot send a readonly value to a mutable generator.")

DEF_ERROR(ReadonlyOperatorCheckNotRan, 10, "Attempted to perform a readonly operator call, but no check was actually performed. Remaining mask: 0x%02X");
DEF_ERROR(ReadonlyOperatorAlreadyInProgress, 11, "Attempted to begin a readonly operation in a frame that's already performing a readonly operation. Old mask: 0x%02X New mask: 0x%02X");
DEF_ERROR(ReadonlyOperatorInNonFrameContext, 12, "Attempted to set the readonly operation mask in a context where no frames exist. (eg. constant folding)");
DEF_ERROR(ReadonlyOperatorArgumentReadonlyMismatch, 13, "Attempted to pass a readonly arguments to an operation that expects mutable parameters.");
DEF_ERROR(ReadonlyOperatorReturnsReadonlyMismatch, 14, "Operator returns readonly, but expected mutable.");
DEF_ERROR(ReadonlyOperatorCallOnUnknownCallableType, 15, "Attempted to perform a readonly operator call, but was unable to determine what kind of callable object was used. No check was performed.");

#undef DEF_ERROR

#ifdef __cplusplus
}
#endif
#endif /* !Py_IMMUTABLE_ERRORS_H */
