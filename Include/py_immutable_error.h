#ifndef Py_IMMUTABLE_ERRORS_H
#define Py_IMMUTABLE_ERRORS_H
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
    (_PyErr_RaiseImmutableWarningV((err), __VA_ARGS__))
PyAPI_FUNC(int) _PyErr_RaiseImmutableWarningV(PyImmutableErrorEntry* err_entry, ...);

#define DEF_ERROR(name, code, fmt_string)                                      \
    PyImmutableErrorEntry name = {code, fmt_string, -1};

DEF_ERROR(ImmutableDictError, 0, "%U on immutable dict");

#undef DEF_ERROR

#ifdef __cplusplus
}
#endif
#endif /* !Py_IMMUTABLE_ERRORS_H */
