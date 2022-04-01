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
DEF_ERROR(ReadonlyNonlocalError, 2, "A function decorated with @readonly_closure cannot call another fuction without @readonly_closure decorated.")
DEF_ERROR(ReadonlyAssignmentError, 3, "Cannot assign a readonly value to a mutable variable.")
DEF_ERROR(ReadonlyArgumentError, 4, "Passing a readonly variable to Argument %S, which is mutable.")
DEF_ERROR(ReadonlyYieldError, 5, "Generator yields a readonly value, but expected it to yield a mutable value.")
DEF_ERROR(ReadonlySendError, 6, "Cannot send a readonly value to a mutable generator.")

#undef DEF_ERROR

#ifdef __cplusplus
}
#endif
#endif /* !Py_IMMUTABLE_ERRORS_H */
