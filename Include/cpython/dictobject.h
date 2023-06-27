#ifndef Py_CPYTHON_DICTOBJECT_H
#  error "this header file must not be included directly"
#endif

#include "cinder/ci_api.h"

typedef struct _dictkeysobject PyDictKeysObject;
typedef struct _dictkeyentry PyDictKeyEntry;

/* The ma_values pointer is NULL for a combined table
 * or points to an array of PyObject* for a split table
 */
typedef struct {
    PyObject_HEAD

    /* Number of items in the dictionary */
    Py_ssize_t ma_used;

    /* Dictionary version: globally unique, value change each time
       the dictionary is modified */
    uint64_t ma_version_tag;

    PyDictKeysObject *ma_keys;

    /* If ma_values is NULL, the table is "combined": keys and values
       are stored in ma_keys.

       If ma_values is not NULL, the table is split:
       keys are stored in ma_keys and values are stored in ma_values */
    PyObject **ma_values;
} PyDictObject;

PyAPI_FUNC(PyObject *) _PyDict_GetItem_KnownHash(PyObject *mp, PyObject *key,
                                       Py_hash_t hash);
PyAPI_FUNC(PyObject *) _PyDict_GetItemIdWithError(PyObject *dp,
                                                  struct _Py_Identifier *key);
PyAPI_FUNC(PyObject *) _PyDict_GetItemStringWithError(PyObject *, const char *);
PyAPI_FUNC(PyObject *) PyDict_SetDefault(
    PyObject *mp, PyObject *key, PyObject *defaultobj);
PyAPI_FUNC(int) _PyDict_SetItem_KnownHash(PyObject *mp, PyObject *key,
                                          PyObject *item, Py_hash_t hash);
PyAPI_FUNC(int) _PyDict_DelItem_KnownHash(PyObject *mp, PyObject *key,
                                          Py_hash_t hash);
PyAPI_FUNC(int) _PyDict_DelItemIf(PyObject *mp, PyObject *key,
                                  int (*predicate)(PyObject *value));
PyDictKeysObject *_PyDict_NewKeysForClass(void);
PyAPI_FUNC(int) _PyDict_Next(
    PyObject *mp, Py_ssize_t *pos, PyObject **key, PyObject **value, Py_hash_t *hash);

/* Get the number of items of a dictionary. */
#define PyDict_GET_SIZE(mp)  (assert(PyDict_Check(mp)),((PyDictObject *)mp)->ma_used)
PyAPI_FUNC(int) _PyDict_Contains_KnownHash(PyObject *, PyObject *, Py_hash_t);
PyAPI_FUNC(int) _PyDict_ContainsId(PyObject *, struct _Py_Identifier *);
PyAPI_FUNC(PyObject *) _PyDict_NewPresized(Py_ssize_t minused);
PyAPI_FUNC(void) _PyDict_MaybeUntrack(PyObject *mp);
PyAPI_FUNC(int) _PyDict_HasOnlyStringKeys(PyObject *mp);
Py_ssize_t _PyDict_KeysSize(PyDictKeysObject *keys);
PyAPI_FUNC(Py_ssize_t) _PyDict_SizeOf(PyDictObject *);
PyAPI_FUNC(PyObject *) _PyDict_Pop(PyObject *, PyObject *, PyObject *);
PyObject *_PyDict_Pop_KnownHash(PyObject *, PyObject *, Py_hash_t, PyObject *);
PyObject *_PyDict_FromKeys(PyObject *, PyObject *, PyObject *);
#define _PyDict_HasSplitTable(d) ((d)->ma_values != NULL)
/* facebook begin t39538061 */
CiAPI_FUNC(Py_ssize_t) _PyDictKeys_GetSplitIndex(PyDictKeysObject *keys, PyObject *key);
/* facebook end t39538061 */

CiAPI_FUNC(void) _PyDictKeys_DecRef(PyDictKeysObject *keys);
CiAPI_FUNC(PyDictKeysObject *) _PyDict_MakeKeysShared(PyObject *dict);

CiAPI_FUNC(PyDictKeyEntry *) _PyDictKeys_GetEntries(PyDictKeysObject *keys);

/* Like PyDict_Merge, but override can be 0, 1 or 2.  If override is 0,
   the first occurrence of a key wins, if override is 1, the last occurrence
   of a key wins, if override is 2, a KeyError with conflicting key as
   argument is raised.
*/
PyAPI_FUNC(int) _PyDict_MergeEx(PyObject *mp, PyObject *other, int override);
PyAPI_FUNC(int) _PyDict_SetItemId(PyObject *dp, struct _Py_Identifier *key, PyObject *item);

PyAPI_FUNC(int) _PyDict_DelItemId(PyObject *mp, struct _Py_Identifier *key);
PyAPI_FUNC(void) _PyDict_DebugMallocStats(FILE *out);

CiAPI_FUNC(int) _PyObjectDict_SetItem(PyTypeObject *tp, PyObject **dictptr, PyObject *name, PyObject *value);
CiAPI_FUNC(PyObject *) _PyDict_LoadGlobal(PyDictObject *, PyDictObject *, PyObject *);
Py_ssize_t _PyDict_GetItemHint(PyDictObject *, PyObject *, Py_ssize_t, PyObject **);

/* _PyDictView */

typedef struct {
    PyObject_HEAD
    PyDictObject *dv_dict;
} _PyDictViewObject;

PyAPI_FUNC(PyObject *) _PyDictView_New(PyObject *, PyTypeObject *);
PyAPI_FUNC(PyObject *) _PyDictView_Intersect(PyObject* self, PyObject *other);

/* Dictionary watchers */

PyAPI_DATA(uint64_t) _pydict_global_version;

#define PY_FOREACH_DICT_EVENT(V) \
    V(ADDED)                     \
    V(MODIFIED)                  \
    V(DELETED)                   \
    V(CLONED)                    \
    V(CLEARED)                   \
    V(DEALLOCATED)

typedef enum {
    #define PY_DEF_EVENT(EVENT) PyDict_EVENT_##EVENT,
    PY_FOREACH_DICT_EVENT(PY_DEF_EVENT)
    #undef PY_DEF_EVENT
} PyDict_WatchEvent;

#define DICT_MAX_WATCHERS 8

#define DICT_VERSION_INCREMENT (1 << DICT_MAX_WATCHERS)
#define DICT_VERSION_MASK (DICT_VERSION_INCREMENT - 1)

#define DICT_NEXT_VERSION() (_pydict_global_version += DICT_VERSION_INCREMENT)

PyAPI_FUNC(void) _PyDict_SendEvent(int watcher_bits,
                                   PyDict_WatchEvent event,
                                   PyDictObject *mp,
                                   PyObject *key,
                                   PyObject *value);

static inline uint64_t
_PyDict_NotifyEvent(PyDict_WatchEvent event,
                    PyDictObject *mp,
                    PyObject *key,
                    PyObject *value)
{
    assert(Py_REFCNT((PyObject*)mp) > 0);
    int watcher_bits = mp->ma_version_tag & DICT_VERSION_MASK;
    if (watcher_bits) {
        _PyDict_SendEvent(watcher_bits, event, mp, key, value);
        return DICT_NEXT_VERSION() | watcher_bits;
    }
    return DICT_NEXT_VERSION();
}

// Callback to be invoked when a watched dict is cleared, dealloced, or modified.
// In clear/dealloc case, key and new_value will be NULL. Otherwise, new_value will be the
// new value for key, NULL if key is being deleted.
typedef int(*PyDict_WatchCallback)(PyDict_WatchEvent event, PyObject* dict, PyObject* key, PyObject* new_value);

// Register/unregister a dict-watcher callback
PyAPI_FUNC(int) PyDict_AddWatcher(PyDict_WatchCallback callback);
PyAPI_FUNC(int) PyDict_ClearWatcher(int watcher_id);

// Mark given dictionary as "watched" (callback will be called if it is modified)
PyAPI_FUNC(int) PyDict_Watch(int watcher_id, PyObject* dict);
PyAPI_FUNC(int) PyDict_Unwatch(int watcher_id, PyObject* dict);

/* Cinder _PyDict_GetItem_* specializations. */

CiAPI_FUNC(PyObject *)_PyDict_GetItem_Unicode(PyObject *op, PyObject *key);
CiAPI_FUNC(PyObject *)_PyDict_GetItem_String_KnownHash(PyObject *op,
                                           const char *key,
                                           Py_ssize_t len,
                                           Py_hash_t hash);
CiAPI_FUNC(PyObject *)_PyDict_GetItem_UnicodeExact(PyObject *op, PyObject *key);
CiAPI_FUNC(PyObject *) _PyDict_GetItem_StackKnownHash(PyObject *op,
                                         PyObject *const *stack,
                                         Py_ssize_t nargs,
                                         Py_hash_t hash);

/* Dict watchers (Cinder-only) TODO move to CinderX */

/* Return 1 if the given dict has unicode-only keys, or 0 otherwise. */
CiAPI_FUNC(int) _PyDict_HasOnlyUnicodeKeys(PyObject *);

/* Return false if PyDict_Lookup() on the given dict is guaranteed to not cause
 * any heap mutations. */
CiAPI_FUNC(int) _PyDict_HasUnsafeKeys(PyObject *);

/* Lazy imports */

/* Return 1 if the given dict has deferred objects, or 0 otherwise. */
CiAPI_FUNC(int) _PyDict_HasDeferredObjects(PyObject *);

/* Flag dictionary as having deferred objects in it */
CiAPI_FUNC(void) _PyDict_SetHasDeferredObjects(PyObject *);

/* Unflag dictionary as having deferred objects in it */
CiAPI_FUNC(void) _PyDict_UnsetHasDeferredObjects(PyObject *);

CiAPI_FUNC(Py_ssize_t) PyDict_ResolveLazyImports(PyObject *);
