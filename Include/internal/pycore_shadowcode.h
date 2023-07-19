/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
/* facebook begin t39538061 */
#include "Objects/dict-common.h"
#include "Python.h"
#include "code.h"
#include "opcode.h"
#include "weakrefobject.h"
#include "pycore_object.h"
#include "pystate.h"
#include "Objects/dict-common.h"
#include "pystate.h"
#include "pycore_moduleobject.h"
#include "pycore_object.h"

#include "cinder/ci_api.h"

#include <stdint.h>

#ifndef Py_SHADOWCODE_H
#define Py_SHADOWCODE_H

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PyWeakReference weakref; /* base weak ref */
    Py_ssize_t invalidate_count;
    /* If this object is a subclass of type we track it's meta-type. */
    PyObject *metatype;
    /* If this object is a type object, we have a dictionary of:
     * Dict[codecache, Dict[name, cache_entry]]
     * Where codecache is the type's codecache, and Dict[name, cache_entry]
     * tracks the caches we've created for the type
     */
    PyObject *type_insts;
    /* New-style caches which hold all of the information about a resolved
     * attribute */
    PyObject *l2_cache;
} PyCodeCacheRef;

typedef int (*pyshadowcache_invalidate)(PyObject *entry);

CiAPI_DATA(PyTypeObject) _PyCodeCache_RefType;

#define PyCodeCacheRef_CheckRefExact(op) (Py_TYPE(op) == &_PyCodeCache_RefType)

PyCodeCacheRef *_PyShadow_NewCache(PyObject *from);

inline PyCodeCacheRef *
_PyShadow_FindCache(PyObject *from)
{
    assert(PyType_SUPPORTS_WEAKREFS(Py_TYPE(from)));

    PyWeakReference **weak_refs =
        (PyWeakReference **)PyObject_GET_WEAKREFS_LISTPTR(from);
    if (weak_refs != NULL) {
        PyWeakReference *head = *weak_refs;
        while (head != NULL) {
            if (PyCodeCacheRef_CheckRefExact(head)) {
                return (PyCodeCacheRef *)head;
            }
            head = head->wr_next;
        }
    }
    return NULL;
}

#define INITIAL_POLYMORPHIC_CACHE_ARRAY_SIZE 4
#define POLYMORPHIC_CACHE_SIZE 4

/* Gets a code cache object from the given weak-referencable object.  This
supports getting caches from types and modules (at least).

Returns a borrowed reference.
 */
inline PyCodeCacheRef *
_PyShadow_GetCache(PyObject *from)
{
    PyCodeCacheRef *res = _PyShadow_FindCache(from);
    if (res != NULL) {
        return res;
    }
    return _PyShadow_NewCache(from);
}

typedef struct {
    PyObject head;
} _PyShadow_CacheEntry;

/* Cache for accessing items from a module */
typedef struct {
    PyObject head;
    uint64_t version;
    PyObject *module; /* borrowed */
    PyObject *value;  /* borrowed */
    PyObject *name;
} _PyShadow_ModuleAttrEntry;

/* Cache for accessing items from an instance of a class */
typedef struct {
    _PyShadow_CacheEntry head;
    PyObject *name;     /* name of the attribute we cache for */
    PyTypeObject *type; /* target type we're caching against, borrowed */
    PyObject *value;    /* descriptor if one is present, borrowed */

    size_t dictoffset;
    Py_ssize_t splitoffset;
    Py_ssize_t nentries;

    /*
     * If `value` points to either a classmethod, staticmethod, or
     * wrapper_descriptor then this contains information to optimize
     * method calls against a type receiver.
     *
     * The upper bits contain the callable object in `value`.
     *
     * The low bit indicates if the interpreter should treat calling
     * the callable as an unbound method (1) or not (0) when performing
     * CALL_METHOD.
     *
     * This contains 0 if it is unset.
     */
     uintptr_t load_method_type_data;
} _PyShadow_InstanceAttrEntry;

typedef enum {
    PYSHADOW_CALL_NOT_UNBOUND = 0,
    PYSHADOW_CALL_UNBOUND = 1,
} _PyShadow_MethCallKind;

static const unsigned int _PyShadow_MethCallKindBits = 1;
static const uintptr_t _PyShadow_MethCallKindMask =
    (1 << _PyShadow_MethCallKindBits) - 1;
static const uintptr_t _PyShadow_MethodLikeMask =
    ~_PyShadow_MethCallKindMask;
static const uintptr_t _PyShadow_LoadMethodTypeDataUnset = 0;

static inline PyObject *
_PyShadow_GetMethodLike(uintptr_t data) {
    return (PyObject *) (data & _PyShadow_MethodLikeMask);
}

static inline _PyShadow_MethCallKind
_PyShadow_GetMethCallKind(uintptr_t data) {
    return (_PyShadow_MethCallKind) (data & _PyShadow_MethCallKindMask);
}

static inline uintptr_t
_PyShadow_MakeLoadMethodTypeData(PyObject *obj,
                                 _PyShadow_MethCallKind kind)
{
    return (uintptr_t) obj | kind;
}

/* Code level cache - mutiple of these exist for different cache targets,
 * allowing > 256 caches per method w/o needing to expand and re-map they
 * byte code. */
typedef struct {
    PyObject **items;
    Py_ssize_t size;
} _ShadowCache;

typedef struct {
    int offset, type;
} _FieldCache;

/* Tracks metadata about our shadow code */
typedef struct _PyShadowCode {
    PyObject ***globals;
    Py_ssize_t globals_size;

    _ShadowCache l1_cache;
    _ShadowCache cast_cache;

    _PyShadow_InstanceAttrEntry ***polymorphic_caches;
    Py_ssize_t polymorphic_caches_size;

    _FieldCache *field_caches;
    Py_ssize_t field_cache_size;

    Py_ssize_t update_count;
    Py_ssize_t len;

    PyObject ***functions;
    Py_ssize_t functions_size;

    _Py_CODEUNIT code[];
} _PyShadowCode;

typedef struct {
    PyCodeObject *code;
    _PyShadowCode *shadow;
    const _Py_CODEUNIT **first_instr;
} _PyShadow_EvalState;

typedef void (*invalidate_func)(PyObject *obj);
typedef int (*is_valid_func)(PyObject *obj);
typedef PyObject *(*pyshadowcache_loadattr_func)(
    _PyShadow_EvalState *shadow,
    const _Py_CODEUNIT *next_instr,
    PyObject *entry,
    PyObject *owner);
typedef int (*pyshadowcache_loadmethod_func)(
    _PyShadow_EvalState *shadow,
    const _Py_CODEUNIT *next_instr,
    _PyShadow_InstanceAttrEntry *entry,
    PyObject *obj,
    PyObject **meth);
typedef int (*storeattr_func)(_PyShadow_EvalState *shadow,
                              const _Py_CODEUNIT *next_instr,
                              _PyShadow_InstanceAttrEntry *entry,
                              PyObject *obj,
                              PyObject *value);

/* Custom type object for cache-types. We have additional virtual methods which
 * we customize based upon the cache type */
typedef struct _PyCacheType {
    PyTypeObject type;
    pyshadowcache_loadattr_func load_func;
    pyshadowcache_loadmethod_func load_method;
    storeattr_func store_attr;
    invalidate_func invalidate;
    int load_attr_opcode, load_method_opcode, store_attr_opcode;
    is_valid_func is_valid;
} _PyCacheType;

CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheDictNoDescr;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheDictDescr;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheSlot;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheNoDictDescr;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheSplitDictDescr;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheSplitDict;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheDictMethod;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheNoDictMethod;
CiAPI_DATA(_PyCacheType) _PyShadow_InstanceCacheSplitDictMethod;

CiAPI_DATA(_PyCacheType) _PyShadow_ModuleAttrEntryType;
CiAPI_DATA(_PyCacheType) _PyShadow_StrictModuleAttrEntryType;

void _PyShadow_InitGlobal(_PyShadow_EvalState *shadow,
                          const _Py_CODEUNIT *next_instr,
                          PyObject *globals,
                          PyObject *builtins,
                          PyObject *name);

PyObject *_PyShadow_GetInlineCacheStats(PyObject *self);

CiAPI_FUNC(void) _PyShadow_ClearCache(PyObject *co);

int _PyShadow_PatchByteCode(_PyShadow_EvalState *shadow,
                            const _Py_CODEUNIT *next_instr,
                            int op,
                            int arg);

CiAPI_FUNC(int) _PyShadow_InitCache(PyCodeObject *co);

static inline PyObject **
_PyShadow_GetGlobal(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->globals != NULL);
    assert(offset > -1 && offset < state->shadow->globals_size);
    return state->shadow->globals[offset];
}

static inline PyObject **
_PyShadow_GetFunction(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->functions != NULL);
    assert(offset > -1 && offset < state->shadow->functions_size);
    return state->shadow->functions[offset];
}

int _PyShadow_CacheFunction(_PyShadow_EvalState *state, PyObject **func);

static inline _PyShadow_InstanceAttrEntry **
_PyShadow_GetPolymorphicAttr(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->polymorphic_caches != NULL);
    assert(offset > -1 && offset < state->shadow->polymorphic_caches_size);
    return state->shadow->polymorphic_caches[offset];
}

static inline _PyShadow_InstanceAttrEntry *
_PyShadow_GetInstanceAttr(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->l1_cache.items != NULL);
    assert(offset > -1 && offset < state->shadow->l1_cache.size);
    return (
        (_PyShadow_InstanceAttrEntry **)state->shadow->l1_cache.items)[offset];
}

static inline _PyShadow_ModuleAttrEntry *
_PyShadow_GetModuleAttr(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->l1_cache.items != NULL);
    assert(offset > -1 && offset < state->shadow->l1_cache.size);
    return (
        (_PyShadow_ModuleAttrEntry **)state->shadow->l1_cache.items)[offset];
}

static inline _PyShadow_ModuleAttrEntry *
_PyShadow_GetStrictModuleAttr(_PyShadow_EvalState *state, int offset)
{
  assert(state->shadow->l1_cache.items != NULL);
  assert(offset > -1 && offset < state->shadow->l1_cache.size);
  return ((_PyShadow_ModuleAttrEntry **)state->shadow->l1_cache.items)[offset];
}

static inline PyObject *
_PyShadow_GetCastType(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->cast_cache.items != NULL);
    assert(offset > -1 && offset < state->shadow->cast_cache.size);
    return state->shadow->cast_cache.items[offset];
}

int _PyShadow_CacheCastType(_PyShadow_EvalState *state, PyObject *type);

static inline _FieldCache *
_PyShadow_GetFieldCache(_PyShadow_EvalState *state, int offset)
{
    assert(state->shadow->field_caches != NULL);
    assert(offset > -1 && offset < state->shadow->field_cache_size);
    return &state->shadow->field_caches[offset];
}

int _PyShadow_CacheFieldType(_PyShadow_EvalState *state, int offset, int type);

PyObject *_PyShadow_LoadAttrPolymorphic(_PyShadow_EvalState *shadow,
                                        const _Py_CODEUNIT *next_instr,
                                        _PyShadow_InstanceAttrEntry **entries,
                                        PyObject *owner);

PyObject *_PyShadow_UpdateFastCache(_PyShadow_InstanceAttrEntry *entry,
                                    PyDictObject *dictobj);

#define PYCACHE_MODULE_VERSION(module) ((PyDictObject *)((PyModuleObject *)module)->md_dict)->ma_version_tag
#define PYCACHE_STRICT_MODULE_VERSION(module) ((PyDictObject *)((PyStrictModuleObject *)module)->globals)->ma_version_tag


int _PyShadow_LoadAttrMiss(_PyShadow_EvalState *shadow,
                           const _Py_CODEUNIT *next_instr,
                           PyObject *name);
int _PyShadow_LoadMethodMiss(_PyShadow_EvalState *shadow,
                             const _Py_CODEUNIT *next_instr,
                             PyObject *name);
void _PyShadow_SetLoadAttrError(PyObject *obj, PyObject *name);
void _PyShadow_TypeModified(PyTypeObject *type);

PyObject *_PyShadow_LoadAttrInvalidate(_PyShadow_EvalState *shadow,
                                       const _Py_CODEUNIT *next_instr,
                                       PyObject *owner,
                                       PyObject *name,
                                       PyTypeObject *type);

int _PyShadow_LoadMethodInvalidate(_PyShadow_EvalState *shadow,
                                   const _Py_CODEUNIT *next_instr,
                                   PyObject *owner,
                                   PyObject *name,
                                   PyObject *type,
                                   PyObject **meth);

PyObject *_PyShadow_LoadAttrWithCache(_PyShadow_EvalState *shadow,
                                     const _Py_CODEUNIT *next_instr,
                                     PyObject *owner,
                                     PyObject *name);

int _PyShadow_StoreAttrWithCache(_PyShadow_EvalState *shadow,
                               const _Py_CODEUNIT *next_instr,
                               PyObject *owner,
                               PyObject *name,
                               PyObject *value);

int _PyShadow_StoreAttrInvalidate(_PyShadow_EvalState *shadow,
                                const _Py_CODEUNIT *next_instr,
                                PyObject *owner,
                                PyObject *name,
                                PyObject *value,
                                PyObject *type);

int _PyShadow_LoadMethodWithCache(_PyShadow_EvalState *shadow,
                                  const _Py_CODEUNIT *next_instr,
                                  PyObject *owner,
                                  PyObject *name,
                                  PyObject **meth);

PyObject *_PyShadow_BinarySubscrWithCache(_PyShadow_EvalState *shadow,
                                          const _Py_CODEUNIT *next_instr,
                                          PyObject *container,
                                          PyObject *sub,
                                          int oparg);

Py_ssize_t _Py_NO_INLINE _PyShadow_FixDictOffset(PyObject *obj,
                                                 Py_ssize_t dictoffset);

static inline Py_ssize_t
_PyShadow_NormalizeDictOffset(PyObject *obj, Py_ssize_t dictoffset)
{
    if (_Py_LIKELY(dictoffset >= 0)) {
        return dictoffset;
    }
    return _PyShadow_FixDictOffset(obj, dictoffset);
}

PyObject *_PyShadow_GetOriginalName(_PyShadow_EvalState *state,
                                    const _Py_CODEUNIT *next_instr);

PyObject *_PyShadow_GetOriginalConst(_PyShadow_EvalState *state,
                                     const _Py_CODEUNIT *next_instr);

/* Statistics about caches for a particular opcode */
typedef struct {
    Py_ssize_t hits;   /* cache successfully used */
    Py_ssize_t misses; /* cache miss and needs to be updated */
    Py_ssize_t
        slightmisses; /* cache is mostly correct, but needed minor updates */
    Py_ssize_t uncacheable; /* we were unable to cache the type */
    Py_ssize_t entries;     /* total number of cache entries */
} OpcodeCacheStats;

typedef struct {
    Py_ssize_t dict_descr_mix;
    Py_ssize_t getattr_type;
    Py_ssize_t getattr_super;
    Py_ssize_t getattr_unknown;
} OpcodeCacheUncachable;

#ifdef INLINE_CACHE_PROFILE

extern Py_ssize_t inline_cache_count;

/* Total number of bytes allocated to inline caches */
extern Py_ssize_t inline_cache_total_size;

extern OpcodeCacheStats opcode_cache_stats[256];

void _PyShadow_LogLocation(_PyShadow_EvalState *shadow,
                           const _Py_CODEUNIT *next_instr,
                           const char *category);

#define INLINE_CACHE_CREATED(cache)                                           \
    do {                                                                      \
        inline_cache_count++;                                                 \
        /*inline_cache_total_size += (cache).nentries *                       \
         * sizeof(_Py_CODEUNIT);*/                                            \
    } while (0)

#define INLINE_CACHE_ENTRY_CREATED(opcode, size)                              \
    do {                                                                      \
        inline_cache_total_size += size;                                      \
        opcode_cache_stats[(opcode)].entries++;                               \
    } while (0)

#define INLINE_CACHE_RECORD_STAT(opcode, stat_name)                           \
    (opcode_cache_stats[(opcode)].stat_name++)

void _PyShadow_TypeStat(PyTypeObject *tp, const char *stat);
void _PyShadow_Stat(const char *cat, const char *name);
#define INLINE_CACHE_TYPE_STAT(tp, stat) _PyShadow_TypeStat(tp, stat)
#define INLINE_CACHE_UNCACHABLE_TYPE(tp)                                      \
    INLINE_CACHE_TYPE_STAT(tp, "uncachable")
#define INLINE_CACHE_INCR(cat, name) _PyShadow_Stat(cat, name)

#else

#define INLINE_CACHE_CREATED(cache)
#define INLINE_CACHE_ENTRY_CREATED(opcode, entry)
#define INLINE_CACHE_RECORD_STAT(opcode, stat_name)
#define INLINE_CACHE_UNCACHABLE_TYPE(tp)
#define INLINE_CACHE_TYPE_STAT(tp, stat)
#define INLINE_CACHE_INCR(cat, name)

#endif

/* Attempts to do a cached split dict lookup.  Returns the value in the
 * dictionary or NULL */
static inline PyObject *
_PyShadow_TrySplitDictLookup(_PyShadow_InstanceAttrEntry *entry,
                             PyObject *dict,
                             int opcode)
{
    PyDictObject *dictobj = (PyDictObject *)dict;
    if (_Py_LIKELY(dictobj != NULL)) {
        if (!_PyDict_HasSplitTable(dictobj)) {
          PyObject* res = _PyDict_GetItem_UnicodeExact((PyObject *)dictobj, entry->name);
          Py_XINCREF(res);
          return res;
        } else if (entry->splitoffset != -1) {
            /* Hit - we have a matching split dictionary and the offset
             * is initialized */
            INLINE_CACHE_RECORD_STAT(opcode, hits);
            PyObject *res = dictobj->ma_values[entry->splitoffset];
            Py_XINCREF(res);
            return res;
        } else if (entry->nentries != dictobj->ma_keys->dk_nentries) {
            INLINE_CACHE_RECORD_STAT(opcode, slightmisses);
            return _PyShadow_UpdateFastCache(entry, dictobj);
        }
        /* Else we have a negative hit, the keys and entries haven't
         * actually changed, but we don't have a split dict index for
         * this.  This is quite common when we're looking at things
         * like a method which is not a data descriptor and requires
         * an instance check */
    }
    return NULL;
}

#define LOAD_ATTR_CACHE_MISS(opcode, target)                                  \
    INLINE_CACHE_RECORD_STAT(opcode, misses);                                 \
    res = _PyShadow_LoadAttrInvalidate(                                       \
        shadow, next_instr, owner, entry->name, target);                      \
    if (res == NULL)                                                          \
        return NULL;

static _Py_ALWAYS_INLINE PyObject *
_PyShadow_LoadAttrDictDescrHit(_PyShadow_InstanceAttrEntry *entry,
                               PyObject *owner)
{
    /* Cache hit */
    PyObject *res = NULL;
    Py_ssize_t dictoffset =
        _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);
    /* if GetItem mutates the dictionary and instance we need the
     * original descriptor value */
    PyObject *descr = entry->value;
    Py_INCREF(descr);

    PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
    PyObject *dict = *dictptr;
    INLINE_CACHE_RECORD_STAT(LOAD_ATTR_DICT_DESCR, hits);
    if (dict != NULL) {
        res = _PyDict_GetItem_UnicodeExact(dict, entry->name);
        Py_XINCREF(res); /* got a borrowed ref */
    }
    if (res == NULL || PyDescr_IsData(descr)) {
        descrgetfunc f = Py_TYPE(descr)->tp_descr_get;
        if (f != NULL) {
            PyObject *got = f(descr, owner, (PyObject *)Py_TYPE(owner));
            Py_DECREF(descr);
            Py_XDECREF(res);
            res = got;
        } else if (res == NULL) {
            res = descr;
        } else {
            Py_DECREF(descr);
        }
    } else {
        Py_DECREF(descr);
    }
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrDictDescr(_PyShadow_EvalState *shadow,
                            const _Py_CODEUNIT *next_instr,
                            _PyShadow_InstanceAttrEntry *entry,
                            PyObject *owner)
{
    assert(((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictMethod.type);

    PyObject *res;
    PyTypeObject *tp = Py_TYPE(owner);
    if (entry->type == tp) {
        INLINE_CACHE_TYPE_STAT(tp, "dict_descr");
        return _PyShadow_LoadAttrDictDescrHit(entry, owner);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_DICT_DESCR, entry->type)
    }
    return res;
}

static PyObject *_Py_NO_INLINE
_PyShadow_LoadAttrDictNoDescrMaybeError(_PyShadow_InstanceAttrEntry *entry,
                                        PyObject *owner)
{
#ifdef ENABLE_CINDERX
    PyObject *res;
    if (entry->value != NULL) {
        /* cached_property descriptor, we need to create the value */
        assert(Py_TYPE(entry->value) == &PyCachedProperty_Type);
        PyCachedPropertyDescrObject *cp =
            (PyCachedPropertyDescrObject *)entry->value;
        PyObject *stack[1] = {owner};
        res = _PyObject_FastCall(cp->func, stack, 1);
        if (res == NULL || _PyObjectDict_SetItem(Py_TYPE(owner),
                                                 _PyObject_GetDictPtr(owner),
                                                 cp->name_or_descr,
                                                 res)) {
            return NULL;
        }
    } else {
        _PyShadow_SetLoadAttrError(owner, entry->name);
        return NULL;
    }
    return res;
#else
    _PyShadow_SetLoadAttrError(owner, entry->name);
    return NULL;
#endif
}


static _Py_ALWAYS_INLINE PyObject *
_PyShadow_LoadAttrDictNoDescrHit(_PyShadow_InstanceAttrEntry *entry,
                                 PyObject *owner)
{
    Py_ssize_t dictoffset =
        _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);

    PyObject *res;
    PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
    PyObject *dict = *dictptr;

    if (dict != NULL) {
        res = _PyDict_GetItem_UnicodeExact(dict, entry->name);
    } else {
        res = NULL;
    }

    if (res == NULL) {
        return _PyShadow_LoadAttrDictNoDescrMaybeError(entry, owner);
    }

    Py_INCREF(res);
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrDictNoDescr(_PyShadow_EvalState *shadow,
                              const _Py_CODEUNIT *next_instr,
                              _PyShadow_InstanceAttrEntry *entry,
                              PyObject *owner)
{
    PyObject *res;
    PyTypeObject *tp = Py_TYPE(owner);

    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_InstanceCacheDictNoDescr.type);

    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_RECORD_STAT(LOAD_ATTR_DICT_NO_DESCR, hits);
        INLINE_CACHE_TYPE_STAT(tp, "dict");
        return _PyShadow_LoadAttrDictNoDescrHit(entry, owner);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_DICT_NO_DESCR, entry->type)
    }
    return res;
}

static _Py_ALWAYS_INLINE PyObject *
_PyShadow_LoadAttrSlotHit(_PyShadow_InstanceAttrEntry *entry, PyObject *owner)
{
#ifdef ENABLE_CINDERX
    PyObject *res = *(PyObject **)((char *)owner + entry->splitoffset);
    if (res == NULL) {
        if (entry->value != NULL &&
            Py_TYPE(entry->value) == &PyCachedProperty_Type) {
            /* cached_property descriptor, we need to create the value */
            PyCachedPropertyDescrObject *cp =
                (PyCachedPropertyDescrObject *)entry->value;
            PyObject *stack[1] = {owner};
            res = _PyObject_FastCall(cp->func, stack, 1);
            if (res == NULL) {
                return NULL;
            }
            *(PyObject **)((char *)owner + entry->splitoffset) = res;
        } else {
            PyErr_SetObject(PyExc_AttributeError, entry->name);
            return NULL;
        }
    }
    Py_INCREF(res);
    return res;
#else
    (void)owner;
    PyErr_SetObject(PyExc_AttributeError, entry->name);
    return NULL;
#endif
}

static inline PyObject *
_PyShadow_LoadAttrSlot(_PyShadow_EvalState *shadow,
                       const _Py_CODEUNIT *next_instr,
                       _PyShadow_InstanceAttrEntry *entry,
                       PyObject *owner)
{
    PyObject *res;

    assert(((PyObject *)entry)->ob_type == &_PyShadow_InstanceCacheSlot.type);

    if (entry->type == Py_TYPE(owner)) {
        return _PyShadow_LoadAttrSlotHit(entry, owner);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_SLOT, entry->type)
    }
    return res;
}

static _Py_ALWAYS_INLINE PyObject *
_PyShadow_LoadAttrSplitDictHit(_PyShadow_InstanceAttrEntry *entry,
                               PyObject *owner)
{
    PyObject *res;
    /* Cache hit */
    Py_ssize_t dictoffset =
        _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);
    PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
    PyObject *dict = *dictptr;
    INLINE_CACHE_TYPE_STAT(Py_TYPE(owner), "fastdict");

    res = _PyShadow_TrySplitDictLookup(entry, dict, LOAD_ATTR_SPLIT_DICT);

    if (_Py_UNLIKELY(res == NULL)) {
        _PyShadow_SetLoadAttrError(owner, entry->name);
    }
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrSplitDict(_PyShadow_EvalState *shadow,
                            const _Py_CODEUNIT *next_instr,
                            _PyShadow_InstanceAttrEntry *entry,
                            PyObject *owner)
{
    PyObject *res;

    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_InstanceCacheSplitDict.type);

    if (entry->type == Py_TYPE(owner)) {
        return _PyShadow_LoadAttrSplitDictHit(entry, owner);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_SPLIT_DICT, entry->type)
    }

    return res;
}

static _Py_ALWAYS_INLINE PyObject *
_PyShadow_LoadAttrSplitDictDescrHit(_PyShadow_InstanceAttrEntry *entry,
                                    PyObject *owner)
{
    PyObject *res;
    /* Cache hit */
    Py_ssize_t dictoffset =
        _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);
    PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
    PyObject *dict = *dictptr;
    PyObject *value = entry->value;
    Py_INCREF(value);

    INLINE_CACHE_TYPE_STAT(tp, "split_dict_descr");
    res =
        _PyShadow_TrySplitDictLookup(entry, dict, LOAD_ATTR_SPLIT_DICT_DESCR);

    if (res == NULL || PyDescr_IsData(value)) {
        INLINE_CACHE_RECORD_STAT(LOAD_ATTR_SPLIT_DICT_DESCR, hits);
        descrgetfunc f = Py_TYPE(value)->tp_descr_get;
        if (f != NULL) {
            PyTypeObject *tp = Py_TYPE(owner);
            PyObject *got = f(value, owner, (PyObject *)tp);
            Py_DECREF(value);
            Py_XDECREF(res);
            res = got;
        } else if (res == NULL) {
            res = value;
        } else {
            Py_DECREF(value);
        }
    } else {
        Py_DECREF(value);
    }
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrSplitDictDescr(_PyShadow_EvalState *shadow,
                                 const _Py_CODEUNIT *next_instr,
                                 _PyShadow_InstanceAttrEntry *entry,
                                 PyObject *owner)
{
    PyObject *res;
    PyTypeObject *tp = Py_TYPE(owner);

    assert(((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictMethod.type);

    if (entry->type == tp) {
        return _PyShadow_LoadAttrSplitDictDescrHit(entry, owner);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_SPLIT_DICT_DESCR, entry->type)
    }
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrType(_PyShadow_EvalState *shadow,
                       const _Py_CODEUNIT *next_instr,
                       _PyShadow_InstanceAttrEntry *entry,
                       PyObject *owner)
{
    PyTypeObject *tp = (PyTypeObject *)owner;
    PyObject *res;

    assert(((PyObject *)entry)->ob_type == &_PyShadow_InstanceCacheSlot.type ||
           /* this "NoDescr" case is because of our special handling of
            * cached_property backed by dict */
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictNoDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDict.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type);

    if (tp == entry->type && entry->value != NULL) {
        INLINE_CACHE_TYPE_STAT(tp, "type");
        INLINE_CACHE_RECORD_STAT(LOAD_ATTR_TYPE, hits);
        res = entry->value;
        descrgetfunc local_get = Py_TYPE(res)->tp_descr_get;
        if (local_get != NULL) {
            /* NULL 2nd argument indicates the descriptor was
             * found on the target object itself (or a base)  */
            INLINE_CACHE_RECORD_STAT(LOAD_ATTR_TYPE, slightmisses);
            Py_INCREF(res);
            PyObject *got = local_get(res, (PyObject *)NULL, (PyObject *)tp);
            Py_DECREF(res);
            res = got;
            if (res == NULL) {
                return NULL;
            }
        } else {
            Py_INCREF(res);
        }
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_TYPE, entry->type)
    }
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrModule(_PyShadow_EvalState *shadow,
                         const _Py_CODEUNIT *next_instr,
                         _PyShadow_ModuleAttrEntry *entry,
                         PyObject *owner)
{
    PyObject *res;
    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_ModuleAttrEntryType.type);

    if (entry->module == owner) {
        if (entry->version != PYCACHE_MODULE_VERSION(owner)) {
            entry->value = _PyDict_GetItem_UnicodeExact(
                ((PyModuleObject *)owner)->md_dict, entry->name);
            if (entry->value == NULL) {
                LOAD_ATTR_CACHE_MISS(LOAD_ATTR_MODULE, NULL)
                return res;
            }
            entry->version = PYCACHE_MODULE_VERSION(owner);
        }
        INLINE_CACHE_RECORD_STAT(LOAD_ATTR_MODULE, hits);
        res = entry->value;
        Py_INCREF(res);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_MODULE, NULL)
    }
    return res;
}

#ifdef ENABLE_CINDERX
static inline PyObject *
_PyShadow_LoadAttrStrictModule(_PyShadow_EvalState *shadow,
                         const _Py_CODEUNIT *next_instr,
                         _PyShadow_ModuleAttrEntry *entry,
                         PyObject *owner)
{
    PyObject *res;
    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_StrictModuleAttrEntryType.type);

    if (entry->module == owner) {
        if (entry->version != PYCACHE_STRICT_MODULE_VERSION(owner)) {
            PyObject * dict = ((PyStrictModuleObject *)owner)->globals;
            int unassigned = strictmodule_is_unassigned(dict, entry->name);
            if (unassigned == 0) {
                entry->value = _PyDict_GetItem_UnicodeExact(dict, entry->name);
            } else {
                entry->value = NULL;
            }

            if (entry->value == NULL) {
                LOAD_ATTR_CACHE_MISS(LOAD_ATTR_S_MODULE, NULL)
                return res;
            }
            entry->version = PYCACHE_STRICT_MODULE_VERSION(owner);
        }
        INLINE_CACHE_RECORD_STAT(LOAD_ATTR_S_MODULE, hits);
        res = entry->value;
        Py_INCREF(res);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_S_MODULE, NULL)
    }
    return res;
}
#endif

static _Py_ALWAYS_INLINE PyObject *
_PyShadow_LoadAttrNoDictDescrHit(_PyShadow_InstanceAttrEntry *entry,
                                 PyObject *owner)
{
    PyObject *res = entry->value;
    Py_INCREF(res);
    if (Py_TYPE(res)->tp_descr_get != NULL) {
        PyTypeObject *tp = Py_TYPE(owner);
        PyObject *got = Py_TYPE(res)->tp_descr_get(res, owner, (PyObject *)tp);
        Py_DECREF(res);
        res = got;
        if (res == NULL)
            return NULL;
    }
    return res;
}

static inline PyObject *
_PyShadow_LoadAttrNoDictDescr(_PyShadow_EvalState *shadow,
                              const _Py_CODEUNIT *next_instr,
                              _PyShadow_InstanceAttrEntry *entry,
                              PyObject *owner)
{
    assert(((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictMethod.type);

    PyTypeObject *tp = Py_TYPE(owner);
    PyObject *res;
    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "nodict_descr");
        INLINE_CACHE_RECORD_STAT(LOAD_ATTR_NO_DICT_DESCR, hits);
        return _PyShadow_LoadAttrNoDictDescrHit(entry, owner);
    } else {
        LOAD_ATTR_CACHE_MISS(LOAD_ATTR_NO_DICT_DESCR, entry->type)
    }
    return res;
}

#define LOAD_METHOD_CACHE_MISS(opcode, target)                                \
    INLINE_CACHE_RECORD_STAT(opcode, misses);                                 \
    return _PyShadow_LoadMethodInvalidate(                                    \
        shadow, next_instr, obj, entry->name, (PyObject *)target, meth);

static inline int
_PyShadow_LoadMethodSplitDictDescr(_PyShadow_EvalState *shadow,
                                   const _Py_CODEUNIT *next_instr,
                                   _PyShadow_InstanceAttrEntry *entry,
                                   PyObject *obj,
                                   PyObject **meth)
{
    PyObject *attr;
    PyTypeObject *tp = Py_TYPE(obj);

    assert(((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDict.type);

    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_splitdict_descr");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_SPLIT_DICT_DESCR, hits);

        Py_ssize_t dictoffset =
            _PyShadow_NormalizeDictOffset(obj, entry->dictoffset);
        PyObject **dictptr = (PyObject **)((char *)obj + dictoffset);

        *meth = entry->value;
        Py_XINCREF(*meth);

        attr = _PyShadow_TrySplitDictLookup(
            entry, *dictptr, LOAD_METHOD_SPLIT_DICT_DESCR);

        if (attr == NULL) {
            if (*meth == NULL) {
                PyErr_Format(PyExc_AttributeError,
                             "'%.50s' object has no attribute '%U'",
                             tp->tp_name,
                             entry->name);
                return 0;
            }

            if (Py_TYPE(*meth)->tp_descr_get != NULL) {
                PyObject *got =
                    Py_TYPE(*meth)->tp_descr_get(*meth, obj, (PyObject *)tp);
                Py_DECREF(*meth);
                *meth = got;
                return 0;
            }
        } else {
            Py_XDECREF(*meth);
            *meth = attr;
        }

        return 0;
    }
    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_SPLIT_DICT_DESCR, entry->type)
}

static inline int
_PyShadow_LoadMethodDictDescr(_PyShadow_EvalState *shadow,
                              const _Py_CODEUNIT *next_instr,
                              _PyShadow_InstanceAttrEntry *entry,
                              PyObject *obj,
                              PyObject **meth)
{
    PyObject **dictptr;
    PyObject *attr;

    assert(((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictNoDescr.type);

    PyTypeObject *tp = Py_TYPE(obj);
    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_dict_descr");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_DICT_DESCR, hits);

        dictptr = _PyObject_GetDictPtr(obj);
        *meth = entry->value;
        Py_XINCREF(*meth);
        if (*dictptr == NULL || (attr = _PyDict_GetItem_UnicodeExact(
                                     *dictptr, entry->name)) == NULL) {
            if (*meth == NULL) {
                PyErr_Format(PyExc_AttributeError,
                             "'%.50s' object has no attribute '%U'",
                             tp->tp_name,
                             entry->name);
                return 0;
            }

            if (Py_TYPE(*meth)->tp_descr_get != NULL) {
                PyObject *got =
                    Py_TYPE(*meth)->tp_descr_get(*meth, obj, (PyObject *)tp);
                Py_DECREF(*meth);
                *meth = got;
                if (*meth == NULL)
                    return 0;
            }
        } else {
            Py_XDECREF(*meth);
            Py_INCREF(attr); /* got a borrowed ref */
            *meth = attr;
        }

        return 0;
    }

    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_DICT_DESCR, entry->type)
}

static inline int
_PyShadow_LoadMethodNoDictDescr(_PyShadow_EvalState *shadow,
                                const _Py_CODEUNIT *next_instr,
                                _PyShadow_InstanceAttrEntry *entry,
                                PyObject *obj,
                                PyObject **meth)
{
    PyTypeObject *tp = Py_TYPE(obj);

    assert(((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type ||
           ((PyObject *)entry)->ob_type == &_PyShadow_InstanceCacheSlot.type);

    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_nodict_descr");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_NO_DICT_DESCR, hits);
        *meth = entry->value;
        Py_INCREF(*meth);

        if (Py_TYPE(*meth)->tp_descr_get != NULL) {
            PyObject *got =
                Py_TYPE(*meth)->tp_descr_get(*meth, obj, (PyObject *)tp);
            Py_DECREF(*meth);
            *meth = got;
            if (*meth == NULL)
                return 0;
        }

        return 0;
    }
    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_NO_DICT_DESCR, entry->type)
}

static inline int
_PyShadow_LoadMethodType(_PyShadow_EvalState *shadow,
                         const _Py_CODEUNIT *next_instr,
                         _PyShadow_InstanceAttrEntry *entry,
                         PyObject *obj,
                         PyObject **meth)
{
    assert(((PyObject *)entry)->ob_type == &_PyShadow_InstanceCacheSlot.type ||
           /* this "NoDescr" case is because of our special handling of
            * cached_property backed by dict */
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictNoDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type);

    if ((PyObject *)entry->type == obj) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_nodict_type_descr");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_TYPE, hits);
        *meth = entry->value;
        Py_INCREF(*meth);

        if (Py_TYPE(*meth)->tp_descr_get != NULL) {
            PyObject *got = Py_TYPE(*meth)->tp_descr_get(*meth, NULL, obj);
            Py_DECREF(*meth);
            *meth = got;
            if (*meth == NULL) {
                return 0;
            }
        }
        return 0;
    }
    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_TYPE, entry->type)
}

static inline int
_PyShadow_LoadMethodTypeMethodLike(_PyShadow_EvalState *shadow,
                                   const _Py_CODEUNIT *next_instr,
                                   _PyShadow_InstanceAttrEntry *entry,
                                   PyObject *obj,
                                   PyObject **meth)
{
    assert(((PyObject *)entry)->ob_type == &_PyShadow_InstanceCacheSlot.type ||
           /* this "NoDescr" case is because of our special handling of
            * cached_property backed by dict */
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictNoDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheSplitDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictDescr.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictDescr.type);

    if ((PyObject *)entry->type == obj) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_type_methodlike");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_TYPE_METHODLIKE, hits);
        uintptr_t data = entry->load_method_type_data;
        *meth = _PyShadow_GetMethodLike(data);
        return _PyShadow_GetMethCallKind(data);
    }
    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_TYPE_METHODLIKE, entry->type)
}

static inline int
_PyShadow_LoadMethodUnshadowedMethod(_PyShadow_EvalState *shadow,
                                             const _Py_CODEUNIT *next_instr,
                                             _PyShadow_InstanceAttrEntry *entry,
                                             PyObject *obj,
                                             PyObject **meth)
{
    PyTypeObject *tp = Py_TYPE(obj);

    assert(((PyObject *)entry)->ob_type == &_PyShadow_InstanceCacheSplitDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheDictMethod.type ||
           ((PyObject *)entry)->ob_type ==
               &_PyShadow_InstanceCacheNoDictMethod.type);

    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_unshadowed_immortal_method");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_UNSHADOWED_METHOD, hits);
        *meth = entry->value;
        return 1;
    }
    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_UNSHADOWED_METHOD, entry->type)
}

static inline int
_PyShadow_LoadMethodDictMethod(_PyShadow_EvalState *shadow,
                               const _Py_CODEUNIT *next_instr,
                               _PyShadow_InstanceAttrEntry *entry,
                               PyObject *obj,
                               PyObject **meth)
{
    PyTypeObject *tp = Py_TYPE(obj);

    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_InstanceCacheDictMethod.type);

    if (entry->type == tp) {
        *meth = entry->value;
        Py_INCREF(*meth);
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_dict_method");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_DICT_METHOD, hits);
        PyObject **dictptr = _PyObject_GetDictPtr(obj);
        PyObject *attr;
        if (*dictptr == NULL || (attr = _PyDict_GetItem_UnicodeExact(
                                     *dictptr, entry->name)) == NULL) {
            return 1;
        } else {
            Py_DECREF(*meth);
            Py_INCREF(attr); /* got a borrowed ref */
            *meth = attr;
            return 0;
        }
    }

    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_DICT_METHOD, entry->type)
}

static inline int
_PyShadow_LoadMethodSplitDictMethod(_PyShadow_EvalState *shadow,
                                    const _Py_CODEUNIT *next_instr,
                                    _PyShadow_InstanceAttrEntry *entry,
                                    PyObject *obj,
                                    PyObject **meth)
{
    PyTypeObject *tp = Py_TYPE(obj);

    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_InstanceCacheSplitDictMethod.type);

    if (entry->type == tp) {
        Py_ssize_t dictoffset =
            _PyShadow_NormalizeDictOffset(obj, entry->dictoffset);
        PyObject **dictptr = (PyObject **)((char *)obj + dictoffset);
        PyObject *attr;

        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_splitdict_method");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_DICT_METHOD, hits);
        *meth = entry->value;
        Py_INCREF(*meth);

        attr = _PyShadow_TrySplitDictLookup(
            entry, *dictptr, LOAD_METHOD_SPLIT_DICT_DESCR);

        if (attr == NULL) {
            return 1;
        }

        Py_DECREF(*meth);
        *meth = attr;
        return 0;
    }

    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_DICT_METHOD, entry->type)
}

static inline int
_PyShadow_LoadMethodNoDictMethod(_PyShadow_EvalState *shadow,
                                 const _Py_CODEUNIT *next_instr,
                                 _PyShadow_InstanceAttrEntry *entry,
                                 PyObject *obj,
                                 PyObject **meth)
{
    PyTypeObject *tp = Py_TYPE(obj);

    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_InstanceCacheNoDictMethod.type);

    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "loadmethod_nodict_method");
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_NO_DICT_METHOD, hits);
        *meth = entry->value;
        Py_INCREF(*meth);
        return 1;
    }

    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_NO_DICT_METHOD, entry->type)
}

static inline int
_PyShadow_LoadMethodModule(_PyShadow_EvalState *shadow,
                           const _Py_CODEUNIT *next_instr,
                           _PyShadow_ModuleAttrEntry *entry,
                           PyObject *obj,
                           PyObject **meth)
{
    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_ModuleAttrEntryType.type);
    if (entry->module == obj) {
        if (entry->version != PYCACHE_MODULE_VERSION(obj)) {
            entry->value = _PyDict_GetItem_UnicodeExact(
                ((PyModuleObject *)obj)->md_dict, entry->name);
            if (entry->value == NULL) {
                LOAD_METHOD_CACHE_MISS(LOAD_METHOD_MODULE, NULL)
            }
            entry->version = PYCACHE_MODULE_VERSION(obj);
        }
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_MODULE, hits);
        *meth = entry->value;
        Py_INCREF(*meth);

        return 0;
    }

    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_MODULE, NULL)
}

#ifdef ENABLE_CINDERX
static inline int
_PyShadow_LoadMethodStrictModule(_PyShadow_EvalState *shadow,
                           const _Py_CODEUNIT *next_instr,
                           _PyShadow_ModuleAttrEntry *entry,
                           PyObject *obj,
                           PyObject **meth)
{
    assert(((PyObject *)entry)->ob_type ==
           &_PyShadow_StrictModuleAttrEntryType.type);
    if (entry->module == obj) {
        if (entry->version != PYCACHE_STRICT_MODULE_VERSION(obj)) {
            PyObject *dict = ((PyStrictModuleObject *)obj)->globals;
            int unassigned = strictmodule_is_unassigned(dict, entry->name);
            if (unassigned != 0) {
                entry->value = NULL;
            } else {
                entry->value = _PyDict_GetItem_UnicodeExact(dict, entry->name);
            }

            if (entry->value == NULL) {
                LOAD_METHOD_CACHE_MISS(LOAD_METHOD_S_MODULE, NULL)
            }
            entry->version = PYCACHE_STRICT_MODULE_VERSION(obj);
        }
        INLINE_CACHE_RECORD_STAT(LOAD_METHOD_S_MODULE, hits);
        *meth = entry->value;
        Py_INCREF(*meth);

        return 0;
    }

    LOAD_METHOD_CACHE_MISS(LOAD_METHOD_S_MODULE, NULL)
}
#endif

#define STORE_ATTR_CACHE_MISS(opcode, target, v)                              \
    INLINE_CACHE_RECORD_STAT(opcode, misses);                                 \
    if (_PyShadow_StoreAttrInvalidate(shadow,                                   \
                                    next_instr,                               \
                                    owner,                                    \
                                    entry->name,                              \
                                    v,                                        \
                                    (PyObject *)entry->type))                 \
        return -1;

static inline int
_PyShadow_StoreAttrDict(_PyShadow_EvalState *shadow,
                        const _Py_CODEUNIT *next_instr,
                        _PyShadow_InstanceAttrEntry *entry,
                        PyObject *owner,
                        PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(owner);
    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_RECORD_STAT(STORE_ATTR_DICT, hits);
        INLINE_CACHE_TYPE_STAT(tp, "dict");
        Py_ssize_t dictoffset =
            _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);

        PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
        PyObject *dict = *dictptr;

        if (dict == NULL) {
            dict = PyObject_GenericGetDict(owner, NULL);
            if (dict == NULL) {
                return -1;
            }
            Py_DECREF(dict);
        }
        return PyDict_SetItem(dict, entry->name, v);
    } else {
        STORE_ATTR_CACHE_MISS(STORE_ATTR_DICT, entry->type, v)
    }
    return 0;
}

static inline int
_PyShadow_StoreAttrDictMethod(_PyShadow_EvalState *shadow,
                              const _Py_CODEUNIT *next_instr,
                              _PyShadow_InstanceAttrEntry *entry,
                              PyObject *owner,
                              PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(owner);
    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_RECORD_STAT(STORE_ATTR_DICT, hits);
        INLINE_CACHE_TYPE_STAT(tp, "dict");
        Py_ssize_t dictoffset =
            _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);

        PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
        PyObject *dict = *dictptr;

        if (dict == NULL) {
            dict = PyObject_GenericGetDict(owner, NULL);
            if (dict == NULL) {
                return -1;
            }
            Py_DECREF(dict);
        }
        _PyType_ClearNoShadowingInstances(tp, entry->value);
        return PyDict_SetItem(dict, entry->name, v);
    } else {
        STORE_ATTR_CACHE_MISS(STORE_ATTR_DICT_METHOD, entry->type, v)
    }
    return 0;
}

static inline int
_PyShadow_StoreAttrDescr(_PyShadow_EvalState *shadow,
                         const _Py_CODEUNIT *next_instr,
                         _PyShadow_InstanceAttrEntry *entry,
                         PyObject *owner,
                         PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(owner);
    if (entry->type == tp) {
        /* Cache hit */
        INLINE_CACHE_TYPE_STAT(tp, "nodict_store_descr");
        INLINE_CACHE_RECORD_STAT(STORE_ATTR_DESCR, hits);
        PyObject *descr = entry->value;
        if (Py_TYPE(descr)->tp_descr_set != NULL) {
            Py_INCREF(descr);
            int res = Py_TYPE(descr)->tp_descr_set(descr, owner, v);
            Py_DECREF(descr);

            if (res == -1) {
                return -1;
            }
        } else {
            /* the descriptor type changed, it's no longer a data descriptor */
            return PyObject_SetAttr(owner, entry->name, v);
        }
    } else {
        STORE_ATTR_CACHE_MISS(STORE_ATTR_DESCR, entry->type, v)
    }
    return 0;
}

static inline int
_PyShadow_StoreAttrSplitDictSet(_PyShadow_EvalState *shadow,
                                const _Py_CODEUNIT *next_instr,
                                _PyShadow_InstanceAttrEntry *entry,
                                PyObject *owner,
                                PyObject *v)
{
    /* Cache hit */
    Py_ssize_t dictoffset =
        _PyShadow_NormalizeDictOffset(owner, entry->dictoffset);
    PyObject **dictptr = (PyObject **)((char *)owner + dictoffset);
    PyObject *dict = *dictptr;
    PyDictObject *dictobj;

    if (dict == NULL) {
        dict = PyObject_GenericGetDict(owner, NULL);
        if (dict == NULL) {
            return -1;
        }
        Py_DECREF(dict); /* GenericGetDict returns new ref */
    }

    INLINE_CACHE_TYPE_STAT(Py_TYPE(owner), "fastdict_store");

    dictobj = (PyDictObject *)dict;
    if (_PyDict_HasSplitTable(dictobj) &&
        entry->splitoffset != -1 &&
        (dictobj->ma_used == entry->splitoffset ||
         dictobj->ma_values[entry->splitoffset] != NULL)) {
        PyObject *old_value = dictobj->ma_values[entry->splitoffset];

        if (!_PyObject_GC_IS_TRACKED(dict)) {
            if (_PyObject_GC_MAY_BE_TRACKED(v)) {
                _PyObject_GC_TRACK(dict);
            }
        }

        INLINE_CACHE_RECORD_STAT(STORE_ATTR_SPLIT_DICT, hits);

        Py_INCREF(v);
        dictobj->ma_values[entry->splitoffset] = v;
        _PyDict_IncVersionForSet(dictobj, entry->name, v);

        if (old_value == NULL) {
            dictobj->ma_used++;
        } else {
            Py_DECREF(old_value);
        }
    } else if (PyDict_SetItem(dict, entry->name, v) == -1) {
        return -1;
    } else if (entry->splitoffset == -1 && _PyDict_HasSplitTable(dictobj)) {
        entry->splitoffset =
            _PyDictKeys_GetSplitIndex(dictobj->ma_keys, entry->name);
    }
    return 0;
}

static inline int
_PyShadow_StoreAttrSplitDict(_PyShadow_EvalState *shadow,
                             const _Py_CODEUNIT *next_instr,
                             _PyShadow_InstanceAttrEntry *entry,
                             PyObject *owner,
                             PyObject *v)
{
    if (entry->type == Py_TYPE(owner)) {
        return _PyShadow_StoreAttrSplitDictSet(
            shadow, next_instr, entry, owner, v);
    } else {
        STORE_ATTR_CACHE_MISS(STORE_ATTR_SPLIT_DICT, entry->type, v)
    }
    return 0;
}

static inline int
_PyShadow_StoreAttrSplitDictMethod(_PyShadow_EvalState *shadow,
                                   const _Py_CODEUNIT *next_instr,
                                   _PyShadow_InstanceAttrEntry *entry,
                                   PyObject *owner,
                                   PyObject *v)
{
    if (entry->type == Py_TYPE(owner)) {
        _PyType_ClearNoShadowingInstances(entry->type, entry->value);
        return _PyShadow_StoreAttrSplitDictSet(
            shadow, next_instr, entry, owner, v);
    } else {
        STORE_ATTR_CACHE_MISS(STORE_ATTR_SPLIT_DICT_METHOD, entry->type, v)
    }
    return 0;
}

static inline int
_PyShadow_StoreAttrSlot(_PyShadow_EvalState *shadow,
                        const _Py_CODEUNIT *next_instr,
                        _PyShadow_InstanceAttrEntry *entry,
                        PyObject *owner,
                        PyObject *v)
{
    if (entry->type == Py_TYPE(owner)) {
        PyObject *old_value =
            *(PyObject **)((char *)owner + entry->splitoffset);
        *(PyObject **)((char *)owner + entry->splitoffset) = v;
        Py_INCREF(v);
        Py_XDECREF(old_value);
    } else {
        STORE_ATTR_CACHE_MISS(STORE_ATTR_SLOT, entry->type, v)
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
#endif /* !Py_SHADOWCODE_H */
