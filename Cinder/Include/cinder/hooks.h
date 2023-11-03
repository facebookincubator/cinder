#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _PyShadowCode;

void Ci_code_sizeof_shadowcode(struct _PyShadowCode *shadow, Py_ssize_t *res);

#ifdef __cplusplus
} // extern "C"
#endif
