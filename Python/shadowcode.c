/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
/* facebook t39538061 */
/* This file contains the implementation of a shadow byte code system.  The
 * idea here is that at runtime a new array of bytes gets produced for the code
 * and based upon dynamic profiling the byte codes get replaced with more
 * optimal byte codes.  Any running code is none the wiser to the fact that
 * there are additional hidden byte codes that are being executed by the
 * interpreter.
 *
 * In addition to the shadow byte code there are a number of cache objects
 * which are used to store information to execute the optimized bytecodes.
 */

#include "Jit/pyjit.h"
#include "Python.h"
#include "pycore_shadowcode.h"
#include "pycore_moduleobject.h"
#include "wordcode_helpers.h"
#include "structmember.h"
#include "structmember.h"
#include <stddef.h>
#include "cinder/exports.h"

#define _PyClassMethod_Check(op) (Py_TYPE(op) == &PyClassMethod_Type)
#define _PyStaticMethod_Check(op) (Py_TYPE(op) == &PyStaticMethod_Type)
#define _PyWrapperDescr_Check(d) (Py_TYPE(d) == &PyWrapperDescr_Type)

extern inline PyCodeCacheRef *_PyShadow_FindCache(PyObject *from);
extern inline PyCodeCacheRef *_PyShadow_GetCache(PyObject *from);

/* Number of inline caches that have been allocated */
Py_ssize_t inline_cache_count = 0;

/* Total number of bytes allocated to inline caches */
Py_ssize_t inline_cache_total_size = 0;

PyTypeObject _PyCodeCache_RefType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "shadow_ref",
    .tp_doc = "shadow_ref",
    .tp_basicsize = sizeof(PyCodeCacheRef),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_free = PyObject_GC_Del,
    .tp_base = &_PyWeakref_RefType,
    .tp_hash = (hashfunc)_Py_HashPointer,
};

static void
instance_attr_free(PyObject *self)
{
    Py_DECREF(((_PyShadow_InstanceAttrEntry *)self)->name);
    Py_TYPE(self)->tp_free(self);
}

/* Base type for our cache types.  Mainly exists for debugging purposes so
 * we can easily assert that we are working on a valid cache */
_PyCacheType _PyShadow_BaseCache = {
    .type = {
        PyVarObject_HEAD_INIT(NULL, 0).tp_name = "ShadowCacheBase",
        .tp_basicsize = sizeof(_PyShadow_CacheEntry),
        .tp_flags = Py_TPFLAGS_DEFAULT,
    }};

PyTypeObject _PyShadow_InstanceAttrEntryType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "instance_shadow_cache",
    .tp_doc = "instance_shadow_cache",
    .tp_basicsize = sizeof(_PyShadow_InstanceAttrEntry),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_dealloc = instance_attr_free,
};

void
invalidate_instance_attr_entry(_PyShadow_InstanceAttrEntry *entry)
{
    assert(Py_TYPE(entry) == &_PyShadow_InstanceAttrEntryType ||
           Py_TYPE(entry)->tp_base == &_PyShadow_BaseCache.type);
    entry->type = NULL;
    entry->value = NULL;
}

int
instance_entry_is_valid(_PyShadow_InstanceAttrEntry *entry)
{
    return entry->type != NULL;
}

/* Aliases the load method types to normal descriptor loads */
#define _PyShadow_LoadAttrDictMethod _PyShadow_LoadAttrDictDescr
#define _PyShadow_LoadAttrNoDictMethod _PyShadow_LoadAttrNoDictDescr
#define _PyShadow_LoadAttrSplitDictMethod _PyShadow_LoadAttrSplitDictDescr

static inline _PyCacheType *
_PyShadow_CacheType(PyObject *obj)
{
    PyTypeObject *type = Py_TYPE(obj);
    assert(type->tp_base == &_PyShadow_BaseCache.type);
    return (_PyCacheType *)type;
}

static int
_PyShadow_IsCacheValid(PyObject *obj)
{
    _PyCacheType *cache_type = _PyShadow_CacheType(obj);
    return cache_type->is_valid(obj);
}

/* We define a type for each of our styles of caches, the types include
 * function pointers for dispatching to our type of operations (e.g. load,
 * store)
 */

#define _PyShadow_LoadMethodSlot _PyShadow_LoadMethodNoDictDescr
#define _PyShadow_LoadMethodSplitDict _PyShadow_LoadMethodSplitDictDescr
#define _PyShadow_LoadMethodDictNoDescr _PyShadow_LoadMethodDictDescr

#define _PyShadow_StoreAttrDictNoDescr _PyShadow_StoreAttrDict
#define _PyShadow_StoreAttrDictDescr _PyShadow_StoreAttrDict
#define _PyShadow_StoreAttrNoDictDescr _PyShadow_StoreAttrDescr
#define _PyShadow_StoreAttrSplitDictDescr _PyShadow_StoreAttrSplitDict

#define _PyShadow_StoreAttrNoDictMethod _PyShadow_StoreAttrDescr

#define PY_CACHE_INST_TYPE(                                                   \
    suffix, load_attr_op, load_method_op, store_attr_op)                      \
    _PyCacheType _PyShadow_InstanceCache##suffix = {                          \
        .type = {PyVarObject_HEAD_INIT(NULL, 0).tp_name =                     \
                     "ShadowCacheInst" #suffix,                               \
                 .tp_basicsize = sizeof(_PyShadow_InstanceAttrEntry),         \
                 .tp_flags = Py_TPFLAGS_DEFAULT,                              \
                 .tp_dealloc = instance_attr_free,                            \
                 .tp_base = &_PyShadow_BaseCache.type},                       \
        .load_func =                                                          \
            (pyshadowcache_loadattr_func)&_PyShadow_LoadAttr##suffix,         \
        .load_method =                                                        \
            (pyshadowcache_loadmethod_func)&_PyShadow_LoadMethod##suffix,     \
        .invalidate = (invalidate_func)invalidate_instance_attr_entry,        \
        .store_attr = (storeattr_func)&_PyShadow_StoreAttr##suffix,           \
        .load_attr_opcode = load_attr_op,                                     \
        .load_method_opcode = load_method_op,                                 \
        .store_attr_opcode = store_attr_op,                                   \
        .is_valid = (is_valid_func)instance_entry_is_valid,                   \
    };

PY_CACHE_INST_TYPE(DictNoDescr,
                   LOAD_ATTR_DICT_NO_DESCR,
                   LOAD_METHOD_DICT_DESCR,
                   STORE_ATTR_DICT)
PY_CACHE_INST_TYPE(DictDescr,
                   LOAD_ATTR_DICT_DESCR,
                   LOAD_METHOD_DICT_DESCR,
                   STORE_ATTR_DICT)
PY_CACHE_INST_TYPE(Slot,
                   LOAD_ATTR_SLOT,
                   LOAD_METHOD_NO_DICT_DESCR,
                   STORE_ATTR_SLOT)
PY_CACHE_INST_TYPE(NoDictDescr,
                   LOAD_ATTR_NO_DICT_DESCR,
                   LOAD_METHOD_NO_DICT_DESCR,
                   STORE_ATTR_DESCR)
PY_CACHE_INST_TYPE(SplitDictDescr,
                   LOAD_ATTR_SPLIT_DICT_DESCR,
                   LOAD_METHOD_SPLIT_DICT_DESCR,
                   STORE_ATTR_SPLIT_DICT)
PY_CACHE_INST_TYPE(SplitDict,
                   LOAD_ATTR_SPLIT_DICT,
                   LOAD_METHOD_SPLIT_DICT_DESCR,
                   STORE_ATTR_SPLIT_DICT)
PY_CACHE_INST_TYPE(NoDictMethod,
                   LOAD_ATTR_NO_DICT_DESCR,
                   LOAD_METHOD_NO_DICT_METHOD,
                   STORE_ATTR_DESCR)
/* These two have special methods which will call
 * _PyType_ClearNoShadowingInstances, but still use the normal opcodes which
 * won't call it.  Either the first assignment, which will occur during the
 * initial cache miss, will cause PyType_Modified to occur, and no one can
 * perform caching against just the method.  Or the type will already have
 * Py_TPFLAGS_NO_SHADOWING_INSTANCES cleared and again no one will be caching
 * the method. */
PY_CACHE_INST_TYPE(DictMethod,
                   LOAD_ATTR_DICT_DESCR,
                   LOAD_METHOD_DICT_METHOD,
                   STORE_ATTR_DICT)
PY_CACHE_INST_TYPE(SplitDictMethod,
                   LOAD_ATTR_SPLIT_DICT_DESCR,
                   LOAD_METHOD_SPLIT_DICT_METHOD,
                   STORE_ATTR_SPLIT_DICT)

static void
module_attr_free(PyObject *self)
{
    Py_DECREF(((_PyShadow_ModuleAttrEntry *)self)->name);
    Py_TYPE(self)->tp_free(self);
}

int
module_entry_is_valid(_PyShadow_ModuleAttrEntry *entry)
{
    return entry->module != NULL &&
           entry->version == PYCACHE_MODULE_VERSION(entry->module);
}

_PyCacheType _PyShadow_ModuleAttrEntryType;

void
invalidate_module_attr_entry(_PyShadow_ModuleAttrEntry *entry)
{
    assert(Py_TYPE(entry) == &_PyShadow_ModuleAttrEntryType.type);
    entry->module = NULL;
    entry->value = NULL;
}

_PyCacheType _PyShadow_ModuleAttrEntryType = {
    .type =
        {
            PyVarObject_HEAD_INIT(NULL, 0).tp_name = "ShadowCacheModule",
            .tp_basicsize = sizeof(_PyShadow_ModuleAttrEntry),
            .tp_flags = Py_TPFLAGS_DEFAULT,
            .tp_dealloc = module_attr_free,
            .tp_base = &_PyShadow_BaseCache.type,
        },
    .load_func = (pyshadowcache_loadattr_func)&_PyShadow_LoadAttrModule,
    .load_method = (pyshadowcache_loadmethod_func)&_PyShadow_LoadMethodModule,
    .invalidate = (invalidate_func)invalidate_module_attr_entry,
    .load_attr_opcode = LOAD_ATTR_MODULE,
    .load_method_opcode = LOAD_METHOD_MODULE,
    .is_valid = (is_valid_func)module_entry_is_valid,
};

int
strictmodule_entry_is_valid(_PyShadow_ModuleAttrEntry *entry) {
    return entry->module != NULL && entry->version == PYCACHE_STRICT_MODULE_VERSION(entry->module);
}

_PyCacheType _PyShadow_StrictModuleAttrEntryType;

void
invalidate_strictmodule_attr_entry(_PyShadow_ModuleAttrEntry *entry)
{
    assert(Py_TYPE(entry) == &_PyShadow_StrictModuleAttrEntryType.type);
    entry->module = NULL;
    entry->value = NULL;
}

_PyCacheType _PyShadow_StrictModuleAttrEntryType = {
    .type = {
        PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = "ShadowCacheStrictModule",
        .tp_basicsize = sizeof(_PyShadow_ModuleAttrEntry),
        .tp_flags = Py_TPFLAGS_DEFAULT,
        .tp_dealloc = module_attr_free,
        .tp_base = &_PyShadow_BaseCache.type,
    },
    .load_func = (pyshadowcache_loadattr_func)&_PyShadow_LoadAttrStrictModule,
    .load_method = (pyshadowcache_loadmethod_func)&_PyShadow_LoadMethodStrictModule,
    .invalidate = (invalidate_func)invalidate_strictmodule_attr_entry,
    .load_attr_opcode = LOAD_ATTR_S_MODULE,
    .load_method_opcode = LOAD_METHOD_S_MODULE,
    .is_valid = (is_valid_func)strictmodule_entry_is_valid,
};

static void
invalidate_cache_entries(PyObject *dict)
{
    if (dict == NULL)
        return;

    PyObject *key, *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(dict, &pos, &key, &value)) {
        if (value->ob_type->tp_base == &_PyShadow_BaseCache.type) {
            ((_PyCacheType *)value->ob_type)->invalidate(value);
        }
    }
}

#define CACHE_UPDATE_DISABLED -1

static void
invalidate_caches(PyCodeCacheRef *cache, int force)
{
    if (cache->invalidate_count != CACHE_UPDATE_DISABLED || force) {
        invalidate_cache_entries(cache->l2_cache);
        if (cache->l2_cache != NULL) {
            PyDict_Clear(cache->l2_cache);
        }

        if (cache->type_insts != NULL) {
            /* If this is a meta type that's being modified we want to clear
             * out any entries for instances of our meta types */
            PyObject *key, *value;
            Py_ssize_t pos = 0;

            while (PyDict_Next(cache->type_insts, &pos, &key, &value)) {
                invalidate_cache_entries(value);
            }
            PyDict_Clear(cache->type_insts);
            Py_CLEAR(cache->type_insts);
        }

        if (cache->invalidate_count++ > 1000) {
            cache->invalidate_count = CACHE_UPDATE_DISABLED;
        }
    }
}

/* Called by type object when a type gets modified.  Invalidate all of our
 * caches for the type */
void
_PyShadow_TypeModified(PyTypeObject *type)
{
    PyCodeCacheRef *cache = _PyShadow_FindCache((PyObject *)type);
    if (cache != NULL) {
        PyTypeObject *metatype;

        /* When a type version changes invalidate all of our caches for it.
         * This lets us use simple type comparisons to see if our caches are
         * still valid instead of having to do version checks */
        invalidate_caches(cache, 0);

        metatype = Py_TYPE(type);
        if (metatype != &PyType_Type) {
            PyCodeCacheRef *new_cache =
                _PyShadow_FindCache((PyObject *)metatype);
            PyCodeCacheRef *metacache = (PyCodeCacheRef *)cache->metatype;
            if (metacache != NULL && new_cache != metacache) {
                /* Clear out the old back pointers from the meta type to the
                 * our cache entries, and update our meta type entry */
                if (metacache->type_insts != NULL) {
                    PyDict_DelItem(metacache->type_insts, (PyObject *)cache);
                }
                Py_CLEAR(cache->metatype);
            }
        }

        Py_CLEAR(cache->l2_cache);
        Py_CLEAR(cache->metatype);
    }
}

/* Callback that's invoked when a cache target is collected.  We cleanup our
 * caches for the object */
PyObject *
weakref_callback_impl(PyObject *self, PyCodeCacheRef *weakref)
{
    /* When the object goes away the cache is no longer valid.  We explicitly
     * clear these out, which allows us to otherwise use simple pointer checks
     * in our inline caches and not worry about object re-use */
    invalidate_caches(weakref, 1);
    Py_XDECREF(weakref->l2_cache);

    if (weakref->metatype != NULL) {
        Py_DECREF(weakref->metatype);
    }

    /* free the weak ref when it's target is no longer alive */
    Py_DECREF(weakref);

    Py_INCREF(Py_None);
    return Py_None;
}

PyMethodDef _WeakrefCallback = {
    "weakref_callback", (PyCFunction)weakref_callback_impl, METH_O, NULL};

static int weakref_init = 0;
static PyObject *weakref_callback;

static inline int
_PyShadow_Init(void)
{
    _PyCodeCache_RefType.tp_traverse = _PyWeakref_RefType.tp_traverse;
    _PyCodeCache_RefType.tp_clear = _PyWeakref_RefType.tp_clear;
    _PyCodeCache_RefType.tp_dealloc = _PyWeakref_RefType.tp_dealloc;
    if (PyType_Ready(&_PyCodeCache_RefType) < 0 ||
        PyType_Ready(&_PyShadow_ModuleAttrEntryType.type) < 0 ||
        PyType_Ready(&_PyShadow_StrictModuleAttrEntryType.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheDictNoDescr.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheDictDescr.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheSlot.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheNoDictDescr.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheSplitDictDescr.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheSplitDict.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheDictMethod.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheNoDictMethod.type) < 0 ||
        PyType_Ready(&_PyShadow_InstanceCacheSplitDictMethod.type) < 0) {
        return 0;
    }
    weakref_callback = PyCFunction_New(&_WeakrefCallback, NULL);
    if (weakref_callback == NULL) {
        return 0;
    }
    return 1;
}

static int
_PyShadow_EnsureInit(void)
{
    if (!weakref_init) {
        weakref_init = _PyShadow_Init();
    }
    return weakref_init;
}

Py_ssize_t _Py_NO_INLINE
_PyShadow_FixDictOffset(PyObject *obj, Py_ssize_t dictoffset)
{
    Py_ssize_t tsize;
    size_t size;

    tsize = ((PyVarObject *)obj)->ob_size;
    if (tsize < 0)
        tsize = -tsize;
    PyTypeObject *tp = Py_TYPE(obj);
    size = _PyObject_VAR_SIZE(tp, tsize);
    assert(size <= PY_SSIZE_T_MAX);

    dictoffset += (Py_ssize_t)size;
    assert(dictoffset > 0);
    assert(dictoffset % SIZEOF_VOID_P == 0);
    return dictoffset;
}

/* Allocates a new PyCodeCacheRef * for a target object.  The cache is a weakly
 * reference to the object and will get cleaned up with the object gets
 * collected.  The PyCodeCacheRef * contains an array of several caches for
 * different targets (e.g. type attributes, instance attributes, etc..)
 */
PyCodeCacheRef *
_PyShadow_NewCache(PyObject *from)
{
    if (!_PyShadow_EnsureInit()) {
        return NULL;
    }

    PyObject *args = PyTuple_New(2);
    if (args == NULL) {
        return NULL;
    }

    PyTuple_SET_ITEM(args, 0, from);
    PyTuple_SET_ITEM(args, 1, weakref_callback);
    Py_INCREF(from);
    Py_INCREF(weakref_callback);

    PyWeakReference *new = (PyWeakReference *)_PyWeakref_RefType.tp_new(
        &_PyCodeCache_RefType, args, NULL);

    Py_DECREF(args);
    if (new == NULL) {
        return NULL;
    }

    return (PyCodeCacheRef *)new;
}

void
_PyShadow_LogLocation(_PyShadow_EvalState *shadow,
                      const _Py_CODEUNIT *next_instr,
                      const char *category)
{
    if (shadow->code == NULL) {
        return;
    }
    char buf[221];
    const char *filename = PyUnicode_AsUTF8(shadow->code->co_filename);
    const char *end;
    if ((end = strrchr(filename, '/')) != NULL) {
        filename = end + 1;
    }
    sprintf(buf,
            "%.60s.%.120s.%d.%ld",
            filename,
            PyUnicode_AsUTF8(shadow->code->co_name),
            shadow->code->co_firstlineno,
            next_instr - *shadow->first_instr);
    INLINE_CACHE_INCR(category, buf);
}

#define CACHE_MISS_INVALIDATE_THRESHOLD 1000

int
_PyShadow_CacheHitInvalidate(_PyShadow_EvalState *shadow,
                             const _Py_CODEUNIT *next_instr,
                             PyObject *type,
                             const char *cache_type)
{
    /* This implements the policy for when we have a cache miss.  Currently we
     * don't have any per-call site statistics or polymorphic dispatch support.
     * Instead we track the overall update count on individual code objects,
     * and if that starts creeping up we'll stop trying to re-calculate the
     * opcodes for the entire code object.  But we'll leave a single successful
     * cache in place as long as it's valid.  If it becomes invalid we'll
     * de-optimize to a generic PyObject_GetAttr.
     */
#ifdef INLINE_CACHE_PROFILE
    _PyShadow_LogLocation(shadow, next_instr, "invalidate");
#endif
    if (type == NULL &&
        shadow->shadow->update_count++ > CACHE_MISS_INVALIDATE_THRESHOLD) {
        /* we're experiencing a lot of churn on types which are being
         * invalidated within this code object.  Disable caching on the
         * type miss and fall-back to the normal get attr path */
        return 1;
    }

    return 0;
}

static int
generic_cache_grow(void **items, Py_ssize_t *size, size_t item_size);
int _PyShadow_IsCacheOpcode(unsigned char opcode);

PyObject *
_PyShadow_LoadAttrSwitchPolymorphic(_PyShadow_EvalState *state,
                                    const _Py_CODEUNIT *next_instr,
                                    PyObject *owner,
                                    PyObject *name,
                                    PyObject *type)
{
    /* Switch to a polymorphic cache */
    _PyShadow_InstanceAttrEntry ***polymorphic_caches =
        state->shadow->polymorphic_caches;

    if (polymorphic_caches == NULL) {
        polymorphic_caches =
            PyMem_Calloc(INITIAL_POLYMORPHIC_CACHE_ARRAY_SIZE,
                         sizeof(_PyShadow_InstanceAttrEntry **));
        if (polymorphic_caches == NULL) {
            return NULL;
        }
        state->shadow->polymorphic_caches = polymorphic_caches;
        state->shadow->polymorphic_caches_size =
            INITIAL_POLYMORPHIC_CACHE_ARRAY_SIZE;
    }

    /* Find a free cache entry */
    Py_ssize_t size = state->shadow->polymorphic_caches_size;
    Py_ssize_t cache_index = -1;
    for (Py_ssize_t i = 0; i < size; i++) {
        if (polymorphic_caches[i] == NULL) {
            cache_index = i;
            break;
        }
    }
    if (cache_index == -1) {
        cache_index = state->shadow->polymorphic_caches_size;
        if (!generic_cache_grow((void **)&state->shadow->polymorphic_caches,
                                &state->shadow->polymorphic_caches_size,
                                sizeof(_PyShadow_InstanceAttrEntry **))) {
            _PyShadow_LoadAttrMiss(state, next_instr, name);
            return PyObject_GetAttr(owner, name);
        }
        polymorphic_caches = state->shadow->polymorphic_caches;
    }

    /* Allocate the memory for the caches */
    _PyShadow_InstanceAttrEntry **entries = PyMem_Calloc(
        POLYMORPHIC_CACHE_SIZE, sizeof(_PyShadow_InstanceAttrEntry *));
    if (entries == NULL) {
        return NULL;
    }
    polymorphic_caches[cache_index] = entries;

    /* Switch the opcode and just run the normal polymorphic code path */
    _PyShadow_PatchByteCode(
        state, next_instr, LOAD_ATTR_POLYMORPHIC, cache_index);
    return _PyShadow_LoadAttrPolymorphic(state, next_instr, entries, owner);
}

int _PyShadow_PolymorphicCacheEnabled = 1;

PyObject *_Py_NO_INLINE
_PyShadow_LoadAttrInvalidate(_PyShadow_EvalState *state,
                             const _Py_CODEUNIT *next_instr,
                             PyObject *owner,
                             PyObject *name,
                             PyTypeObject *type)
{
    assert(_Py_OPCODE(*(next_instr - 1)) != LOAD_ATTR_POLYMORPHIC);
    assert(_PyShadow_IsCacheOpcode(_Py_OPCODE(*(next_instr - 1))));

    /* Type is coming from the cache entry.  If it is NULL then it means the
     * type has been modified and we've invalidated the cache.  If it is
     * non-NULL then it means we are seeing a different type come through
     * the call site */
    if (type != NULL && type->tp_getattro == PyObject_GenericGetAttr &&
        _PyShadow_PolymorphicCacheEnabled) {
        return _PyShadow_LoadAttrSwitchPolymorphic(
            state, next_instr, owner, name, (PyObject *)type);
    }

    if (_PyShadow_CacheHitInvalidate(
            state, next_instr, (PyObject *)type, "invalidate_attr")) {
        _PyShadow_LoadAttrMiss(state, next_instr, name);
        return PyObject_GetAttr(owner, name);
    }

    return _PyShadow_LoadAttrWithCache(state, next_instr, owner, name);
}

PyObject *_PyShadow_GetCacheForAttr(PyCodeCacheRef *cache, PyObject *name);
PyObject *_PyShadow_LoadAttrRunCacheEntry(_PyShadow_EvalState *state,
                                          const _Py_CODEUNIT *next_instr,
                                          PyObject *entry,
                                          PyObject *owner,
                                          PyObject *name);
static PyObject *_PyShadow_LoadCacheInfo(PyTypeObject *tp,
                                         PyObject *name,
                                         PyCodeCacheRef *cache);

static inline int
opsize(const _Py_CODEUNIT *instr, const _Py_CODEUNIT *first_instr)
{
    int existing_size = 0;
    assert(instr >= first_instr);

    do {
        instr--;
        existing_size++;
    } while (instr > first_instr && (_Py_OPCODE(*instr) == EXTENDED_ARG ||
                                     _Py_OPCODE(*instr) == SHADOW_NOP));
    return existing_size;
}

int _PyShadow_GetOriginalOparg(_PyShadow_EvalState *state,
                               const _Py_CODEUNIT *next_instr)
{
    _Py_CODEUNIT *rawcode = (_Py_CODEUNIT *)PyBytes_AS_STRING(state->code->co_code);
    _Py_CODEUNIT *instr = &rawcode[next_instr - *state->first_instr];
    instr--; /* we point to the next instruction, we want the current one */
    const int existing_size = opsize(instr, (_Py_CODEUNIT *)PyBytes_AsString(state->code->co_code));
    _Py_CODEUNIT *start = instr - (existing_size - 1);
    int oparg = _Py_OPARG(*start);
    while (_Py_OPCODE(*start) == EXTENDED_ARG) {
        oparg = oparg << 8;
        start++;
        oparg |= _Py_OPARG(*start);
    }
    return oparg;
}

PyObject *
_PyShadow_GetOriginalName(_PyShadow_EvalState *state,
                          const _Py_CODEUNIT *next_instr)
{
    int oparg = _PyShadow_GetOriginalOparg(state, next_instr);

    return PyTuple_GET_ITEM(state->code->co_names, oparg);
}

PyObject *
_PyShadow_GetOriginalConst(_PyShadow_EvalState *state,
                          const _Py_CODEUNIT *next_instr)
{
    int oparg = _PyShadow_GetOriginalOparg(state, next_instr);

    return PyTuple_GET_ITEM(state->code->co_consts, oparg);
}

PyObject *
_PyShadow_LoadAttrPolymorphic(_PyShadow_EvalState *state,
                              const _Py_CODEUNIT *next_instr,
                              _PyShadow_InstanceAttrEntry **entries,
                              PyObject *owner)
{
    _PyShadow_InstanceAttrEntry *entry;
    PyTypeObject *type = Py_TYPE(owner);

    Py_ssize_t index = -1;
    for (int i = 0; i < POLYMORPHIC_CACHE_SIZE; i++) {
        if (entries[i] == NULL) {
            index = i;
            break;
        }
    }

    PyObject *name = _PyShadow_GetOriginalName(state, next_instr);

    if (index == -1 || type->tp_getattro != PyObject_GenericGetAttr) {
        /* This type cannot be cached in a polymorphic cache */
        goto done;
    }

    PyCodeCacheRef *cache = _PyShadow_GetCache((PyObject *)type);
    if (cache == NULL) {
        return NULL;
    } else if (cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        goto done;
    }

    entry =
        (_PyShadow_InstanceAttrEntry *)_PyShadow_GetCacheForAttr(cache, name);
    assert(entry == NULL ||
           (Py_TYPE((PyObject *)entry) == &_PyShadow_InstanceCacheSlot.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheSplitDict.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheDictNoDescr.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheNoDictDescr.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheSplitDictMethod.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheSplitDictDescr.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheDictMethod.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheDictDescr.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheNoDictMethod.type ||
            Py_TYPE((PyObject *)entry) ==
                &_PyShadow_InstanceCacheNoDictDescr.type));

    if (entry != NULL && entry->type != NULL) {
        /* We have an existing valid cache, re-use it */
        assert(entry->type == type);
        Py_INCREF(entry);
        goto run_entry;
    }

    entry = (_PyShadow_InstanceAttrEntry *)_PyShadow_LoadCacheInfo(
        type, name, cache);
    if (entry != NULL) {
        goto run_entry;
    }

done:
    return PyObject_GetAttr(owner, name);
run_entry:
    entries[index] = entry;

    _PyCacheType *cache_type = (_PyCacheType *)((PyObject *)entry)->ob_type;

    return cache_type->load_func(state, next_instr, (PyObject *)entry, owner);
}

/* Private API for the LOAD_METHOD opcode. */
extern int _PyObject_GetMethod(PyObject *, PyObject *, PyObject **);

int
_PyShadow_LoadMethodInvalidate(_PyShadow_EvalState *shadow,
                               const _Py_CODEUNIT *next_instr,
                               PyObject *owner,
                               PyObject *name,
                               PyObject *type,
                               PyObject **meth)
{
    if (_PyShadow_CacheHitInvalidate(
            shadow, next_instr, type, "invalidate_method")) {
        _PyShadow_LoadMethodMiss(shadow, next_instr, name);
        return _PyObject_GetMethod(owner, name, meth);
    }

    return _PyShadow_LoadMethodWithCache(
        shadow, next_instr, owner, name, meth);
}

int _PyShadow_IsCacheOpcode(unsigned char opcode);
int
_PyShadow_AttrMiss(_PyShadow_EvalState *shadow,
                   const _Py_CODEUNIT *next_instr,
                   PyObject *name,
                   int opcode)
{
    for (Py_ssize_t i = 0; i < PyTuple_Size(shadow->code->co_names); i++) {
        if (PyUnicode_Compare(PyTuple_GetItem(shadow->code->co_names, i),
                              name) == 0) {
            INLINE_CACHE_RECORD_STAT(_Py_OPCODE(*next_instr), misses);
            /* deoptimize the call site */
            return _PyShadow_PatchByteCode(shadow, next_instr, opcode, i);
        }
    }
    assert(0);
    return -1;
}

int
_PyShadow_LoadAttrMiss(_PyShadow_EvalState *shadow,
                       const _Py_CODEUNIT *next_instr,
                       PyObject *name)
{
    return _PyShadow_AttrMiss(shadow, next_instr, name, LOAD_ATTR_UNCACHABLE);
}

int
_PyShadow_StoreAttrMiss(_PyShadow_EvalState *shadow,
                        const _Py_CODEUNIT *next_instr,
                        PyObject *name)
{
    return _PyShadow_AttrMiss(shadow, next_instr, name, STORE_ATTR_UNCACHABLE);
}

int
_PyShadow_LoadMethodMiss(_PyShadow_EvalState *shadow,
                         const _Py_CODEUNIT *next_instr,
                         PyObject *name)
{
    return _PyShadow_AttrMiss(
        shadow, next_instr, name, LOAD_METHOD_UNCACHABLE);
}

int
_PyShadow_PatchByteCode(_PyShadow_EvalState *state,
                        const _Py_CODEUNIT *next_instr,
                        int op,
                        int arg)
{
    _Py_CODEUNIT *instr =
        &state->shadow->code[next_instr - *state->first_instr];
    instr--; /* we point to the next instruction, we want the current one */

    const int existing_size = opsize(instr, state->shadow->code);
    /* get where this opcode actually starts including EXTENDED_ARGs and
     * SHADOW_NOPs */
    _Py_CODEUNIT *start = instr - (existing_size - 1);
    int newsize = instrsize(arg);
    if (newsize <= existing_size) {
        if (_PyShadow_IsCacheOpcode(_Py_OPCODE(*instr))) {
            /* If we're replacing an existing cache w/ a new cache entry then
             * we'll dec ref the old cache entry.  Grab the old entry now, and
             * null it out if we're the last reference to it so we can replace
             * it.
             */
            _ShadowCache *cache = &state->shadow->l1_cache;
            PyObject *old_entry = cache->items[_Py_OPARG(*instr)];
            assert(old_entry != NULL);
            if (old_entry->ob_refcnt == 1) {
                assert(!_PyShadow_IsCacheValid(old_entry));
                assert(_Py_OPARG(*instr) < cache->size);

                cache->items[_Py_OPARG(*instr)] = NULL;
            }
            Py_DECREF(old_entry);
        }

        /* Usually we'll be the same as the existing opcode, but if we're
         * writing a smaller instruction than exists then we'll insert
         * SHADOW_NOPs at the beginning.  We don't use normal NOPs
         * because we can't disambiguate them from normal NOPs if we
         * expand the instruction back up in size */
        for (int i = newsize; i < existing_size; i++) {
            write_op_arg(start++, SHADOW_NOP, 0, 1);
        }
        write_op_arg(start, op, arg, newsize);
        return 0;
    }
    return 1;
}

static void
cache_init(_ShadowCache *cache)
{
    cache->items = NULL;
    cache->size = 0;
}

static int
generic_cache_grow(void **items, Py_ssize_t *size, size_t item_size)
{
    const Py_ssize_t initial_size = 4;
    Py_ssize_t new_size = *items == NULL ? initial_size : *size * 2;
    if (new_size > 256) {
        /* Cache beyond 256 not currently supported */
        return 0;
    }
    char *new = PyMem_Realloc(*items, item_size * new_size);
    if (new == NULL) {
        return 0;
    }
    memset(&new[*size * item_size], 0, item_size * (new_size - *size));
    *items = new;
    *size = new_size;
    return 1;
}

static int
shadow_cache_grow(_ShadowCache *cache)
{
    return generic_cache_grow(
        (void **)&cache->items, &cache->size, sizeof(PyObject *));
}

static Py_ssize_t
_PyShadow_CacheFind(_ShadowCache *cache,
                    _PyShadow_EvalState *state,
                    PyObject *existing)
{
    /* scan for an existing item in the cache we can re-use */
    for (Py_ssize_t i = 0; i < cache->size; i++) {
        if (cache->items[i] == existing) {
            return i;
        }
    }
    return -1;
}

static Py_ssize_t
_PyShadow_CacheAdd(_ShadowCache *cache,
                   _PyShadow_EvalState *state,
                   PyObject *from)
{
    for (Py_ssize_t i = 0; i < cache->size; i++) {
        if (cache->items[i] == NULL) {
            cache->items[i] = from;
            return i;
        }
    }

    size_t index = cache->size;
    if (shadow_cache_grow(cache)) {
        cache->items[index] = from;
        return index;
    }
    return -1;
}

int
_PyShadow_CacheCastType(_PyShadow_EvalState *state, PyObject *type)
{
    int index = _PyShadow_CacheFind(&state->shadow->cast_cache, state, type);
    if (index != -1) {
        return index;
    }

    index = _PyShadow_CacheAdd(&state->shadow->cast_cache, state, type);
    if (index != -1) {
        Py_INCREF(type);
    }
    return index;
}

int
_PyShadow_CacheFieldType(_PyShadow_EvalState *state, int offset, int type)
{
    _FieldCache *caches = state->shadow->field_caches;
    Py_ssize_t cache_size = state->shadow->field_cache_size;
    if (caches != NULL) {
        for (Py_ssize_t i = 0; i < cache_size; i++) {
            if (caches[i].offset == offset && caches[i].type == type) {
                return i;
            }
        }
    }

    Py_ssize_t new_cache_size;
    if (cache_size != 0) {
        new_cache_size = cache_size * 2;
        if (new_cache_size >= 256) {
            return -1;
        }
    } else {
        new_cache_size = 4;
    }

    caches = PyMem_Realloc(caches, sizeof(_FieldCache) * new_cache_size);
    if (caches == NULL) {
        return -1;
    }
    caches[cache_size].offset = offset;
    caches[cache_size].type = type;
    state->shadow->field_caches = caches;
    state->shadow->field_cache_size = new_cache_size;
    return cache_size;
}

typedef int (*attr_miss_invalidate_func)(_PyShadow_EvalState *state,
                                         const _Py_CODEUNIT *next_instr,
                                         PyObject *name);

int
_PyShadow_IsCacheOpcode(unsigned char opcode)
{
    switch (opcode) {
    case LOAD_ATTR_TYPE:
    case LOAD_ATTR_SLOT:
    case LOAD_ATTR_NO_DICT_DESCR:
    case LOAD_ATTR_DICT_DESCR:
    case LOAD_ATTR_DICT_NO_DESCR:
    case LOAD_ATTR_SPLIT_DICT:
    case LOAD_ATTR_SPLIT_DICT_DESCR:
    case LOAD_ATTR_MODULE:
    case LOAD_ATTR_S_MODULE:
    case LOAD_METHOD_SPLIT_DICT_DESCR:
    case LOAD_METHOD_DICT_DESCR:
    case LOAD_METHOD_NO_DICT_DESCR:
    case LOAD_METHOD_DICT_METHOD:
    case LOAD_METHOD_SPLIT_DICT_METHOD:
    case LOAD_METHOD_NO_DICT_METHOD:
    case LOAD_METHOD_TYPE:
    case LOAD_METHOD_MODULE:
    case LOAD_METHOD_S_MODULE:
    case STORE_ATTR_DICT:
    case STORE_ATTR_DESCR:
    case STORE_ATTR_SPLIT_DICT:
    case STORE_ATTR_SLOT:
        return 1;
    }
    return 0;
}

void
_PyShadow_PatchOrMiss(_PyShadow_EvalState *state,
                      const _Py_CODEUNIT *next_instr,
                      int opcode,
                      PyObject *entry,
                      PyObject *name,
                      attr_miss_invalidate_func miss)
{
    _ShadowCache *cache = &state->shadow->l1_cache;
    _Py_CODEUNIT *instr =
        &state->shadow->code[next_instr - *state->first_instr];
    instr--; /* we point to the next instruction, we want the current one */

    Py_ssize_t index = _PyShadow_CacheFind(cache, state, entry);
    if (index != -1 ||
        (index = _PyShadow_CacheAdd(cache, state, entry)) != -1) {
        Py_INCREF(entry);
        if (_PyShadow_PatchByteCode(state, next_instr, opcode, index)) {
            Py_DECREF(entry);
        }
    } else {
        /* Mark as uncachable if we're out of entries */
        miss(state, next_instr, name);
    }
}

void
_PyShadow_InitGlobal(_PyShadow_EvalState *state,
                     const _Py_CODEUNIT *next_instr,
                     PyObject *globals,
                     PyObject *builtins,
                     PyObject *name)
{
    if (!_PyShadow_EnsureInit() || !_PyDict_CanWatch(builtins) ||
        !_PyDict_CanWatch(globals)) {
        return;
    }

    PyObject **cache = _PyJIT_GetGlobalCache(builtins, globals, name);
    if (cache == NULL) {
        return;
    }
    assert(*cache != NULL);

    _PyShadowCode *shadow = state->shadow;
    for (Py_ssize_t i = 0; i < shadow->globals_size; i++) {
        if (shadow->globals[i] == cache || shadow->globals[i] == NULL) {
            shadow->globals[i] = cache;
            _PyShadow_PatchByteCode(state, next_instr, LOAD_GLOBAL_CACHED, i);
            return;
        }
    }

    assert(0 && "No empty cache slots");
}

int _PyShadow_CacheFunction(_PyShadow_EvalState *state, PyObject **func)
{
    if (func == NULL) {
        return -1;
    }
    _PyShadowCode *shadow = state->shadow;
    for (Py_ssize_t i = 0; i < shadow->functions_size; i++) {
        if (shadow->functions[i] == func || shadow->functions[i] == NULL) {
            shadow->functions[i] = func;
            return i;
        }
    }

    return -1;
}

void
_PyShadow_SetLoadAttrError(PyObject *obj, PyObject *name)
{
    PyObject *mod_name;
    PyTypeObject *tp = Py_TYPE(obj);
    if (PyModule_CheckExact(obj)) {
        PyErr_Clear();
        PyObject *mod_dict = ((PyModuleObject *)obj)->md_dict;
        if (mod_dict) {
            _Py_IDENTIFIER(__name__);
            mod_name = _PyDict_GetItemIdWithError(mod_dict, &PyId___name__);
            assert((mod_name || !PyErr_Occurred()) &&
                   "_PyDict_GetItemIdWithError should only fail with invalid identifiers");
            if (mod_name && PyUnicode_Check(mod_name)) {
                PyErr_Format(PyExc_AttributeError,
                             "module '%U' has no attribute '%U'",
                             mod_name,
                             name);
                return;
            }
        }
        PyErr_Format(PyExc_AttributeError,
                    "module has no attribute '%U'", name);
    } else
    if (PyStrictModule_CheckExact(obj)) {
        PyErr_Clear();
        PyStrictModuleObject *m = (PyStrictModuleObject *)obj;
        if (m->globals) {
            _Py_IDENTIFIER(__name__);
            mod_name = _PyDict_GetItemIdWithError(m->globals, &PyId___name__);
            assert((mod_name || !PyErr_Occurred()) &&
                   "_PyDict_GetItemIdWithError should only fail with invalid identifiers");
            if (mod_name && PyUnicode_Check(mod_name)) {
                PyErr_Format(PyExc_AttributeError,
                            "module '%U' has no attribute '%U'", mod_name, name);
                return;
            }
        }
        PyErr_Format(PyExc_AttributeError,
                    "module has no attribute '%U'", name);
    } else
    {
        PyErr_Format(PyExc_AttributeError,
                     "'%.50s' object has no attribute '%U'",
                     tp->tp_name,
                     name);
    }
}

#ifdef INLINE_CACHE_PROFILE
static PyObject *type_stats;
#endif

/* Sets up the relationship between a metatype and a type for cache
 * invalidation. The metatype gets a dictionary which Dict[codecache,
 * Dict[name, cacheentry]]. This allows invalidating cache entries if the meta
 * type is mtuated. The type gets a back pointer to the meta type. This allows
 * clearing the caches from the meta type when the type is destroyed or when
 * the type's meta type is changed.
 */
int
_PyShadow_RegisterMetaRelationship(PyObject *type,
                                   PyObject *name,
                                   PyObject *cache_entry)
{
    PyTypeObject *metatype = Py_TYPE(type);
    PyCodeCacheRef *metacache, *cache;

    if (metatype == &PyType_Type) {
        return 0;
    }

    /* we need to invalidate this cache if the metaclass has a
     * __getattribute__ attached to it later */
    cache = _PyShadow_GetCache(type);
    if (cache == NULL) {
        return -1;
    }

    metacache = _PyShadow_GetCache((PyObject *)metatype);
    if (metacache == NULL) {
        return -1;
    }

    if (cache->metatype == NULL) {
        cache->metatype = (PyObject *)metacache;
        Py_INCREF(metacache);
    }

    assert(cache->metatype == (PyObject *)metacache);

    if (metacache->type_insts == NULL) {
        metacache->type_insts = PyDict_New();
        if (metacache->type_insts == NULL) {
            return -1;
        }
    }

    PyObject *existing_dict = PyDict_GetItem(metacache->type_insts, type);
    if (existing_dict == NULL) {
        existing_dict = PyDict_New();
        if (existing_dict == NULL) {
            return -1;
        }

        if (PyDict_SetItem(
                metacache->type_insts, (PyObject *)cache, existing_dict)) {
            return -1;
        }
        Py_DECREF(existing_dict);
    }

    return PyDict_SetItem(existing_dict, name, cache_entry);
}

static int
_PyShadow_IsMethod(PyObject *descr)
{
    return PyFunction_Check(descr) || (Py_TYPE(descr) == &PyMethodDescr_Type);
}

static PyObject *
_PyShadow_NewCacheEntry(_PyCacheType *cache_type)
{
    INLINE_CACHE_ENTRY_CREATED(opcode, cache_type->tp_basicsize);
    return cache_type->type.tp_alloc(&cache_type->type, 0);
}

static uintptr_t
get_load_method_type_data(PyObject *descr)
{
    if (descr == NULL) {
        return _PyShadow_LoadMethodTypeDataUnset;
    }

    PyObject *obj = NULL;
    _PyShadow_MethCallKind kind;
    if (_PyClassMethod_Check(descr)) {
        obj = Ci_PyClassMethod_GetFunc(descr);
        kind = PYSHADOW_CALL_UNBOUND;
    } else if (_PyStaticMethod_Check(descr)) {
        obj = Ci_PyStaticMethod_GetFunc(descr);
        kind = PYSHADOW_CALL_NOT_UNBOUND;
    } else if (_PyWrapperDescr_Check(descr) ||
               _PyShadow_IsMethod(descr) ||
               PyCFunction_Check(descr)) {
        obj = descr;
        kind = PYSHADOW_CALL_NOT_UNBOUND;
    } else {
        return _PyShadow_LoadMethodTypeDataUnset;
    }

    if ((obj == NULL) || !Py_IS_IMMORTAL(obj)) {
        return _PyShadow_LoadMethodTypeDataUnset;
    }

    return _PyShadow_MakeLoadMethodTypeData(obj, kind);
}

static _PyShadow_InstanceAttrEntry *
_PyShadow_NewInstanceCache(_PyCacheType *cache_type,
                           PyObject *name,
                           PyTypeObject *type,
                           PyObject *value)
{
    _PyShadow_InstanceAttrEntry *res =
        (_PyShadow_InstanceAttrEntry *)_PyShadow_NewCacheEntry(cache_type);
    if (res != NULL) {
        res->name = PyUnicode_FromObject(name);
        if (res->name == NULL) {
            Py_DECREF(res);
            return NULL;
        }
        PyObject_Hash(res->name);
        res->type = type;
        res->value = value;
        res->load_method_type_data = get_load_method_type_data(value);
    }
    return res;
}

#define CACHED_KEYS(tp) (((PyHeapTypeObject *)tp)->ht_cached_keys)

/* Looks up an existing cache for a given name. Returns a borrowed reference */
PyObject *
_PyShadow_GetCacheForAttr(PyCodeCacheRef *cache, PyObject *name)
{
    PyObject *type_cache = cache->l2_cache;

    if (type_cache != NULL) {
        /* Find existing cache entry from our weakref in the type */
        return PyDict_GetItem(type_cache, name);
    }
    return NULL;
}

int
_PyShadow_AddCacheForAttr(PyCodeCacheRef *cache,
                          PyObject *name,
                          PyObject *entry)
{
    if (cache->l2_cache == NULL) {
        cache->l2_cache = PyDict_New();
        if (cache->l2_cache == NULL) {
            return -1;
        }
    }

    /* If we have an existing entry it shouldn't be valid any more.  But
     * we still want to call invalidate it.  The existing l2 cache entry
     * may be floating around in l1 caches.  By replacing it in the l2
     * cache now we won't get the opportunity to call invalidate on it
     * when the object is freed.  So we eagerly call invalidate now to make
     * sure it has no dangling references to the object that would fire
     * when we encounter it later. */
    PyObject *existing = PyDict_GetItem(cache->l2_cache, name);
    if (existing != NULL) {
        assert(!_PyShadow_IsCacheValid(existing));
        ((_PyCacheType *)existing->ob_type)->invalidate(existing);
    }

    if (PyDict_SetItem(cache->l2_cache, name, (PyObject *)entry)) {
        return -1;
    }

    return 0;
}

static PyObject *
_PyShadow_LoadCacheInfo(PyTypeObject *tp,
                        PyObject *name,
                        PyCodeCacheRef *cache)
{
    Py_ssize_t dictoffset = 0;
    int nentries = 0;
    Py_ssize_t splitoffset = 0;
    _PyCacheType *cache_type;
    _PyShadow_InstanceAttrEntry *entry;
    PyObject *descr = _PyType_Lookup(tp, name);

    if (tp->tp_dict == NULL && PyType_Ready(tp) < 0) {
        return NULL;
    }

    /* Cache miss, need to perform MRO walk */
    if (!PyType_HasFeature(tp, Py_TPFLAGS_VALID_VERSION_TAG)) {
        /* Obj's type doesn't use PyObject_GenericGetAttr.
           and it's not a module. Mark this call site as uncacheable.
        */
        INLINE_CACHE_UNCACHABLE_TYPE(tp);
        return NULL;
    }

    /* Inline _PyObject_GetDictPtr */
    if (PyType_IsSubtype(tp, &PyStrictModule_Type)) {
        dictoffset = strictmodule_dictoffset;
    } else
    {
      dictoffset = tp->tp_dictoffset;
    }

    if (descr != NULL) {
        descrgetfunc f = descr->ob_type->tp_descr_get;

        if (f != NULL && PyDescr_IsData(descr)) {
            /* data descriptor takes precedence requires no dictionary access,
             * but first see if it's one we specialize */
            if (Py_TYPE(descr) == &PyMemberDescr_Type) {
                PyMemberDescrObject *member = (PyMemberDescrObject *)descr;
                if (member->d_member->type == T_OBJECT_EX &&
                    !(member->d_member->flags & READONLY)) {
                    splitoffset = member->d_member->offset;
                    cache_type = &_PyShadow_InstanceCacheSlot;
                    goto done;
                }
            } else if (Py_TYPE(descr) == &PyCachedProperty_Type) {
                PyCachedPropertyDescrObject *member =
                    (PyCachedPropertyDescrObject *)descr;
                if (Py_TYPE(member->name_or_descr) == &PyMemberDescr_Type) {
                    /* cached property backed by a slot */
                    splitoffset =
                        ((PyMemberDescrObject *)member->name_or_descr)
                            ->d_member->offset;
                    cache_type = &_PyShadow_InstanceCacheSlot;
                    goto done;
                }

                /* cached property backed by a dict */
                cache_type = &_PyShadow_InstanceCacheDictNoDescr;
                goto done;
            }

            /* not a special data descriptor */
            cache_type = &_PyShadow_InstanceCacheNoDictDescr;
            goto done;
        }
    }

    if (dictoffset != 0) {
        PyDictKeysObject *cached;
        if ((tp->tp_flags & Py_TPFLAGS_HEAPTYPE) &&
            (cached = CACHED_KEYS(tp))) {
            /* we have a split dict, we can access the slot directly */
            splitoffset = _PyDictKeys_GetSplitIndex(cached, name);
            nentries = cached->dk_nentries;

            if (descr == NULL) {
                cache_type = &_PyShadow_InstanceCacheSplitDict;
            } else if (_PyShadow_IsMethod(descr)) {
                cache_type = &_PyShadow_InstanceCacheSplitDictMethod;
            } else {
                cache_type = &_PyShadow_InstanceCacheSplitDictDescr;
            }
        } else if (descr == NULL) {
            cache_type = &_PyShadow_InstanceCacheDictNoDescr;
        } else if (_PyShadow_IsMethod(descr)) {
            cache_type = &_PyShadow_InstanceCacheDictMethod;
        } else {
            cache_type = &_PyShadow_InstanceCacheDictDescr;
        }
    } else if (descr != NULL) {
        if (_PyShadow_IsMethod(descr)) {
            cache_type = &_PyShadow_InstanceCacheNoDictMethod;
        } else {
            cache_type = &_PyShadow_InstanceCacheNoDictDescr;
        }
    } else {
        /* we have no descriptor, and no dictionary, we can't fin the attr */
        return NULL;
    }

done:
    entry = _PyShadow_NewInstanceCache(cache_type, name, tp, descr);
    if (entry == NULL) {
        return NULL;
    }
    entry->dictoffset = dictoffset;
    entry->splitoffset = splitoffset;
    entry->nentries = nentries;

    if (_PyShadow_AddCacheForAttr(cache, name, (PyObject *)entry)) {
        Py_DECREF(entry);
        return NULL;
    }
    return (PyObject *)entry;
}

PyObject *
_PyShadow_LoadAttrRunCacheEntry(_PyShadow_EvalState *state,
                                const _Py_CODEUNIT *next_instr,
                                PyObject *entry,
                                PyObject *owner,
                                PyObject *name)
{
    _PyCacheType *cache_type = (_PyCacheType *)((PyObject *)entry)->ob_type;
    int opcode = cache_type->load_attr_opcode;

    _PyShadow_PatchOrMiss(
        state, next_instr, opcode, entry, name, &_PyShadow_LoadAttrMiss);

    return cache_type->load_func(state, next_instr, entry, owner);
}

static int
_PyShadow_LoadAttrTryCacheHit(_PyShadow_EvalState *state,
                              const _Py_CODEUNIT *next_instr,
                              PyObject *owner,
                              PyObject *name,
                              PyCodeCacheRef *cache,
                              PyObject **res)
{
    if (cache == NULL || cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        _PyShadow_LoadAttrMiss(state, next_instr, name);
        *res = PyObject_GetAttr(owner, name);
        return 1;
    }

    PyObject *entry = _PyShadow_GetCacheForAttr(cache, name);
    if (entry != NULL && _PyShadow_IsCacheValid(entry)) {
        /* We have an existing valid cache, re-use it */
        *res = _PyShadow_LoadAttrRunCacheEntry(
            state, next_instr, entry, owner, name);
        return 1;
    }
    return 0;
}

/* When accessing an attribute defined on a type we might have a conflicting
 * attribute on the meta-type.  If that's a descriptor it takes precedence.  If
 * it's a data descriptor it will always take precedence.  If it's a non-data
 * descriptor it will only take precedence if the attribute isn't defined on
 * the type.  Ultimately we want to support common attributes like
 * object.__setattr__ and object.__init__.
 */
int
_PyShadow_IsUncachableMetaAttr(PyObject *descr)
{
    return descr != NULL && PyDescr_IsData(descr);
}

int
_PyShadow_IsMetaclassAttrConflict(PyObject *entry, PyObject *descr)
{
    return entry != NULL && descr != NULL &&
           ((_PyShadow_InstanceAttrEntry *)entry)->value == NULL;
}

/* Attempts to resolve an attribute on a type.  Returns 1 if we fail to resolve
 * it and have performed no actions.  Returns 0
 * on success with a valid result or NULL and an exception set */
int
_PyShadow_GetAttrType(_PyShadow_EvalState *state,
                      const _Py_CODEUNIT *next_instr,
                      PyObject *type,
                      PyObject *name,
                      PyObject **res)
{
    /* types are special because their caches can be either for instances or
     * types but we only have one type load opcode */
    PyObject *descr = _PyType_Lookup(Py_TYPE(type), name);
    PyObject *entry;
    PyCodeCacheRef *cache;

    if (_PyShadow_IsUncachableMetaAttr(descr)) {
        /* meta-type attribute or meta-type defines custom __getattr__ */
        return 1;
    }

    cache = _PyShadow_GetCache(type);
    if (cache == NULL || cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        return 1;
    }

    entry = _PyShadow_GetCacheForAttr(cache, name);
    if (entry != NULL && _PyShadow_IsCacheValid(entry)) {
        /* We have an existing valid cache, re-use it */
        Py_INCREF(entry);
        goto run_entry;
    }

    entry = _PyShadow_LoadCacheInfo((PyTypeObject *)type, name, cache);
    if (entry == NULL) {
        return 1;
    }

    /* If we don't have a descriptor (the entry value is NULL) then we don't
     * have the attribute */
    if (((_PyShadow_InstanceAttrEntry *)entry)->value == NULL ||
        _PyShadow_RegisterMetaRelationship(
            (PyObject *)type, name, (PyObject *)entry)) {
        Py_DECREF(entry);
        return 1;
    }
run_entry:
    if (_PyShadow_IsMetaclassAttrConflict(entry, descr)) {
        Py_DECREF(entry);
        return 1;
    }

    _PyShadow_PatchOrMiss(state,
                          next_instr,
                          LOAD_ATTR_TYPE,
                          entry,
                          name,
                          &_PyShadow_LoadAttrMiss);
    *res = _PyShadow_LoadAttrType(
        state, next_instr, (_PyShadow_InstanceAttrEntry *)entry, type);
    Py_DECREF(entry);
    return 0;
}

static int
_PyShadow_GetAttrModule(_PyShadow_EvalState *state,
                        const _Py_CODEUNIT *next_instr,
                        PyObject *owner,
                        PyObject *name,
                        PyObject **res)
{
    PyCodeCacheRef *cache = _PyShadow_GetCache(owner);
    PyObject *getattr;

    if (_PyShadow_LoadAttrTryCacheHit(
            state, next_instr, owner, name, cache, res)) {
        return 0;
    }

    PyObject *dict = PyModule_Dict(owner);

    if (dict) {

        int version;
        PyObject *value;

        if (PyStrictModule_Check(owner)) {
            version = PYCACHE_STRICT_MODULE_VERSION(owner);
            if (strictmodule_is_unassigned(dict, name) == 0) {
                value = _PyDict_GetAttrItem(dict, name);
            } else {
                value = NULL;
            }
        } else
        {
            version = PYCACHE_MODULE_VERSION(owner);
            value = _PyDict_GetAttrItem(dict, name);
        }

        if (value != NULL) {
            Py_INCREF(
                value); /* value was borrowed, and we need a ref to return */
            *res = value;

            _PyShadow_ModuleAttrEntry *entry;
            if (PyStrictModule_Check(owner)) {
                entry = (_PyShadow_ModuleAttrEntry *)_PyShadow_NewCacheEntry(
                    &_PyShadow_StrictModuleAttrEntryType);
            } else
            {
                entry = (_PyShadow_ModuleAttrEntry *)_PyShadow_NewCacheEntry(
                    &_PyShadow_ModuleAttrEntryType);
            }

            if (entry == NULL) {
                return 0;
            }

            entry->value = value;
            entry->module = owner;
            entry->name = PyUnicode_FromObject(name);
            if (entry->name == NULL) {
                Py_DECREF(entry);
                return 0;
            }
            PyObject_Hash(entry->name);
            entry->module = owner;
            entry->version = version;

            if (_PyShadow_AddCacheForAttr(cache, name, (PyObject *)entry)) {
                Py_DECREF(entry);
                return 0;
            }

            /* we just update the cache and return the value directly here in
             * case the lookup modifies the module via a key in the modules
             * dict which matches the string w/ a custom __eq__/__hash__ */
            _PyShadow_PatchOrMiss(state,
                                  next_instr,
                                  PyStrictModule_Check(owner) ? LOAD_ATTR_S_MODULE : LOAD_ATTR_MODULE,
                                  (PyObject *)entry,
                                  name,
                                  &_PyShadow_LoadAttrMiss);

            Py_DECREF(entry);
            return 0;
        }

        if (PyErr_Occurred()) {
            *res = NULL;
            return 0;
        }

        _Py_IDENTIFIER(__getattr__);
        getattr = _PyDict_GetItemIdWithError(dict, &PyId___getattr__);
        assert((getattr || !PyErr_Occurred()) &&
               "_PyDict_GetItemIdWithError should only fail with invalid identifiers");
        if (getattr) {
            PyObject *stack[1] = {name};
            *res = _PyObject_FastCall(getattr, stack, 1);
        } else {
            *res = NULL;
            _PyShadow_SetLoadAttrError(owner, name);
        }
        return 0;
    }
    return 1;
}

static int test_func(PyObject *name) {
    if (name == NULL) {
        return 1;
    }
    return 0;
}

PyObject *
_PyShadow_LoadAttrWithCache(_PyShadow_EvalState *state,
                           const _Py_CODEUNIT *next_instr,
                           PyObject *owner,
                           PyObject *name)
{

    PyTypeObject *type = Py_TYPE(owner);
    PyObject *res, *entry;
    PyObject *dir_str = PyUnicode_FromString("__dir__");
    int i = 0;
    if (name != NULL && PyUnicode_Find(name, dir_str, 0, PyUnicode_GET_LENGTH(name), 1) != -1) {
        i = test_func(name);
    }
    if (i) {
        return NULL;
    }
    Py_XDECREF(dir_str);

    if (type->tp_getattro != PyObject_GenericGetAttr) {
        /* "rare"" types which overrides getattr, or something unsupported */
        PyObject *res;

        if (type->tp_getattro == PyType_Type.tp_getattro) {
            if (_PyShadow_GetAttrType(state, next_instr, owner, name, &res)) {
                goto cache_miss;
            }
            return res;
        } else if (
            type->tp_getattro == PyModule_Type.tp_getattro
            || type->tp_getattro == PyStrictModule_Type.tp_getattro
        ) {
            PyObject *descr = _PyType_Lookup(type, name);
            if (descr == NULL) {
                if (_PyShadow_GetAttrModule(
                        state, next_instr, owner, name, &res)) {
                    goto cache_miss;
                }
                return res;
            }
            /* Fall through to let the descriptor be handled */
        }  else {
            goto cache_miss;
        }
    }

    /* See if we have an existing cache and if so just execute it */
    PyCodeCacheRef *cache = _PyShadow_GetCache((PyObject *)type);
    if (_PyShadow_LoadAttrTryCacheHit(
            state, next_instr, owner, name, cache, &res)) {
        return res;
    }

    /* Resolve the information for the type member */
    entry = _PyShadow_LoadCacheInfo(type, name, cache);
    if (entry == NULL) {
        goto cache_miss;
    }

    res =
        _PyShadow_LoadAttrRunCacheEntry(state, next_instr, entry, owner, name);
    Py_DECREF(entry);
    return res;
cache_miss:
    _PyShadow_LoadAttrMiss(state, next_instr, name);
    return PyObject_GetAttr(owner, name);
}

int
_PyShadow_LoadMethodRunCacheEntry(_PyShadow_EvalState *state,
                                  const _Py_CODEUNIT *next_instr,
                                  PyObject *entry,
                                  PyObject *owner,
                                  PyObject *name,
                                  PyObject **method)
{
    _PyCacheType *cache_type = (_PyCacheType *)((PyObject *)entry)->ob_type;
    int opcode = cache_type->load_method_opcode;

    // We can use a specialized opcode that immediately returns the cached
    // method without any reference counting if:
    //   1. The method is immortal.
    //   2. There are no instances of the type with attributes that shadow
    //      methods. Caches are invalidated if shadowing occurs and
    //      the bytecode is replaced with one of the other specialized
    //      versions (e.g. LOAD_METHOD_{SPLIT,NO,COMBINED}_DICT_METHOD)
    PyObject *descr = ((_PyShadow_InstanceAttrEntry *) entry)->value;
    if ((descr != NULL) && Py_IS_IMMORTAL(descr)) {
        if (cache_type == &_PyShadow_InstanceCacheNoDictMethod) {
            // No instance dictionary so shadowing cannot occur
            opcode = LOAD_METHOD_UNSHADOWED_METHOD;
        }
        else if (PyType_HasFeature(Py_TYPE(owner), Py_TPFLAGS_NO_SHADOWING_INSTANCES) &&
                   ((cache_type == &_PyShadow_InstanceCacheDictMethod) ||
                    (cache_type == &_PyShadow_InstanceCacheSplitDictMethod))) {
            opcode = LOAD_METHOD_UNSHADOWED_METHOD;
        }
    }

    _PyShadow_PatchOrMiss(
        state, next_instr, opcode, entry, name, &_PyShadow_LoadMethodMiss);

    return cache_type->load_method(state,
                                   next_instr,
                                   (_PyShadow_InstanceAttrEntry *)entry,
                                   owner,
                                   method);
}

static int
_PyShadow_LoadMethodTryCacheHit(_PyShadow_EvalState *state,
                                const _Py_CODEUNIT *next_instr,
                                PyObject *owner,
                                PyObject *name,
                                PyCodeCacheRef *cache,
                                PyObject **method,
                                int *meth_found)
{
    if (cache == NULL || cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        _PyShadow_LoadMethodMiss(state, next_instr, name);
        *meth_found = _PyObject_GetMethod(owner, name, method);
        return 1;
    }

    PyObject *entry = _PyShadow_GetCacheForAttr(cache, name);
    if (entry != NULL && _PyShadow_IsCacheValid(entry)) {
        /* We have an existing valid cache, re-use it */
        *meth_found = _PyShadow_LoadMethodRunCacheEntry(
            state, next_instr, entry, owner, name, method);
        return 1;
    }

    return 0;
}

static int
_PyShadow_LoadMethodFromType(_PyShadow_EvalState *state,
                             const _Py_CODEUNIT *next_instr,
                             PyObject *type,
                             PyObject *name,
                             PyObject **method)
{
    PyObject *descr = _PyType_Lookup(Py_TYPE(type), name);
    PyObject *entry;
    PyCodeCacheRef *cache;

    if (_PyShadow_IsUncachableMetaAttr(descr)) {
        /* meta-type attribute or meta-type defines custom __getattr__ */
        return 0;
    }

    cache = _PyShadow_GetCache(type);
    if (cache == NULL || cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        return 0;
    }

    entry = _PyShadow_GetCacheForAttr(cache, name);
    if (entry != NULL && _PyShadow_IsCacheValid(entry)) {
        /* We have an existing valid cache, re-use it */
        Py_INCREF(entry);
        goto run_entry;
    }

    entry = _PyShadow_LoadCacheInfo((PyTypeObject *)type, name, cache);
    if (entry == NULL) {
        return 0;
    }

    if (((_PyShadow_InstanceAttrEntry *)entry)->value == NULL ||
        _PyShadow_RegisterMetaRelationship(
            (PyObject *)type, name, (PyObject *)entry)) {
        Py_DECREF(entry);
        return 0;
    }
run_entry:
    if (_PyShadow_IsMetaclassAttrConflict(entry, descr)) {
        Py_DECREF(entry);
        return 0;
    }

    int opcode;
    _PyShadow_InstanceAttrEntry *iae = (_PyShadow_InstanceAttrEntry *) entry;
    if (iae->load_method_type_data == _PyShadow_LoadMethodTypeDataUnset) {
        opcode = LOAD_METHOD_TYPE;
    } else {
        opcode = LOAD_METHOD_TYPE_METHODLIKE;
    }
    _PyShadow_PatchOrMiss(state,
                          next_instr,
                          opcode,
                          entry,
                          name,
                          &_PyShadow_LoadMethodMiss);
    int res = _PyShadow_LoadMethodType(
        state, next_instr, (_PyShadow_InstanceAttrEntry *)entry, type, method);
    Py_DECREF(entry);
    return res;
}

int
_PyShadow_LoadMethodFromModule(_PyShadow_EvalState *state,
                               const _Py_CODEUNIT *next_instr,
                               PyObject *obj,
                               PyObject *name,
                               PyObject **method)
{
    PyCodeCacheRef *cache = _PyShadow_GetCache(obj);
    int meth_found;

    if (_PyShadow_LoadMethodTryCacheHit(
            state, next_instr, obj, name, cache, method, &meth_found)) {
        return meth_found;
    }

    PyObject *dict = PyModule_Dict(obj);
    if (dict) {
        int version;
        PyObject *value;

        if (PyStrictModule_Check(obj)) {
            version = PYCACHE_STRICT_MODULE_VERSION(obj);
            if (strictmodule_is_unassigned(dict, name) == 0) {
                value = _PyDict_GetAttrItem(dict, name);
            } else {
                value = NULL;
            }
        } else
        {
            version = PYCACHE_MODULE_VERSION(obj);
            value = _PyDict_GetAttrItem(dict, name);
        }

        if (value != NULL) {

            _PyShadow_ModuleAttrEntry *entry;
            if (PyStrictModule_Check(obj)) {
                entry = (_PyShadow_ModuleAttrEntry *)_PyShadow_NewCacheEntry(
                    &_PyShadow_StrictModuleAttrEntryType);
            } else
            {
                entry = (_PyShadow_ModuleAttrEntry *)_PyShadow_NewCacheEntry(
                    &_PyShadow_ModuleAttrEntryType);
            }
            if (entry == NULL) {
                return 0;
            }

            entry->value = value;
            entry->name = PyUnicode_FromObject(name);
            if (entry->name == NULL) {
                Py_DECREF(entry);
                return 0;
            }
            PyObject_Hash(entry->name);
            entry->module = obj;
            entry->version = version;

            if (_PyShadow_AddCacheForAttr(cache, name, (PyObject *)entry)) {
                Py_DECREF(entry);
                return 0;
            }

            _PyShadow_PatchOrMiss(state,
                                  next_instr,
                                  PyStrictModule_Check(obj) ? LOAD_METHOD_S_MODULE : LOAD_METHOD_MODULE,
                                  (PyObject *)entry,
                                  name,
                                  &_PyShadow_LoadMethodMiss);
            Py_DECREF(entry);
            /* we just return the value directly here in case the lookup
             * modified the module */
            *method = value;
            Py_INCREF(
                value); /* value was borred, and we need a ref to return */
            return 0;
        }
    }
    return 0;
}

int
_PyShadow_LoadMethodWithCache(_PyShadow_EvalState *state,
                              const _Py_CODEUNIT *next_instr,
                              PyObject *obj,
                              PyObject *name,
                              PyObject **method)
{
    PyTypeObject *tp = Py_TYPE(obj);
    PyCodeCacheRef *cache;
    PyObject *entry;
    int meth_found;

    assert(*method == NULL);
    if (tp->tp_getattro != PyObject_GenericGetAttr) {
        if (tp->tp_getattro == PyType_Type.tp_getattro) {
            /* calling a method on a type */
            meth_found = _PyShadow_LoadMethodFromType(
                state, next_instr, obj, name, method);
            if (*method != NULL) {
                return meth_found;
            }
        } else if (tp->tp_getattro == PyModule_Type.tp_getattro
                   || tp->tp_getattro == PyStrictModule_Type.tp_getattro
        ) {
            if (_PyType_Lookup(tp, name) == NULL) {
                int meth_found = _PyShadow_LoadMethodFromModule(
                    state, next_instr, obj, name, method);
                if (*method != NULL) {
                    return meth_found;
                }
                goto load_method_miss;
            }
        }
        /* unsupported instance type with a custom get attribute */
        goto load_method_miss;
    }

    cache = _PyShadow_GetCache((PyObject *)tp);
    if (cache == NULL || cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        goto load_method_miss;
    }

    if (_PyShadow_LoadMethodTryCacheHit(
            state, next_instr, obj, name, cache, method, &meth_found)) {
        return meth_found;
    }

    entry = _PyShadow_LoadCacheInfo(tp, name, cache);
    if (entry == NULL) {
        goto load_method_miss;
    }

    meth_found = _PyShadow_LoadMethodRunCacheEntry(
        state, next_instr, entry, obj, name, method);
    Py_DECREF(entry);
    return meth_found;
load_method_miss:
    _PyShadow_LoadMethodMiss(state, next_instr, name);
    return _PyObject_GetMethod(obj, name, method);
}

int
_PyShadow_StoreAttrRunCacheEntry(_PyShadow_EvalState *state,
                                 const _Py_CODEUNIT *next_instr,
                                 PyObject *entry,
                                 PyObject *owner,
                                 PyObject *name,
                                 PyObject *value)
{
    _PyCacheType *cache_type = (_PyCacheType *)((PyObject *)entry)->ob_type;
    int opcode = cache_type->store_attr_opcode;

    _PyShadow_PatchOrMiss(
        state, next_instr, opcode, entry, name, &_PyShadow_StoreAttrMiss);

    assert(Py_TYPE(owner) == ((_PyShadow_InstanceAttrEntry *)entry)->type);
    return cache_type->store_attr(
        state, next_instr, (_PyShadow_InstanceAttrEntry *)entry, owner, value);
}

static int
_PyShadow_StoreAttrTryCacheHit(_PyShadow_EvalState *state,
                               const _Py_CODEUNIT *next_instr,
                               PyObject *owner,
                               PyObject *name,
                               PyCodeCacheRef *cache,
                               PyObject *value,
                               int *done)
{
    if (cache == NULL || cache->invalidate_count == CACHE_UPDATE_DISABLED) {
        _PyShadow_StoreAttrMiss(state, next_instr, name);
        *done = 1;
        return PyObject_SetAttr(owner, name, value);
    }

    PyObject *entry = _PyShadow_GetCacheForAttr(cache, name);
    if (entry != NULL && _PyShadow_IsCacheValid(entry)) {
        /* We have an existing valid cache, re-use it */
        *done = 1;
        return _PyShadow_StoreAttrRunCacheEntry(
            state, next_instr, entry, owner, name, value);
    }

    return 0;
}

int
_PyShadow_StoreAttrWithCache(_PyShadow_EvalState *state,
                           const _Py_CODEUNIT *next_instr,
                           PyObject *owner,
                           PyObject *name,
                           PyObject *value)
{
    PyTypeObject *tp = Py_TYPE(owner);
    int res, done = 0;
    PyObject *entry;

    if (tp->tp_setattro != PyObject_GenericSetAttr) {
        /* custom set attr, we can't cache... */
        goto store_attr_miss;
    }

    PyCodeCacheRef *cache = _PyShadow_GetCache((PyObject *)tp);
    if (cache == NULL) {
        return PyObject_SetAttr(owner, name, value);
    }

    res = _PyShadow_StoreAttrTryCacheHit(
        state, next_instr, owner, name, cache, value, &done);
    if (done) {
        return res;
    }

    entry = _PyShadow_LoadCacheInfo(tp, name, cache);
    if (entry == NULL) {
        goto store_attr_miss;
    }

    res = _PyShadow_StoreAttrRunCacheEntry(
        state, next_instr, entry, owner, name, value);
    Py_DECREF(entry);
    return res;
store_attr_miss:
    _PyShadow_StoreAttrMiss(state, next_instr, name);
    return PyObject_SetAttr(owner, name, value);
}

int
_PyShadow_StoreAttrInvalidate(_PyShadow_EvalState *shadow,
                            const _Py_CODEUNIT *next_instr,
                            PyObject *owner,
                            PyObject *name,
                            PyObject *value,
                            PyObject *type)
{
    if (_PyShadow_CacheHitInvalidate(
            shadow, next_instr, type, "invalidate_setattr")) {
        _PyShadow_StoreAttrMiss(shadow, next_instr, name);
        return PyObject_SetAttr(owner, name, value);
    }

    return _PyShadow_StoreAttrWithCache(shadow, next_instr, owner, name, value);
}

PyObject *_Py_NO_INLINE
_PyShadow_UpdateFastCache(_PyShadow_InstanceAttrEntry *entry,
                          PyDictObject *dictobj)
{
    PyObject *res;
    if (_PyDict_HasSplitTable(dictobj)) {
        entry->splitoffset =
            _PyDictKeys_GetSplitIndex(dictobj->ma_keys, entry->name);
        entry->nentries = dictobj->ma_keys->dk_nentries;
        if (entry->splitoffset != -1) {
            res = dictobj->ma_values[entry->splitoffset];
            Py_XINCREF(res);
        } else {
            res = NULL;
        }
    } else {
        res = _PyDict_GetItem_UnicodeExact((PyObject *)dictobj, entry->name);
        Py_XINCREF(res);
    }
    return res;
}

PyObject *
_PyShadow_BinarySubscrWithCache(_PyShadow_EvalState *shadow,
                                const _Py_CODEUNIT *next_instr,
                                PyObject *container,
                                PyObject *sub,
                                int oparg)
{
    int shadow_op = -1;
    PyObject *res;
    if (PyDict_CheckExact(container)) {
        if (PyUnicode_CheckExact(sub)) {
            shadow_op = BINARY_SUBSCR_DICT_STR;
            res = _PyDict_GetItem_Unicode(container, sub);
            if (res == NULL) {
                _PyErr_SetKeyError(sub);
            } else {
                Py_INCREF(res);
            }
        } else {
            shadow_op = BINARY_SUBSCR_DICT;
            res = Ci_dict_subscript(container, sub);
        }
    } else if (PyList_CheckExact(container)) {
        shadow_op = BINARY_SUBSCR_LIST;
        res = Ci_list_subscript(container, sub);
    } else if (PyTuple_CheckExact(container)) {
        _Py_CODEUNIT prev_word = next_instr[-2];
        int prev_opcode = _Py_OPCODE(prev_word);
        if (prev_opcode == LOAD_CONST && PyLong_CheckExact(sub)) {
            Py_ssize_t i = PyLong_AsSsize_t(sub);
            if (i == -1 && PyErr_Occurred()) {
                PyErr_Clear();
            } else {
                res = Ci_tuple_subscript(container, sub);
                // patch the load const
                shadow_op = BINARY_SUBSCR_TUPLE_CONST_INT;
                if (_PyShadow_PatchByteCode(
                        shadow, &(next_instr[-1]), shadow_op, (int)i)) {
                    _PyShadow_PatchByteCode(
                        shadow, next_instr, BINARY_SUBSCR_TUPLE, oparg);
                }
                return res;
            }
        }
        res = Ci_tuple_subscript(container, sub);
        shadow_op = BINARY_SUBSCR_TUPLE;
    } else {
        res = PyObject_GetItem(container, sub);
    }
    if (shadow_op >= 0) {
        _PyShadow_PatchByteCode(shadow, next_instr, shadow_op, oparg);
    }
    return res;
}

#ifdef INLINE_CACHE_PROFILE

/* Indexed by opcode */
OpcodeCacheStats opcode_cache_stats[256];

OpcodeCacheUncachable opcode_uncachable_stats;

static int
_PyShadow_AddOpcodeCacheStat(PyObject *container,
                             const char *key,
                             Py_ssize_t stat)
{
    PyObject *v = PyLong_FromSsize_t(stat);
    if (v == NULL) {
        return -1;
    }

    int st = PyDict_SetItemString(container, key, v);
    /* Ownership transferred to container on success, freed on failure. */
    Py_DECREF(v);

    return st;
}

static int
_PyShadow_AddOpcodeCacheStatsDict(PyObject *container,
                                  const char *key,
                                  OpcodeCacheStats *stats)
{
    PyObject *dct = PyDict_New();
    if (dct == NULL) {
        return -1;
    }

#define ADD_STAT(expr, key)                                                   \
    do {                                                                      \
        if (_PyShadow_AddOpcodeCacheStat(dct, (key), (expr)) == -1) {         \
            Py_DECREF(dct);                                                   \
            return -1;                                                        \
        }                                                                     \
    } while (0)

    ADD_STAT(stats->hits, "hits");
    ADD_STAT(stats->slightmisses, "slightmisses");
    ADD_STAT(stats->misses, "misses");
    ADD_STAT(stats->uncacheable, "uncacheable");
    ADD_STAT(stats->entries, "entries");

#undef ADD_STAT
    int st = PyDict_SetItemString(container, key, dct);
    /* Ownership transferred to container on success, freed on failure. */
    Py_DECREF(dct);
    return st;
}

static PyObject *
_PyShadow_MakeUncachableStats(void)
{
    const char *alloc = _PyMem_GetAllocatorsName();
    if (!alloc || (strcmp(alloc, "malloc") && strcmp(alloc, "pymalloc"))) {
        /* avoid excessive allocation in the face of unusual allocators */
        return NULL;
    }
    PyObject *dct = PyDict_New();

#define ADD_STAT(expr, key)                                                   \
    do {                                                                      \
        if (_PyShadow_AddOpcodeCacheStat(dct, (key), (expr)) == -1) {         \
            Py_DECREF(dct);                                                   \
            return NULL;                                                      \
        }                                                                     \
    } while (0)

    if (dct != NULL) {
        ADD_STAT(opcode_uncachable_stats.dict_descr_mix, "dict_descr_mix");
        ADD_STAT(opcode_uncachable_stats.getattr_super, "getattr_super");
        ADD_STAT(opcode_uncachable_stats.getattr_type, "getattr_type");
        ADD_STAT(opcode_uncachable_stats.getattr_unknown, "getattr_unknown");
    }

#undef ADD_STAT
    return dct;
}

void
_PyShadow_Stat(const char *cat, const char *name)
{
    const char *alloc = _PyMem_GetAllocatorsName();
    if (!alloc || (strcmp(alloc, "malloc") && strcmp(alloc, "pymalloc"))) {
        /* avoid excessive allocation in the face of unusual allocators */
        return;
    }

    if (type_stats == NULL) {
        type_stats = PyDict_New();
    }
    if (type_stats != NULL) {
        PyObject *dict = PyDict_GetItemString(type_stats, cat);
        if (dict == NULL) {
            dict = PyDict_New();
            if (dict == NULL) {
                return;
            }
            if (PyDict_SetItemString(type_stats, cat, dict) == -1) {
                Py_DECREF(dict);
                return;
            }
            Py_DECREF(dict); /* we now have a borrwed ref */
        }
        PyObject *value = PyDict_GetItemString(dict, name);
        if (value == NULL) {
            value = PyLong_FromLong(1);
        } else {
            value = PyLong_FromLong(PyLong_AsLong(value) + 1);
        }
        if (value != NULL) {
            PyDict_SetItemString(dict, name, value);
            Py_DECREF(value);
        }
    }
}

void
_PyShadow_TypeStat(PyTypeObject *tp, const char *stat)
{
    if (tp->tp_name != NULL) {
        _PyShadow_Stat(stat, tp->tp_name);
    }
}

PyObject *
_PyShadow_GetInlineCacheStats(PyObject *self)
{
    PyObject *opcode_stats = PyDict_New();
    if (opcode_stats == NULL) {
        return NULL;
    }

    int st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats, "LOAD_GLOBAL", &opcode_cache_stats[LOAD_GLOBAL]);
    if (st == -1) {
        goto err;
    }
    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats, "LOAD_ATTR", &opcode_cache_stats[LOAD_ATTR]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_NO_DICT_DESCR",
        &opcode_cache_stats[LOAD_ATTR_NO_DICT_DESCR]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_UNCACHABLE",
        &opcode_cache_stats[LOAD_ATTR_UNCACHABLE]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_DICT_DESCR",
        &opcode_cache_stats[LOAD_ATTR_DICT_DESCR]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_DICT_NO_DESCR",
        &opcode_cache_stats[LOAD_ATTR_DICT_NO_DESCR]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_SPLIT_DICT",
        &opcode_cache_stats[LOAD_ATTR_SPLIT_DICT]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_SPLIT_DICT_DESCR",
        &opcode_cache_stats[LOAD_ATTR_SPLIT_DICT_DESCR]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats, "LOAD_ATTR_TYPE", &opcode_cache_stats[LOAD_ATTR_TYPE]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(
        opcode_stats,
        "LOAD_ATTR_MODULE",
        &opcode_cache_stats[LOAD_ATTR_MODULE]);
    if (st == -1) {
        goto err;
    }

    st = _PyShadow_AddOpcodeCacheStatsDict(opcode_stats, "LOAD_ATTR_S_MODULE",
                                             &opcode_cache_stats[LOAD_ATTR_S_MODULE]);
    if (st == -1) {
        goto err;
    }

    PyObject *uncachable = _PyShadow_MakeUncachableStats();
    if (uncachable == NULL) {
        goto err;
    }
    if (type_stats == NULL) {
        type_stats = PyDict_New();
        if (type_stats == NULL) {
            goto err;
        }
    }

    PyObject *ret = Py_BuildValue("nnNOO",
                                  inline_cache_count,
                                  inline_cache_total_size,
                                  opcode_stats,
                                  uncachable,
                                  type_stats);
    if (ret == NULL) {
        goto err;
    }
    Py_DECREF(type_stats);
    type_stats = NULL;
    memset(&opcode_cache_stats, 0, sizeof(opcode_cache_stats));
    memset(&opcode_uncachable_stats, 0, sizeof(OpcodeCacheUncachable));
    /* Ownership transferred to caller */
    return ret;

err:
    Py_DECREF(opcode_stats);
    return NULL;
}

#else

PyObject *
_PyShadow_GetInlineCacheStats(PyObject *self)
{
    Py_INCREF(Py_None);
    return Py_None;
}

#endif

int
_PyShadow_InitCache(PyCodeObject *co)
{
    char *buffer = PyBytes_AS_STRING(co->co_code);

    /* names is a bitmask of seen names, if we have a limited number */
    size_t names = 0, funcs = 0;
    Py_ssize_t glob_count = 0, func_count = 0;

    PyObject *set = NULL, *func_set = NULL;
    if (PyTuple_Size(co->co_names) > (Py_ssize_t)sizeof(names) * 8) {
        /* we have lots of names, let's use a set to count them... */
        set = PySet_New(NULL);
        if (set == NULL) {
            return -1;
        }
    }

    if (co->co_flags & CO_STATICALLY_COMPILED &&
        PyTuple_Size(co->co_consts) > (Py_ssize_t)sizeof(names) * 8) {
        /* we have lots of consts, let's use a set to count them... */
        func_set = PySet_New(NULL);
        if (func_set == NULL) {
            return -1;
        }
    }

    /* Scan the byte code for all LOAD_GLOBALs and pre-allocate enough space
     * for all of them */
    _Py_CODEUNIT *instr = (_Py_CODEUNIT *)buffer;
    _Py_CODEUNIT *end = (_Py_CODEUNIT *)(buffer + Py_SIZE(co->co_code));
    while (instr < end) {
        unsigned char opcode = _Py_OPCODE(*instr);
        int oparg = _Py_OPARG(*instr);

        while (opcode == EXTENDED_ARG) {
            instr++;
            oparg = _Py_OPARG(*instr) | (oparg << 8);
            opcode = _Py_OPCODE(*instr);
        }

        if (opcode == LOAD_GLOBAL) {
            if (set == NULL) {
                size_t index = (size_t)1 << oparg;
                if (!(names & index)) {
                    names |= index;
                    glob_count++;
                }
            } else if (PySet_Add(set, PyTuple_GET_ITEM(co->co_names, oparg))) {
                Py_DECREF(set);
                Py_XDECREF(func_set);
                return -1;
            }
        } else if (opcode == INVOKE_FUNCTION) {
            if (func_set == NULL) {
                size_t index = (size_t)1 << oparg;
                if (!(funcs & index)) {
                    funcs |= index;
                    func_count++;
                }
            } else if (PySet_Add(func_set, PyTuple_GET_ITEM(co->co_consts, oparg))) {
                Py_DECREF(func_set);
                Py_XDECREF(set);
                return -1;
            }
        }

        instr++;
    }

    if (set != NULL) {
        glob_count = PySet_Size(set);
        Py_DECREF(set);
    }

    if (func_set != NULL) {
        func_count = PySet_Size(func_set);
        Py_DECREF(func_set);
    }

    _PyShadowCode *shadow;
    shadow = PyMem_Malloc(sizeof(_PyShadowCode) + Py_SIZE(co->co_code));
    if (shadow == NULL) {
        return -1;
    }

    shadow->update_count = 0;
    shadow->len = Py_SIZE(co->co_code);
    memcpy(shadow->code, buffer, Py_SIZE(co->co_code));

    if (glob_count) {
        shadow->globals = PyMem_Calloc(glob_count, sizeof(PyObject **));
        if (shadow->globals == NULL) {
            PyMem_Free(shadow);
            return -1;
        }
    } else {
        shadow->globals = NULL;
    }
    shadow->globals_size = glob_count;

    if (func_count) {
        shadow->functions = PyMem_Calloc(func_count, sizeof(PyObject **));
        if (shadow->functions == NULL) {
            PyMem_Free(shadow->globals);
            PyMem_Free(shadow);
            return -1;
        }
    } else {
        shadow->functions = NULL;
    }
    shadow->functions_size = func_count;

    shadow->polymorphic_caches = NULL;
    shadow->polymorphic_caches_size = 0;

    shadow->field_caches = NULL;
    shadow->field_cache_size = 0;

    cache_init(&shadow->l1_cache);
    cache_init(&shadow->cast_cache);

    co->co_mutable->shadow = shadow;
    return 0;
}

void
_PyShadowCode_Free(_PyShadowCode *shadow)
{
    if (shadow->globals_size) {
        PyMem_Free(shadow->globals);
    }

    if (shadow->functions_size) {
        PyMem_Free(shadow->functions);
    }

    if (shadow->polymorphic_caches_size) {
        for (Py_ssize_t i = 0; i < shadow->polymorphic_caches_size; i++) {
            if (shadow->polymorphic_caches[i] == NULL) {
                break;
            }
            for (int j = 0; j < POLYMORPHIC_CACHE_SIZE; j++) {
                Py_XDECREF(shadow->polymorphic_caches[i][j]);
            }
            PyMem_Free(shadow->polymorphic_caches[i]);
            shadow->polymorphic_caches[i] = NULL;
        }
        PyMem_Free(shadow->polymorphic_caches);
    }

    _Py_CODEUNIT *next_instr = &shadow->code[0];
    /* Caches are ref counted by count of occuranced in the byte code, free
     * all of the active caches now by walking the byte code */
    while (next_instr <
           (&shadow->code[0] + shadow->len / sizeof(_Py_CODEUNIT))) {
        unsigned char opcode = _Py_OPCODE(*next_instr);
        int oparg = _Py_OPARG(*next_instr);
        while (opcode == EXTENDED_ARG) {
            next_instr++;
            oparg = _Py_OPARG(*next_instr) | (oparg << 8);
            opcode = _Py_OPCODE(*next_instr);
        }

        if (_PyShadow_IsCacheOpcode(opcode)) {
            assert(oparg >= 0 && oparg < shadow->l1_cache.size);
            assert(shadow->l1_cache.items[oparg] != NULL);
            Py_DECREF(shadow->l1_cache.items[oparg]);
        }
        next_instr++;
    }

    shadow->globals = NULL;
    shadow->globals_size = 0;
    if (shadow->l1_cache.items != NULL) {
        PyMem_Free(shadow->l1_cache.items);
    }
    if (shadow->cast_cache.items != NULL) {
        _ShadowCache *cache = &shadow->cast_cache;
        for (Py_ssize_t i = 0; i < cache->size; i++) {
            if (cache->items[i] == NULL) {
                break;
            }
            Py_DECREF(cache->items[i]);
        }
        PyMem_Free(shadow->cast_cache.items);
    }
    if (shadow->field_caches != NULL) {
        PyMem_Free(shadow->field_caches);
    }
    PyMem_Free(shadow);
}

void
_PyShadow_ClearCache(PyObject *obj)
{
    if (PyCode_Check(obj)) {
        /* clear the shadow byte and l1 caches */
        PyCodeObject *co = (PyCodeObject *)obj;
        assert(co->co_mutable->curcalls == 0);
        if (co->co_mutable->shadow == NULL) {
            return;
        }
        _PyShadowCode_Free(co->co_mutable->shadow);
        co->co_mutable->shadow = NULL;
    } else if (PyType_Check(obj) || PyModule_CheckExact(obj)) {
        /* clear the l2 caches */
        PyCodeCacheRef *cache = _PyShadow_FindCache(obj);
        if (cache != NULL) {
            _PyWeakref_ClearRef((PyWeakReference *)cache);
            assert(_PyShadow_FindCache(obj) == NULL);
            Py_XDECREF(weakref_callback_impl(NULL, cache));
        }
    } else if (PyFunction_Check(obj)) {
        /* clear the caches for the associated function */
        PyCodeObject *code = (PyCodeObject *)PyFunction_GetCode(obj);
        if (code != NULL && code->co_mutable->shadow != NULL &&
            code->co_mutable->curcalls == 0) {
            _PyShadow_ClearCache((PyObject *)code);
        }
    }
}
