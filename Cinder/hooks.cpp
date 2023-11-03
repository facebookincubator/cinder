#include "cinder/hooks.h"
#include "Shadowcode/shadowcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Placeholder file to add in custom hooks */

void Ci_code_sizeof_shadowcode(struct _PyShadowCode *shadow, Py_ssize_t *res) {
    res += sizeof(_PyShadowCode);
    res += sizeof(PyObject *) * shadow->l1_cache.size;
    res += sizeof(PyObject *) * shadow->cast_cache.size;
    res += sizeof(PyObject **) * shadow->globals_size;
    res += sizeof(_PyShadow_InstanceAttrEntry **) *
            shadow->polymorphic_caches_size;
    res += sizeof(_FieldCache) * shadow->field_cache_size;
    res += sizeof(_Py_CODEUNIT) * shadow->len;
}

#ifdef __cplusplus
} // extern "C"
#endif
