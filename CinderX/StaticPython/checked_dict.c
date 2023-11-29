#include "Python.h"
#include "pycore_bitutils.h"         // _Py_bit_length
#include "pycore_gc.h"               // _PyObject_GC_IS_TRACKED()
#include "pycore_interp.h"           // PyDict_MAXFREELIST
#include "pycore_object.h"           // _PyObject_GC_TRACK()
#include "pycore_pystate.h"          // _Py_InterpreterState_GET
#include "Objects/dict-common.h"
#include "Objects/stringlib/eq.h"    // unicode_eq()

#include "cinder/exports.h"

#include "StaticPython/checked_dict.h"
#include "StaticPython/classloader.h"

/***********************************************************************
 * Type-enforced dictionary - shares most of the implementation with the
 * standard builtin dictioanry.  Replaces things which can do mutation
 * with a version that forces type checks when called from un-typed
 * Python code.  Statically Typed Python code will be able to call versions
 * of most functionality in a way that elides the type checks */

CiAPI_DATA(_PyGenericTypeDef) Ci_CheckedDict_Type;

CiAPI_DATA(PyTypeObject) Ci_CheckedDictKeys_Type;
CiAPI_DATA(PyTypeObject) Ci_CheckedDictValues_Type;
CiAPI_DATA(PyTypeObject) Ci_CheckedDictItems_Type;

#define Ci_CheckedDictKeys_Check(op) PyObject_TypeCheck(op, &Ci_CheckedDictKeys_Type)
#define Ci_CheckedDictValues_Check(op) PyObject_TypeCheck(op, &Ci_CheckedDictValues_Type)
#define Ci_CheckedDictItems_Check(op) PyObject_TypeCheck(op, &Ci_CheckedDictItems_Type)
/* This excludes Values, since they are not sets. */
# define Ci_CheckedDictViewSet_Check(op) \
    (Ci_CheckedDictKeys_Check(op) || Ci_CheckedDictItems_Check(op))

CiAPI_DATA(PyTypeObject) Ci_CheckedDictIterKey_Type;
CiAPI_DATA(PyTypeObject) Ci_CheckedDictIterValue_Type;
CiAPI_DATA(PyTypeObject) Ci_CheckedDictIterItem_Type;

CiAPI_DATA(PyTypeObject) Ci_CheckedDictRevIterKey_Type;
CiAPI_DATA(PyTypeObject) Ci_CheckedDictRevIterItem_Type;
CiAPI_DATA(PyTypeObject) Ci_CheckedDictRevIterValue_Type;

inline int
Ci_DictOrChecked_SetItemInternal(PyObject *op, PyObject *key, PyObject *value) {
    if (PyDict_Check(op)) {
        return Ci_Dict_SetItemInternal(op, key, value);
    } else if (Ci_CheckedDict_Check(op)) {
        return Ci_CheckedDict_SetItemInternal(op, key, value);
    } else {
        PyErr_BadInternalCall();
        return -1;
    }
}

Py_ssize_t Ci_CheckedDict_KeysSize(PyDictKeysObject *keys);

/* === Copied from dictobject.c.h === */

PyDoc_STRVAR(dict_fromkeys__doc__,
"fromkeys($type, iterable, value=None, /)\n"
"--\n"
"\n"
"Create a new dictionary with keys from iterable and values set to value.");

PyDoc_STRVAR(dict___contains____doc__,
"__contains__($self, key, /)\n"
"--\n"
"\n"
"True if the dictionary has the specified key, else False.");

#define DICT___CONTAINS___METHODDEF    \
    {"__contains__", (PyCFunction)dict___contains__, METH_O|METH_COEXIST, dict___contains____doc__},

PyDoc_STRVAR(dict_get__doc__,
"get($self, key, default=None, /)\n"
"--\n"
"\n"
"Return the value for key if key is in the dictionary, else default.");

PyDoc_STRVAR(dict_setdefault__doc__,
"setdefault($self, key, default=None, /)\n"
"--\n"
"\n"
"Insert key with a value of default if key is not in the dictionary.\n"
"\n"
"Return the value for key if key is in the dictionary, else default.");

PyDoc_STRVAR(dict_pop__doc__,
"pop($self, key, default=<unrepresentable>, /)\n"
"--\n"
"\n"
"D.pop(k[,d]) -> v, remove specified key and return the corresponding value.\n"
"\n"
"If the key is not found, return the default if given; otherwise,\n"
"raise a KeyError.");

#define DICT_POP_METHODDEF    \
    {"pop", (PyCFunction)(void(*)(void))dict_pop, METH_FASTCALL, dict_pop__doc__},

static PyObject *
dict_pop_impl(PyDictObject *self, PyObject *key, PyObject *default_value);

static PyObject *
dict_pop(PyDictObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *key;
    PyObject *default_value = NULL;

    if (!_PyArg_CheckPositional("pop", nargs, 1, 2)) {
        goto exit;
    }
    key = args[0];
    if (nargs < 2) {
        goto skip_optional;
    }
    default_value = args[1];
skip_optional:
    return_value = dict_pop_impl(self, key, default_value);

exit:
    return return_value;
}

PyDoc_STRVAR(dict_popitem__doc__,
"popitem($self, /)\n"
"--\n"
"\n"
"Remove and return a (key, value) pair as a 2-tuple.\n"
"\n"
"Pairs are returned in LIFO (last-in, first-out) order.\n"
"Raises KeyError if the dict is empty.");

#define DICT_POPITEM_METHODDEF    \
    {"popitem", (PyCFunction)dict_popitem, METH_NOARGS, dict_popitem__doc__},

static PyObject *
dict_popitem_impl(PyDictObject *self);

static PyObject *
dict_popitem(PyDictObject *self, PyObject *Py_UNUSED(ignored))
{
    return dict_popitem_impl(self);
}

PyDoc_STRVAR(dict___reversed____doc__,
"__reversed__($self, /)\n"
"--\n"
"\n"
"Return a reverse iterator over the dict keys.");

#define DICT___REVERSED___METHODDEF    \
    {"__reversed__", (PyCFunction)dict___reversed__, METH_NOARGS, dict___reversed____doc__},

static PyObject *
dict___reversed___impl(PyDictObject *self);

static PyObject *
dict___reversed__(PyDictObject *self, PyObject *Py_UNUSED(ignored))
{
    return dict___reversed___impl(self);
}

/* === End copied from dictobject.c.h === */

/* === Copied from dictobject.c === */

/* Changed only to rename some PyDict_* exposed elsewhere to CiCheckedDict_*, and
 * change some PyDict_Check to Ci_CheckedDict_Check. */

#define PERTURB_SHIFT 5
#define PyDict_MINSIZE 8

/* forward declarations */
static Py_ssize_t lookdict(PyDictObject *mp, PyObject *key,
                           Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports);
static Py_ssize_t lookdict_unicode(PyDictObject *mp, PyObject *key,
                                   Py_hash_t hash, PyObject **value_addr,
                                   int resolve_lazy_imports);
static Py_ssize_t
lookdict_unicode_nodummy(PyDictObject *mp, PyObject *key,
                         Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports);
static Py_ssize_t lookdict_split(PyDictObject *mp, PyObject *key,
                                 Py_hash_t hash, PyObject **value_addr,
                                 int resolve_lazy_imports);

static int dictresize(PyDictObject *mp, Py_ssize_t newsize);

static PyObject* dict_iter(PyDictObject *dict);

static void free_keys_object(PyDictKeysObject *keys);

static struct _Py_dict_state *
get_dict_state(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return &interp->dict_state;
}

#define DK_SIZE(dk) ((dk)->dk_size)
#if SIZEOF_VOID_P > 4
#define DK_IXSIZE(dk)                          \
    (DK_SIZE(dk) <= 0xff ?                     \
        1 : DK_SIZE(dk) <= 0xffff ?            \
            2 : DK_SIZE(dk) <= 0xffffffff ?    \
                4 : sizeof(int64_t))
#else
#define DK_IXSIZE(dk)                          \
    (DK_SIZE(dk) <= 0xff ?                     \
        1 : DK_SIZE(dk) <= 0xffff ?            \
            2 : sizeof(int32_t))
#endif
#define DK_ENTRIES(dk) \
    ((PyDictKeyEntry*)(&((int8_t*)((dk)->dk_indices))[DK_SIZE(dk) * DK_IXSIZE(dk)]))

#define DK_MASK(dk) (((dk)->dk_size)-1)
#define IS_POWER_OF_2(x) (((x) & (x-1)) == 0)

static inline void
dictkeys_incref(PyDictKeysObject *dk)
{
#ifdef Py_REF_DEBUG
    _Py_RefTotal++;
#endif
    dk->dk_refcnt++;
}

static inline void
dictkeys_decref(PyDictKeysObject *dk)
{
    assert(dk->dk_refcnt > 0);
#ifdef Py_REF_DEBUG
    _Py_RefTotal--;
#endif
    if (--dk->dk_refcnt == 0) {
        free_keys_object(dk);
    }
}

/* lookup indices.  returns DKIX_EMPTY, DKIX_DUMMY, or ix >=0 */
static inline Py_ssize_t
dictkeys_get_index(const PyDictKeysObject *keys, Py_ssize_t i)
{
    Py_ssize_t s = DK_SIZE(keys);
    Py_ssize_t ix;

    if (s <= 0xff) {
        const int8_t *indices = (const int8_t*)(keys->dk_indices);
        ix = indices[i];
    }
    else if (s <= 0xffff) {
        const int16_t *indices = (const int16_t*)(keys->dk_indices);
        ix = indices[i];
    }
#if SIZEOF_VOID_P > 4
    else if (s > 0xffffffff) {
        const int64_t *indices = (const int64_t*)(keys->dk_indices);
        ix = indices[i];
    }
#endif
    else {
        const int32_t *indices = (const int32_t*)(keys->dk_indices);
        ix = indices[i];
    }
    assert(ix >= DKIX_DUMMY);
    return ix;
}

/* write to indices. */
static inline void
dictkeys_set_index(PyDictKeysObject *keys, Py_ssize_t i, Py_ssize_t ix)
{
    Py_ssize_t s = DK_SIZE(keys);

    assert(ix >= DKIX_DUMMY);

    if (s <= 0xff) {
        int8_t *indices = (int8_t*)(keys->dk_indices);
        assert(ix <= 0x7f);
        indices[i] = (char)ix;
    }
    else if (s <= 0xffff) {
        int16_t *indices = (int16_t*)(keys->dk_indices);
        assert(ix <= 0x7fff);
        indices[i] = (int16_t)ix;
    }
#if SIZEOF_VOID_P > 4
    else if (s > 0xffffffff) {
        int64_t *indices = (int64_t*)(keys->dk_indices);
        indices[i] = ix;
    }
#endif
    else {
        int32_t *indices = (int32_t*)(keys->dk_indices);
        assert(ix <= 0x7fffffff);
        indices[i] = (int32_t)ix;
    }
}

/* USABLE_FRACTION is the maximum dictionary load.
 * Increasing this ratio makes dictionaries more dense resulting in more
 * collisions.  Decreasing it improves sparseness at the expense of spreading
 * indices over more cache lines and at the cost of total memory consumed.
 *
 * USABLE_FRACTION must obey the following:
 *     (0 < USABLE_FRACTION(n) < n) for all n >= 2
 *
 * USABLE_FRACTION should be quick to calculate.
 * Fractions around 1/2 to 2/3 seem to work well in practice.
 */
#define USABLE_FRACTION(n) (((n) << 1)/3)

/* Find the smallest dk_size >= minsize. */
static inline Py_ssize_t
calculate_keysize(Py_ssize_t minsize)
{
#if SIZEOF_LONG == SIZEOF_SIZE_T
    minsize = (minsize | PyDict_MINSIZE) - 1;
    return 1LL << _Py_bit_length(minsize | (PyDict_MINSIZE-1));
#elif defined(_MSC_VER)
    // On 64bit Windows, sizeof(long) == 4.
    minsize = (minsize | PyDict_MINSIZE) - 1;
    unsigned long msb;
    _BitScanReverse64(&msb, (uint64_t)minsize);
    return 1LL << (msb + 1);
#else
    Py_ssize_t size;
    for (size = PyDict_MINSIZE;
            size < minsize && size > 0;
            size <<= 1)
        ;
    return size;
#endif
}

/* estimate_keysize is reverse function of USABLE_FRACTION.
 *
 * This can be used to reserve enough size to insert n entries without
 * resizing.
 */
static inline Py_ssize_t
estimate_keysize(Py_ssize_t n)
{
    return calculate_keysize((n*3 + 1) / 2);
}


/* GROWTH_RATE. Growth rate upon hitting maximum load.
 * Currently set to used*3.
 * This means that dicts double in size when growing without deletions,
 * but have more head room when the number of deletions is on a par with the
 * number of insertions.  See also bpo-17563 and bpo-33205.
 *
 * GROWTH_RATE was set to used*4 up to version 3.2.
 * GROWTH_RATE was set to used*2 in version 3.3.0
 * GROWTH_RATE was set to used*2 + capacity/2 in 3.4.0-3.6.0.
 */
#define GROWTH_RATE(d) ((d)->ma_used*3)

#define ENSURE_ALLOWS_DELETIONS(d) \
    if ((d)->ma_keys->dk_lookup == lookdict_unicode_nodummy) { \
        (d)->ma_keys->dk_lookup = lookdict_unicode; \
    }

/* This immutable, empty PyDictKeysObject is used for PyDict_Clear()
 * (which cannot fail and thus can do no allocation).
 */
static PyDictKeysObject empty_keys_struct = {
        1, /* dk_refcnt */
        1, /* dk_size */
        lookdict_split, /* dk_lookup */
        0, /* dk_usable (immutable) */
        0, /* dk_nentries */
        {DKIX_EMPTY, DKIX_EMPTY, DKIX_EMPTY, DKIX_EMPTY,
         DKIX_EMPTY, DKIX_EMPTY, DKIX_EMPTY, DKIX_EMPTY}, /* dk_indices */
};

static PyObject *empty_values[1] = { NULL };

#define Py_EMPTY_KEYS &empty_keys_struct

/* Uncomment to check the dict content in Ci_CheckedDict_CheckConsistency() */
/* #define DEBUG_PYDICT */

#ifdef DEBUG_PYDICT
#  define ASSERT_CONSISTENT(op) assert(Ci_CheckedDict_CheckConsistency((PyObject *)(op), 1))
#else
#  define ASSERT_CONSISTENT(op) assert(Ci_CheckedDict_CheckConsistency((PyObject *)(op), 0))
#endif

int
Ci_CheckedDict_CheckConsistency(PyObject *op, int check_content)
{
#define CHECK(expr) \
    do { if (!(expr)) { _PyObject_ASSERT_FAILED_MSG(op, Py_STRINGIFY(expr)); } } while (0)

    assert(op != NULL);
    CHECK(Ci_CheckedDict_Check(op));
    PyDictObject *mp = (PyDictObject *)op;

    PyDictKeysObject *keys = mp->ma_keys;
    int splitted = _PyDict_HasSplitTable(mp);
    Py_ssize_t usable = USABLE_FRACTION(keys->dk_size);

    CHECK(0 <= mp->ma_used && mp->ma_used <= usable);
    CHECK(IS_POWER_OF_2(keys->dk_size));
    CHECK(0 <= keys->dk_usable && keys->dk_usable <= usable);
    CHECK(0 <= keys->dk_nentries && keys->dk_nentries <= usable);
    CHECK(keys->dk_usable + keys->dk_nentries <= usable);

    if (!splitted) {
        /* combined table */
        CHECK(keys->dk_refcnt == 1);
    }

    if (check_content) {
        PyDictKeyEntry *entries = DK_ENTRIES(keys);
        Py_ssize_t i;

        for (i=0; i < keys->dk_size; i++) {
            Py_ssize_t ix = dictkeys_get_index(keys, i);
            CHECK(DKIX_DUMMY <= ix && ix <= usable);
        }

        for (i=0; i < usable; i++) {
            PyDictKeyEntry *entry = &entries[i];
            PyObject *key = entry->me_key;

            if (key != NULL) {
                if (PyUnicode_CheckExact(key)) {
                    Py_hash_t hash = ((PyASCIIObject *)key)->hash;
                    CHECK(hash != -1);
                    CHECK(entry->me_hash == hash);
                }
                else {
                    /* test_dict fails if PyObject_Hash() is called again */
                    CHECK(entry->me_hash != -1);
                }
                if (!splitted) {
                    CHECK(entry->me_value != NULL);
                }
            }

            if (splitted) {
                CHECK(entry->me_value == NULL);
            }
        }

        if (splitted) {
            /* splitted table */
            for (i=0; i < mp->ma_used; i++) {
                CHECK(mp->ma_values[i] != NULL);
            }
        }
    }
    return 1;

#undef CHECK
}

static PyDictKeysObject*
new_keys_object(Py_ssize_t size)
{
    PyDictKeysObject *dk;
    Py_ssize_t es, usable;

    assert(size >= PyDict_MINSIZE);
    assert(IS_POWER_OF_2(size));

    usable = USABLE_FRACTION(size);
    if (size <= 0xff) {
        es = 1;
    }
    else if (size <= 0xffff) {
        es = 2;
    }
#if SIZEOF_VOID_P > 4
    else if (size <= 0xffffffff) {
        es = 4;
    }
#endif
    else {
        es = sizeof(Py_ssize_t);
    }

    struct _Py_dict_state *state = get_dict_state();
#ifdef Py_DEBUG
    // new_keys_object() must not be called after _PyDict_Fini()
    assert(state->keys_numfree != -1);
#endif
    if (size == PyDict_MINSIZE && state->keys_numfree > 0) {
        dk = state->keys_free_list[--state->keys_numfree];
    }
    else
    {
        dk = PyObject_Malloc(sizeof(PyDictKeysObject)
                             + es * size
                             + sizeof(PyDictKeyEntry) * usable);
        if (dk == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
    }
#ifdef Py_REF_DEBUG
    _Py_RefTotal++;
#endif
    dk->dk_refcnt = 1;
    dk->dk_size = size;
    dk->dk_usable = usable;
    dk->dk_lookup = lookdict_unicode_nodummy;
    dk->dk_nentries = 0;
    memset(&dk->dk_indices[0], 0xff, es * size);
    memset(DK_ENTRIES(dk), 0, sizeof(PyDictKeyEntry) * usable);
    return dk;
}


static void
free_keys_object(PyDictKeysObject *keys)
{
    PyDictKeyEntry *entries = DK_ENTRIES(keys);
    Py_ssize_t i, n;
    for (i = 0, n = keys->dk_nentries; i < n; i++) {
        Py_XDECREF(entries[i].me_key);
        Py_XDECREF(entries[i].me_value);
    }
    struct _Py_dict_state *state = get_dict_state();
#ifdef Py_DEBUG
    // free_keys_object() must not be called after _PyDict_Fini()
    assert(state->keys_numfree != -1);
#endif
    if (keys->dk_size == PyDict_MINSIZE && state->keys_numfree < PyDict_MAXFREELIST) {
        state->keys_free_list[state->keys_numfree++] = keys;
        return;
    }
    PyObject_Free(keys);
}

static PyDictKeysObject *
clone_combined_dict_keys(PyDictObject *orig)
{
    assert(Ci_CheckedDict_Check((PyObject *)orig));
    assert(Py_TYPE(orig)->tp_iter == (getiterfunc)dict_iter);
    assert(orig->ma_values == NULL);
    assert(orig->ma_keys->dk_refcnt == 1);

    Py_ssize_t keys_size = Ci_CheckedDict_KeysSize(orig->ma_keys);
    PyDictKeysObject *keys = PyObject_Malloc(keys_size);
    if (keys == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    memcpy(keys, orig->ma_keys, keys_size);

    /* After copying key/value pairs, we need to incref all
       keys and values and they are about to be co-owned by a
       new dict object. */
    PyDictKeyEntry *ep0 = DK_ENTRIES(keys);
    Py_ssize_t n = keys->dk_nentries;
    for (Py_ssize_t i = 0; i < n; i++) {
        PyDictKeyEntry *entry = &ep0[i];
        PyObject *value = entry->me_value;
        if (value != NULL) {
            Py_INCREF(value);
            Py_INCREF(entry->me_key);
        }
    }

    /* Since we copied the keys table we now have an extra reference
       in the system.  Manually call increment _Py_RefTotal to signal that
       we have it now; calling dictkeys_incref would be an error as
       keys->dk_refcnt is already set to 1 (after memcpy). */
#ifdef Py_REF_DEBUG
    _Py_RefTotal++;
#endif
    return keys;
}

/* Search index of hash table from offset of entry table */
static Py_ssize_t
lookdict_index(PyDictKeysObject *k, Py_hash_t hash, Py_ssize_t index)
{
    size_t mask = DK_MASK(k);
    size_t perturb = (size_t)hash;
    size_t i = (size_t)hash & mask;

    for (;;) {
        Py_ssize_t ix = dictkeys_get_index(k, i);
        if (ix == index) {
            return i;
        }
        if (ix == DKIX_EMPTY) {
            return DKIX_EMPTY;
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i*5 + perturb + 1);
    }
    Py_UNREACHABLE();
}

#define free_values(values) PyMem_Free(values)

/*
The basic lookup function used by all operations.
This is based on Algorithm D from Knuth Vol. 3, Sec. 6.4.
Open addressing is preferred over chaining since the link overhead for
chaining would be substantial (100% with typical malloc overhead).

The initial probe index is computed as hash mod the table size. Subsequent
probe indices are computed as explained earlier.

All arithmetic on hash should ignore overflow.

The details in this version are due to Tim Peters, building on many past
contributions by Reimer Behrends, Jyrki Alakuijala, Vladimir Marangozov and
Christian Tismer.

lookdict() is general-purpose, and may return DKIX_ERROR if (and only if) a
comparison raises an exception.
lookdict_unicode() below is specialized to string keys, comparison of which can
never raise an exception; that function can never return DKIX_ERROR when key
is string.  Otherwise, it falls back to lookdict().
lookdict_unicode_nodummy is further specialized for string keys that cannot be
the <dummy> value.
For both, when the key isn't found a DKIX_EMPTY is returned.
*/
static Py_ssize_t _Py_HOT_FUNCTION
lookdict(PyDictObject *mp, PyObject *key,
         Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports)
{
    size_t i, mask, perturb;
    PyDictKeysObject *dk;
    PyDictKeyEntry *ep0;

top:
    dk = mp->ma_keys;
    ep0 = DK_ENTRIES(dk);
    mask = DK_MASK(dk);
    perturb = hash;
    i = (size_t)hash & mask;

    for (;;) {
        Py_ssize_t ix = dictkeys_get_index(dk, i);
        if (ix == DKIX_EMPTY) {
            *value_addr = NULL;
            return ix;
        }
        if (ix >= 0) {
            PyDictKeyEntry *ep = &ep0[ix];
            assert(ep->me_key != NULL);
            if (ep->me_key == key) {
                *value_addr = ep->me_value;
                return ix;
            }
            if (ep->me_hash == hash) {
                PyObject *startkey = ep->me_key;
                Py_INCREF(startkey);
                int cmp = PyObject_RichCompareBool(startkey, key, Py_EQ);
                Py_DECREF(startkey);
                if (cmp < 0) {
                    *value_addr = NULL;
                    return DKIX_ERROR;
                }
                if (dk == mp->ma_keys && ep->me_key == startkey) {
                    if (cmp > 0) {
                        *value_addr = ep->me_value;
                        return ix;
                    }
                }
                else {
                    /* The dict was mutated, restart */
                    goto top;
                }
            }
        }
        perturb >>= PERTURB_SHIFT;
        i = (i*5 + perturb + 1) & mask;
    }
    Py_UNREACHABLE();
}

/* Specialized version for string-only keys */
static Py_ssize_t _Py_HOT_FUNCTION
lookdict_unicode(PyDictObject *mp, PyObject *key,
                 Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports)
{
    assert(mp->ma_values == NULL);
    /* Make sure this function doesn't have to handle non-unicode keys,
       including subclasses of str; e.g., one reason to subclass
       unicodes is to override __eq__, and for speed we don't cater to
       that here. */
    if (!PyUnicode_CheckExact(key)) {
        return lookdict(mp, key, hash, value_addr, resolve_lazy_imports);
    }

    PyDictKeyEntry *ep0 = DK_ENTRIES(mp->ma_keys);
    size_t mask = DK_MASK(mp->ma_keys);
    size_t perturb = (size_t)hash;
    size_t i = (size_t)hash & mask;

    for (;;) {
        Py_ssize_t ix = dictkeys_get_index(mp->ma_keys, i);
        if (ix == DKIX_EMPTY) {
            *value_addr = NULL;
            return DKIX_EMPTY;
        }
        if (ix >= 0) {
            PyDictKeyEntry *ep = &ep0[ix];
            assert(ep->me_key != NULL);
            assert(PyUnicode_CheckExact(ep->me_key));
            if (ep->me_key == key ||
                    (ep->me_hash == hash && unicode_eq(ep->me_key, key))) {
                *value_addr = ep->me_value;
                return ix;
            }
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i*5 + perturb + 1);
    }
    Py_UNREACHABLE();
}

/* Faster version of lookdict_unicode when it is known that no <dummy> keys
 * will be present. */
static Py_ssize_t _Py_HOT_FUNCTION
lookdict_unicode_nodummy(PyDictObject *mp, PyObject *key,
                         Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports)
{
    assert(mp->ma_values == NULL);
    /* Make sure this function doesn't have to handle non-unicode keys,
       including subclasses of str; e.g., one reason to subclass
       unicodes is to override __eq__, and for speed we don't cater to
       that here. */
    if (!PyUnicode_CheckExact(key)) {
        return lookdict(mp, key, hash, value_addr, resolve_lazy_imports);
    }

    PyDictKeyEntry *ep0 = DK_ENTRIES(mp->ma_keys);
    size_t mask = DK_MASK(mp->ma_keys);
    size_t perturb = (size_t)hash;
    size_t i = (size_t)hash & mask;

    for (;;) {
        Py_ssize_t ix = dictkeys_get_index(mp->ma_keys, i);
        assert (ix != DKIX_DUMMY);
        if (ix == DKIX_EMPTY) {
            *value_addr = NULL;
            return DKIX_EMPTY;
        }
        PyDictKeyEntry *ep = &ep0[ix];
        assert(ep->me_key != NULL);
        assert(PyUnicode_CheckExact(ep->me_key));
        if (ep->me_key == key ||
            (ep->me_hash == hash && unicode_eq(ep->me_key, key))) {
            *value_addr = ep->me_value;
            return ix;
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i*5 + perturb + 1);
    }
    Py_UNREACHABLE();
}

/* Version of lookdict for split tables.
 * All split tables and only split tables use this lookup function.
 * Split tables only contain unicode keys and no dummy keys,
 * so algorithm is the same as lookdict_unicode_nodummy.
 */
static Py_ssize_t _Py_HOT_FUNCTION
lookdict_split(PyDictObject *mp, PyObject *key,
               Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports)
{
    /* mp must split table */
    assert(mp->ma_values != NULL);
    if (!PyUnicode_CheckExact(key)) {
        Py_ssize_t ix = lookdict(mp, key, hash, value_addr, resolve_lazy_imports);
        if (ix >= 0) {
            *value_addr = mp->ma_values[ix];
        }
        return ix;
    }

    PyDictKeyEntry *ep0 = DK_ENTRIES(mp->ma_keys);
    size_t mask = DK_MASK(mp->ma_keys);
    size_t perturb = (size_t)hash;
    size_t i = (size_t)hash & mask;

    for (;;) {
        Py_ssize_t ix = dictkeys_get_index(mp->ma_keys, i);
        assert (ix != DKIX_DUMMY);
        if (ix == DKIX_EMPTY) {
            *value_addr = NULL;
            return DKIX_EMPTY;
        }
        PyDictKeyEntry *ep = &ep0[ix];
        assert(ep->me_key != NULL);
        assert(PyUnicode_CheckExact(ep->me_key));
        if (ep->me_key == key ||
            (ep->me_hash == hash && unicode_eq(ep->me_key, key))) {
            *value_addr = mp->ma_values[ix];
            return ix;
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i*5 + perturb + 1);
    }
    Py_UNREACHABLE();
}

#define MAINTAIN_TRACKING(mp, key, value) \
    do { \
        if (!_PyObject_GC_IS_TRACKED(mp)) { \
            if (_PyObject_GC_MAY_BE_TRACKED(key) || \
                _PyObject_GC_MAY_BE_TRACKED(value)) { \
                _PyObject_GC_TRACK(mp); \
            } \
        } \
    } while(0)

/* Internal function to find slot for an item from its hash
   when it is known that the key is not present in the dict.

   The dict must be combined. */
static Py_ssize_t
find_empty_slot(PyDictKeysObject *keys, Py_hash_t hash)
{
    assert(keys != NULL);

    const size_t mask = DK_MASK(keys);
    size_t i = hash & mask;
    Py_ssize_t ix = dictkeys_get_index(keys, i);
    for (size_t perturb = hash; ix >= 0;) {
        perturb >>= PERTURB_SHIFT;
        i = (i*5 + perturb + 1) & mask;
        ix = dictkeys_get_index(keys, i);
    }
    return i;
}

static int
insertion_resize(PyDictObject *mp)
{
    return dictresize(mp, calculate_keysize(GROWTH_RATE(mp)));
}

/*
Internal routine to insert a new item into the table.
Used both by the internal resize routine and by the public insert routine.
Returns -1 if an error occurred, or 0 on success.
*/
static int
insertdict(PyDictObject *mp, PyObject *key, Py_hash_t hash, PyObject *value)
{
    PyObject *old_value;
    PyDictKeyEntry *ep;

    Py_INCREF(key);
    Py_INCREF(value);
    if (mp->ma_values != NULL && !PyUnicode_CheckExact(key)) {
        if (insertion_resize(mp) < 0)
            goto Fail;
    }

    Py_ssize_t ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 0);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        goto Fail;

    MAINTAIN_TRACKING(mp, key, value);

    /* When insertion order is different from shared key, we can't share
     * the key anymore.  Convert this instance to combine table.
     */
    if (_PyDict_HasSplitTable(mp) &&
        ((ix >= 0 && old_value == NULL && mp->ma_used != ix) ||
         (ix == DKIX_EMPTY && mp->ma_used != mp->ma_keys->dk_nentries))) {
        if (insertion_resize(mp) < 0)
            goto Fail;
        ix = DKIX_EMPTY;
    }

    if (ix == DKIX_EMPTY) {
        uint64_t new_version = _PyDict_NotifyEvent(
            PyDict_EVENT_ADDED, mp, key, value);
        /* Insert into new slot. */
        assert(old_value == NULL);
        if (mp->ma_keys->dk_usable <= 0) {
            /* Need to resize. */
            if (insertion_resize(mp) < 0)
                goto Fail;
        }
        if (!PyUnicode_CheckExact(key) && mp->ma_keys->dk_lookup != lookdict) {
            mp->ma_keys->dk_lookup = lookdict;
        }
        Py_ssize_t hashpos = find_empty_slot(mp->ma_keys, hash);
        ep = &DK_ENTRIES(mp->ma_keys)[mp->ma_keys->dk_nentries];
        dictkeys_set_index(mp->ma_keys, hashpos, mp->ma_keys->dk_nentries);
        ep->me_key = key;
        ep->me_hash = hash;
        if (mp->ma_values) {
            assert (mp->ma_values[mp->ma_keys->dk_nentries] == NULL);
            mp->ma_values[mp->ma_keys->dk_nentries] = value;
        }
        else {
            ep->me_value = value;
        }
        mp->ma_used++;
        mp->ma_version_tag = new_version;
        mp->ma_keys->dk_usable--;
        mp->ma_keys->dk_nentries++;
        assert(mp->ma_keys->dk_usable >= 0);
        ASSERT_CONSISTENT(mp);
        return 0;
    }

    if (old_value != value) {
        uint64_t new_version = _PyDict_NotifyEvent(
            PyDict_EVENT_MODIFIED, mp, key, value);
        if (_PyDict_HasSplitTable(mp)) {
            mp->ma_values[ix] = value;
            if (old_value == NULL) {
                /* pending state */
                assert(ix == mp->ma_used);
                mp->ma_used++;
            }
        }
        else {
            assert(old_value != NULL);
            DK_ENTRIES(mp->ma_keys)[ix].me_value = value;
        }
        mp->ma_version_tag = new_version;
    }
    Py_XDECREF(old_value); /* which **CAN** re-enter (see issue #22653) */
    ASSERT_CONSISTENT(mp);
    Py_DECREF(key);
    return 0;

Fail:
    Py_DECREF(value);
    Py_DECREF(key);
    return -1;
}

// Same to insertdict but specialized for ma_keys = Py_EMPTY_KEYS.
static int
insert_to_emptydict(PyDictObject *mp, PyObject *key, Py_hash_t hash,
                    PyObject *value)
{
    assert(mp->ma_keys == Py_EMPTY_KEYS);

    uint64_t new_version = _PyDict_NotifyEvent(
        PyDict_EVENT_ADDED, mp, key, value);

    PyDictKeysObject *newkeys = new_keys_object(PyDict_MINSIZE);
    if (newkeys == NULL) {
        return -1;
    }
    dictkeys_decref(Py_EMPTY_KEYS);
    mp->ma_keys = newkeys;
    mp->ma_values = NULL;

    if (!PyUnicode_CheckExact(key)) {
        mp->ma_keys->dk_lookup = lookdict;
    }

    Py_INCREF(key);
    Py_INCREF(value);
    MAINTAIN_TRACKING(mp, key, value);

    size_t hashpos = (size_t)hash & (PyDict_MINSIZE-1);
    PyDictKeyEntry *ep = DK_ENTRIES(mp->ma_keys);
    dictkeys_set_index(mp->ma_keys, hashpos, 0);
    ep->me_key = key;
    ep->me_hash = hash;
    ep->me_value = value;
    mp->ma_used++;
    mp->ma_version_tag = new_version;
    mp->ma_keys->dk_usable--;
    mp->ma_keys->dk_nentries++;
    return 0;
}

/*
Internal routine used by dictresize() to build a hashtable of entries.
*/
static void
build_indices(PyDictKeysObject *keys, PyDictKeyEntry *ep, Py_ssize_t n)
{
    size_t mask = (size_t)DK_SIZE(keys) - 1;
    for (Py_ssize_t ix = 0; ix != n; ix++, ep++) {
        Py_hash_t hash = ep->me_hash;
        size_t i = hash & mask;
        for (size_t perturb = hash; dictkeys_get_index(keys, i) != DKIX_EMPTY;) {
            perturb >>= PERTURB_SHIFT;
            i = mask & (i*5 + perturb + 1);
        }
        dictkeys_set_index(keys, i, ix);
    }
}

/*
Restructure the table by allocating a new table and reinserting all
items again.  When entries have been deleted, the new table may
actually be smaller than the old one.
If a table is split (its keys and hashes are shared, its values are not),
then the values are temporarily copied into the table, it is resized as
a combined table, then the me_value slots in the old table are NULLed out.
After resizing a table is always combined,
but can be resplit by _PyDict_MakeKeysShared().
*/
static int
dictresize(PyDictObject *mp, Py_ssize_t newsize)
{
    Py_ssize_t numentries;
    PyDictKeysObject *oldkeys;
    PyObject **oldvalues;
    PyDictKeyEntry *oldentries, *newentries;

    if (newsize <= 0) {
        PyErr_NoMemory();
        return -1;
    }
    assert(IS_POWER_OF_2(newsize));
    assert(newsize >= PyDict_MINSIZE);

    oldkeys = mp->ma_keys;

    /* NOTE: Current odict checks mp->ma_keys to detect resize happen.
     * So we can't reuse oldkeys even if oldkeys->dk_size == newsize.
     * TODO: Try reusing oldkeys when reimplement odict.
     */

    /* Allocate a new table. */
    mp->ma_keys = new_keys_object(newsize);
    if (mp->ma_keys == NULL) {
        mp->ma_keys = oldkeys;
        return -1;
    }
    // New table must be large enough.
    assert(mp->ma_keys->dk_usable >= mp->ma_used);
    if (oldkeys->dk_lookup == lookdict)
        mp->ma_keys->dk_lookup = oldkeys->dk_lookup;

    numentries = mp->ma_used;
    oldentries = DK_ENTRIES(oldkeys);
    newentries = DK_ENTRIES(mp->ma_keys);
    oldvalues = mp->ma_values;
    if (oldvalues != NULL) {
        /* Convert split table into new combined table.
         * We must incref keys; we can transfer values.
         * Note that values of split table is always dense.
         */
        for (Py_ssize_t i = 0; i < numentries; i++) {
            assert(oldvalues[i] != NULL);
            PyDictKeyEntry *ep = &oldentries[i];
            PyObject *key = ep->me_key;
            Py_INCREF(key);
            newentries[i].me_key = key;
            newentries[i].me_hash = ep->me_hash;
            newentries[i].me_value = oldvalues[i];
        }

        dictkeys_decref(oldkeys);
        mp->ma_values = NULL;
        if (oldvalues != empty_values) {
            free_values(oldvalues);
        }
    }
    else {  // combined table.
        if (oldkeys->dk_nentries == numentries) {
            memcpy(newentries, oldentries, numentries * sizeof(PyDictKeyEntry));
        }
        else {
            PyDictKeyEntry *ep = oldentries;
            for (Py_ssize_t i = 0; i < numentries; i++) {
                while (ep->me_value == NULL)
                    ep++;
                newentries[i] = *ep++;
            }
        }

        assert(oldkeys->dk_lookup != lookdict_split);
        assert(oldkeys->dk_refcnt == 1);
#ifdef Py_REF_DEBUG
        _Py_RefTotal--;
#endif
        struct _Py_dict_state *state = get_dict_state();
#ifdef Py_DEBUG
        // dictresize() must not be called after _PyDict_Fini()
        assert(state->keys_numfree != -1);
#endif
        if (oldkeys->dk_size == PyDict_MINSIZE &&
            state->keys_numfree < PyDict_MAXFREELIST)
        {
            state->keys_free_list[state->keys_numfree++] = oldkeys;
        }
        else {
            PyObject_Free(oldkeys);
        }
    }

    build_indices(mp->ma_keys, newentries, numentries);
    mp->ma_keys->dk_usable -= numentries;
    mp->ma_keys->dk_nentries = numentries;
    return 0;
}

inline int Ci_CheckedDict_SetItemInternal(PyObject *op, PyObject *key, PyObject *value)
{
    assert(key);
    assert(value);
    PyDictObject *mp;
    Py_hash_t hash;
    mp = (PyDictObject *)op;
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1)
    {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return -1;
    }

    if (mp->ma_keys == Py_EMPTY_KEYS) {
        return insert_to_emptydict(mp, key, hash, value);
    }
    /* insertdict() handles any resizing that might be necessary */
    return insertdict(mp, key, hash, value);
}

void
Ci_CheckedDict_Clear(PyObject *op)
{
    PyDictObject *mp;
    PyDictKeysObject *oldkeys;
    PyObject **oldvalues;
    Py_ssize_t i, n;

    if (!Ci_CheckedDict_Check(op))
        return;
    mp = ((PyDictObject *)op);
    oldkeys = mp->ma_keys;
    oldvalues = mp->ma_values;
    if (oldvalues == empty_values)
        return;
    /* Empty the dict... */
    uint64_t new_version = _PyDict_NotifyEvent(
        PyDict_EVENT_CLEARED, mp, NULL, NULL);
    dictkeys_incref(Py_EMPTY_KEYS);
    mp->ma_keys = Py_EMPTY_KEYS;
    mp->ma_values = empty_values;
    mp->ma_used = 0;
    mp->ma_version_tag = new_version;
    /* ...then clear the keys and values */
    if (oldvalues != NULL) {
        n = oldkeys->dk_nentries;
        for (i = 0; i < n; i++)
            Py_CLEAR(oldvalues[i]);
        free_values(oldvalues);
        dictkeys_decref(oldkeys);
    }
    else {
       assert(oldkeys->dk_refcnt == 1);
       dictkeys_decref(oldkeys);
    }
    ASSERT_CONSISTENT(mp);
}

/* Internal version of PyDict_Next that returns a hash value in addition
 * to the key and value.
 * Return 1 on success, return 0 when the reached the end of the dictionary
 * (or if op is not a dictionary)
 */
int
Ci_CheckedDict_Next(PyObject *op, Py_ssize_t *ppos, PyObject **pkey,
                    PyObject **pvalue, Py_hash_t *phash)
{
    Py_ssize_t i;
    PyDictObject *mp;
    PyDictKeysObject *dk;
    PyDictKeyEntry *ep;
    PyObject *value;

    if (!Ci_CheckedDict_Check(op))
        return 0;
    mp = (PyDictObject *)op;
    dk = mp->ma_keys;

    i = *ppos;
    if (mp->ma_values) {
        if (i < 0 || i >= mp->ma_used)
            return 0;
        /* values of split table is always dense */
        ep = &DK_ENTRIES(dk)[i];
        value = mp->ma_values[i];
        assert(value != NULL);
    }
    else {
        Py_ssize_t n = dk->dk_nentries;
        if (i < 0 || i >= n)
            return 0;
        ep = &DK_ENTRIES(dk)[i];
        while (i < n && ep->me_value == NULL) {
            ep++;
            i++;
        }
        if (i >= n)
            return 0;
        value = ep->me_value;
    }
    *ppos = i+1;
    if (pkey)
        *pkey = ep->me_key;
    if (phash)
        *phash = ep->me_hash;
    if (pvalue)
        *pvalue = value;
    return 1;
}


/* Internal version of dict.pop(). */
static PyObject *
Ci_CheckedDict_Pop_KnownHash(PyObject *dict, PyObject *key, Py_hash_t hash, PyObject *deflt)
{
    Py_ssize_t ix, hashpos;
    PyObject *old_value, *old_key;
    PyDictKeyEntry *ep;
    PyDictObject *mp;

    assert(Ci_CheckedDict_Check(dict));
    mp = (PyDictObject *)dict;

    if (mp->ma_used == 0) {
        if (deflt) {
            Py_INCREF(deflt);
            return deflt;
        }
        _PyErr_SetKeyError(key);
        return NULL;
    }
    ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 1);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return NULL;
    if (ix == DKIX_EMPTY || old_value == NULL) {
        if (deflt) {
            Py_INCREF(deflt);
            return deflt;
        }
        _PyErr_SetKeyError(key);
        return NULL;
    }

    // Split table doesn't allow deletion.  Combine it.
    if (_PyDict_HasSplitTable(mp)) {
        if (dictresize(mp, DK_SIZE(mp->ma_keys))) {
            return NULL;
        }
        ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 1);
        assert(ix >= 0);
    }

    uint64_t new_version = _PyDict_NotifyEvent(
        PyDict_EVENT_DELETED, mp, key, NULL);

    hashpos = lookdict_index(mp->ma_keys, hash, ix);
    assert(hashpos >= 0);
    assert(old_value != NULL);
    mp->ma_used--;
    mp->ma_version_tag = new_version;
    dictkeys_set_index(mp->ma_keys, hashpos, DKIX_DUMMY);
    ep = &DK_ENTRIES(mp->ma_keys)[ix];
    ENSURE_ALLOWS_DELETIONS(mp);
    old_key = ep->me_key;
    ep->me_key = NULL;
    ep->me_value = NULL;
    Py_DECREF(old_key);

    ASSERT_CONSISTENT(mp);
    return old_value;
}

static PyObject *
Ci_CheckedDict_Pop(PyObject *dict, PyObject *key, PyObject *deflt)
{
    Py_hash_t hash;

    if (((PyDictObject *)dict)->ma_used == 0) {
        if (deflt) {
            Py_INCREF(deflt);
            return deflt;
        }
        _PyErr_SetKeyError(key);
        return NULL;
    }
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return NULL;
    }
    return Ci_CheckedDict_Pop_KnownHash(dict, key, hash, deflt);
}


/* Methods */

static void
dict_dealloc(PyDictObject *mp)
{
    assert(Py_REFCNT(mp) == 0);
    Py_SET_REFCNT(mp, 1);
    _PyDict_NotifyEvent(PyDict_EVENT_DEALLOCATED, mp, NULL, NULL);
    if (Py_REFCNT(mp) > 1) {
        Py_SET_REFCNT(mp, Py_REFCNT(mp) - 1);
        return;
    }
    Py_SET_REFCNT(mp, 0);
    PyObject **values = mp->ma_values;
    PyDictKeysObject *keys = mp->ma_keys;
    Py_ssize_t i, n;

    /* bpo-31095: UnTrack is needed before calling any callbacks */
    PyObject_GC_UnTrack(mp);
    Py_TRASHCAN_BEGIN(mp, dict_dealloc)
    if (values != NULL) {
        if (values != empty_values) {
            for (i = 0, n = mp->ma_keys->dk_nentries; i < n; i++) {
                Py_XDECREF(values[i]);
            }
            free_values(values);
        }
        dictkeys_decref(keys);
    }
    else if (keys != NULL) {
        assert(keys->dk_refcnt == 1);
        dictkeys_decref(keys);
    }
    struct _Py_dict_state *state = get_dict_state();
#ifdef Py_DEBUG
    // new_dict() must not be called after _PyDict_Fini()
    assert(state->numfree != -1);
#endif
    if (state->numfree < PyDict_MAXFREELIST && Py_IS_TYPE(mp, &PyDict_Type)) {
        state->free_list[state->numfree++] = mp;
    }
    else {
        Py_TYPE(mp)->tp_free((PyObject *)mp);
    }
    Py_TRASHCAN_END
}

static PyObject *
dict_repr(PyDictObject *mp)
{
    Py_ssize_t i;
    PyObject *key = NULL, *value = NULL;
    _PyUnicodeWriter writer;
    int first;

    i = Py_ReprEnter((PyObject *)mp);
    if (i != 0) {
        return i > 0 ? PyUnicode_FromString("{...}") : NULL;
    }

    if (mp->ma_used == 0) {
        Py_ReprLeave((PyObject *)mp);
        return PyUnicode_FromString("{}");
    }

    _PyUnicodeWriter_Init(&writer);
    writer.overallocate = 1;
    /* "{" + "1: 2" + ", 3: 4" * (len - 1) + "}" */
    writer.min_length = 1 + 4 + (2 + 4) * (mp->ma_used - 1) + 1;

    if (_PyUnicodeWriter_WriteChar(&writer, '{') < 0)
        goto error;

    /* Do repr() on each key+value pair, and insert ": " between them.
       Note that repr may mutate the dict. */
    i = 0;
    first = 1;
    while (Ci_CheckedDict_Next((PyObject *)mp, &i, &key, &value, NULL)) {
        PyObject *s;
        int res;

        /* Prevent repr from deleting key or value during key format. */
        Py_INCREF(key);
        Py_INCREF(value);

        if (!first) {
            if (_PyUnicodeWriter_WriteASCIIString(&writer, ", ", 2) < 0)
                goto error;
        }
        first = 0;

        s = PyObject_Repr(key);
        if (s == NULL)
            goto error;
        res = _PyUnicodeWriter_WriteStr(&writer, s);
        Py_DECREF(s);
        if (res < 0)
            goto error;

        if (_PyUnicodeWriter_WriteASCIIString(&writer, ": ", 2) < 0)
            goto error;

        s = PyObject_Repr(value);
        if (s == NULL)
            goto error;
        res = _PyUnicodeWriter_WriteStr(&writer, s);
        Py_DECREF(s);
        if (res < 0)
            goto error;

        Py_CLEAR(key);
        Py_CLEAR(value);
    }

    writer.overallocate = 0;
    if (_PyUnicodeWriter_WriteChar(&writer, '}') < 0)
        goto error;

    Py_ReprLeave((PyObject *)mp);

    return _PyUnicodeWriter_Finish(&writer);

error:
    Py_ReprLeave((PyObject *)mp);
    _PyUnicodeWriter_Dealloc(&writer);
    Py_XDECREF(key);
    Py_XDECREF(value);
    return NULL;
}

static Py_ssize_t
dict_length(PyDictObject *mp)
{
    return mp->ma_used;
}

static PyObject *
dict_subscript(PyDictObject *mp, PyObject *key)
{
    Py_ssize_t ix;
    Py_hash_t hash;
    PyObject *value;

    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return NULL;
    }
    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 1);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return NULL;
    if (ix == DKIX_EMPTY || value == NULL) {
        if (!PyDict_CheckExact(mp)) {
            /* Look up __missing__ method if we're a subclass. */
            PyObject *missing, *res;
            _Py_IDENTIFIER(__missing__);
            missing = _PyObject_LookupSpecial((PyObject *)mp, &PyId___missing__);
            if (missing != NULL) {
                res = PyObject_CallOneArg(missing, key);
                Py_DECREF(missing);
                return res;
            }
            else if (PyErr_Occurred())
                return NULL;
        }
        _PyErr_SetKeyError(key);
        return NULL;
    }
    Py_INCREF(value);
    return value;
}

static int
dict_merge(PyObject *a, PyObject *b, int override)
{
    PyDictObject *mp, *other;
    Py_ssize_t i, n;
    PyDictKeyEntry *entry, *ep0;

    assert(0 <= override && override <= 2);

    /* We accept for the argument either a concrete dictionary object,
     * or an abstract "mapping" object.  For the former, we can do
     * things quite efficiently.  For the latter, we only require that
     * PyMapping_Keys() and PyObject_GetItem() be supported.
     */
    if (a == NULL || !Ci_CheckedDict_Check(a) || b == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    mp = (PyDictObject*)a;
    if (Ci_CheckedDict_Check(b) && (Py_TYPE(b)->tp_iter == (getiterfunc)dict_iter)) {
        other = (PyDictObject*)b;
        if (other == mp || other->ma_used == 0)
            /* a.update(a) or a.update({}); nothing to do */
            return 0;
        if (mp->ma_used == 0) {
            /* Since the target dict is empty, PyDict_GetItem()
             * always returns NULL.  Setting override to 1
             * skips the unnecessary test.
             */
            override = 1;
            PyDictKeysObject *okeys = other->ma_keys;

            // If other is clean, combined, and just allocated, just clone it.
            if (other->ma_values == NULL &&
                    other->ma_used == okeys->dk_nentries &&
                    (okeys->dk_size == PyDict_MINSIZE ||
                     USABLE_FRACTION(okeys->dk_size/2) < other->ma_used)) {
                uint64_t new_version = _PyDict_NotifyEvent(
                    PyDict_EVENT_CLONED, mp, b, NULL);
                PyDictKeysObject *keys = clone_combined_dict_keys(other);
                if (keys == NULL) {
                    return -1;
                }

                dictkeys_decref(mp->ma_keys);
                mp->ma_keys = keys;
                if (mp->ma_values != NULL) {
                    if (mp->ma_values != empty_values) {
                        free_values(mp->ma_values);
                    }
                    mp->ma_values = NULL;
                }

                mp->ma_used = other->ma_used;
                mp->ma_version_tag = new_version;
                ASSERT_CONSISTENT(mp);

                if (_PyObject_GC_IS_TRACKED(other) && !_PyObject_GC_IS_TRACKED(mp)) {
                    /* Maintain tracking. */
                    _PyObject_GC_TRACK(mp);
                }

                return 0;
            }
        }
        /* Do one big resize at the start, rather than
         * incrementally resizing as we insert new items.  Expect
         * that there will be no (or few) overlapping keys.
         */
        if (USABLE_FRACTION(mp->ma_keys->dk_size) < other->ma_used) {
            if (dictresize(mp, estimate_keysize(mp->ma_used + other->ma_used))) {
               return -1;
            }
        }
        ep0 = DK_ENTRIES(other->ma_keys);
        for (i = 0, n = other->ma_keys->dk_nentries; i < n; i++) {
            PyObject *key, *value;
            Py_hash_t hash;
            entry = &ep0[i];
            key = entry->me_key;
            hash = entry->me_hash;
            if (other->ma_values)
                value = other->ma_values[i];
            else
                value = entry->me_value;

            if (value != NULL) {
                int err = 0;
                Py_INCREF(key);
                Py_INCREF(value);
                if (override == 1)
                    err = insertdict(mp, key, hash, value);
                else {
                    err = _PyDict_Contains_KnownHash(a, key, hash);
                    if (err == 0) {
                        err = insertdict(mp, key, hash, value);
                    }
                    else if (err > 0) {
                        if (override != 0) {
                            _PyErr_SetKeyError(key);
                            Py_DECREF(value);
                            Py_DECREF(key);
                            return -1;
                        }
                        err = 0;
                    }
                }
                Py_DECREF(value);
                Py_DECREF(key);
                if (err != 0)
                    return -1;

                if (n != other->ma_keys->dk_nentries) {
                    PyErr_SetString(PyExc_RuntimeError,
                                    "dict mutated during update");
                    return -1;
                }
            }
        }
    }
    else {
        /* Do it the generic, slower way */
        PyObject *keys = PyMapping_Keys(b);
        PyObject *iter;
        PyObject *key, *value;
        int status;

        if (keys == NULL)
            /* Docstring says this is equivalent to E.keys() so
             * if E doesn't have a .keys() method we want
             * AttributeError to percolate up.  Might as well
             * do the same for any other error.
             */
            return -1;

        iter = PyObject_GetIter(keys);
        Py_DECREF(keys);
        if (iter == NULL)
            return -1;

        for (key = PyIter_Next(iter); key; key = PyIter_Next(iter)) {
            if (override != 1) {
                status = PyDict_Contains(a, key);
                if (status != 0) {
                    if (status > 0) {
                        if (override == 0) {
                            Py_DECREF(key);
                            continue;
                        }
                        _PyErr_SetKeyError(key);
                    }
                    Py_DECREF(key);
                    Py_DECREF(iter);
                    return -1;
                }
            }
            value = PyObject_GetItem(b, key);
            if (value == NULL) {
                Py_DECREF(iter);
                Py_DECREF(key);
                return -1;
            }
            status = Ci_Dict_SetItemInternal(a, key, value);
            Py_DECREF(key);
            Py_DECREF(value);
            if (status < 0) {
                Py_DECREF(iter);
                return -1;
            }
        }
        Py_DECREF(iter);
        if (PyErr_Occurred())
            /* Iterator completed, via error */
            return -1;
    }
    ASSERT_CONSISTENT(a);
    return 0;
}

/* Return 1 if dicts equal, 0 if not, -1 if error.
 * Gets out as soon as any difference is detected.
 * Uses only Py_EQ comparison.
 */
static int
dict_equal(PyDictObject *a, PyDictObject *b)
{
    Py_ssize_t i;

    if (a->ma_used != b->ma_used)
        /* can't be equal if # of entries differ */
        return 0;
    /* Same # of entries -- check all of 'em.  Exit early on any diff. */
    for (i = 0; i < a->ma_keys->dk_nentries; i++) {
        PyDictKeyEntry *ep = &DK_ENTRIES(a->ma_keys)[i];
        PyObject *aval;
        if (a->ma_values)
            aval = a->ma_values[i];
        else
            aval = ep->me_value;
        if (aval != NULL) {
            int cmp;
            PyObject *bval;
            PyObject *key = ep->me_key;
            /* temporarily bump aval's refcount to ensure it stays
               alive until we're done with it */
            Py_INCREF(aval);
            /* ditto for key */
            Py_INCREF(key);
            /* reuse the known hash value */
            b->ma_keys->dk_lookup(b, key, ep->me_hash, &bval, 0);
            if (bval == NULL) {
                Py_DECREF(key);
                Py_DECREF(aval);
                if (PyErr_Occurred())
                    return -1;
                return 0;
            }
            Py_INCREF(bval);
            cmp = PyObject_RichCompareBool(aval, bval, Py_EQ);
            Py_DECREF(key);
            Py_DECREF(aval);
            Py_DECREF(bval);
            if (cmp <= 0)  /* error or not equal */
                return cmp;
        }
    }
    return 1;
}

static PyObject *
dict___contains__(PyDictObject *self, PyObject *key)
{
    register PyDictObject *mp = self;
    Py_hash_t hash;
    Py_ssize_t ix;
    PyObject *value;

    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return NULL;
    }
    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 0);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return NULL;
    if (ix == DKIX_EMPTY || value == NULL)
        Py_RETURN_FALSE;
    Py_RETURN_TRUE;
}

static PyObject *
dict_get_impl(PyDictObject *self, PyObject *key, PyObject *default_value)
{
    PyObject *val = NULL;
    Py_hash_t hash;
    Py_ssize_t ix;

    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return NULL;
    }
    ix = self->ma_keys->dk_lookup(self, key, hash, &val, 1);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return NULL;
    if (ix == DKIX_EMPTY || val == NULL) {
        val = default_value;
    }
    Py_INCREF(val);
    return val;
}

PyObject *
Ci_CheckedDict_SetDefault(PyObject *d, PyObject *key, PyObject *defaultobj)
{
    PyDictObject *mp = (PyDictObject *)d;
    PyObject *value;
    Py_hash_t hash;

    if (!Ci_CheckedDict_Check(d)) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return NULL;
    }
    if (mp->ma_keys == Py_EMPTY_KEYS) {
        if (insert_to_emptydict(mp, key, hash, defaultobj) < 0) {
            return NULL;
        }
        return defaultobj;
    }

    if (mp->ma_values != NULL && !PyUnicode_CheckExact(key)) {
        if (insertion_resize(mp) < 0)
            return NULL;
    }

    Py_ssize_t ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 1);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return NULL;

    if (_PyDict_HasSplitTable(mp) &&
        ((ix >= 0 && value == NULL && mp->ma_used != ix) ||
         (ix == DKIX_EMPTY && mp->ma_used != mp->ma_keys->dk_nentries))) {
        if (insertion_resize(mp) < 0) {
            return NULL;
        }
        ix = DKIX_EMPTY;
    }

    if (ix == DKIX_EMPTY) {
        PyDictKeyEntry *ep, *ep0;
        uint64_t new_version = _PyDict_NotifyEvent(
            PyDict_EVENT_ADDED, mp, key, defaultobj);
        value = defaultobj;
        if (mp->ma_keys->dk_usable <= 0) {
            if (insertion_resize(mp) < 0) {
                return NULL;
            }
        }
        if (!PyUnicode_CheckExact(key) && mp->ma_keys->dk_lookup != lookdict) {
            mp->ma_keys->dk_lookup = lookdict;
        }
        Py_ssize_t hashpos = find_empty_slot(mp->ma_keys, hash);
        ep0 = DK_ENTRIES(mp->ma_keys);
        ep = &ep0[mp->ma_keys->dk_nentries];
        dictkeys_set_index(mp->ma_keys, hashpos, mp->ma_keys->dk_nentries);
        Py_INCREF(key);
        Py_INCREF(value);
        MAINTAIN_TRACKING(mp, key, value);
        ep->me_key = key;
        ep->me_hash = hash;
        if (_PyDict_HasSplitTable(mp)) {
            assert(mp->ma_values[mp->ma_keys->dk_nentries] == NULL);
            mp->ma_values[mp->ma_keys->dk_nentries] = value;
        }
        else {
            ep->me_value = value;
        }
        mp->ma_used++;
        mp->ma_version_tag = new_version;
        mp->ma_keys->dk_usable--;
        mp->ma_keys->dk_nentries++;
        if (PyLazyImport_CheckExact(value)) {
            _PyDict_SetHasDeferredObjects((PyObject *)mp);
        }
        assert(mp->ma_keys->dk_usable >= 0);
    }
    else if (value == NULL) {
        uint64_t new_version = _PyDict_NotifyEvent(
            PyDict_EVENT_ADDED, mp, key, defaultobj);
        value = defaultobj;
        assert(_PyDict_HasSplitTable(mp));
        assert(ix == mp->ma_used);
        Py_INCREF(value);
        MAINTAIN_TRACKING(mp, key, value);
        mp->ma_values[ix] = value;
        mp->ma_used++;
        mp->ma_version_tag = new_version;
        if (PyLazyImport_CheckExact(value)) {
            _PyDict_SetHasDeferredObjects((PyObject *)mp);
        }
    }

    ASSERT_CONSISTENT(mp);
    return value;
}

static PyObject *
dict_setdefault_impl(PyDictObject *self, PyObject *key,
                     PyObject *default_value)
{
    PyObject *val;

    val = Ci_CheckedDict_SetDefault((PyObject *)self, key, default_value);
    Py_XINCREF(val);
    return val;
}

static PyObject *
dict_clear(PyDictObject *mp, PyObject *Py_UNUSED(ignored))
{
    Ci_CheckedDict_Clear((PyObject *)mp);
    Py_RETURN_NONE;
}

static PyObject *
dict_pop_impl(PyDictObject *self, PyObject *key, PyObject *default_value)
{
    return Ci_CheckedDict_Pop((PyObject*)self, key, default_value);
}

Py_ssize_t
Ci_CheckedDict_KeysSize(PyDictKeysObject *keys)
{
    return (sizeof(PyDictKeysObject)
            + DK_IXSIZE(keys) * DK_SIZE(keys)
            + USABLE_FRACTION(DK_SIZE(keys)) * sizeof(PyDictKeyEntry));
}

static PyObject *
dict_sizeof(PyDictObject *mp, PyObject *Py_UNUSED(ignored))
{
    return PyLong_FromSsize_t(_PyDict_SizeOf(mp));
}

PyDoc_STRVAR(getitem__doc__, "x.__getitem__(y) <==> x[y]");

PyDoc_STRVAR(sizeof__doc__,
"D.__sizeof__() -> size of D in memory, in bytes");

PyDoc_STRVAR(update__doc__,
"D.update([E, ]**F) -> None.  Update D from dict/iterable E and F.\n\
If E is present and has a .keys() method, then does:  for k in E: D[k] = E[k]\n\
If E is present and lacks a .keys() method, then does:  for k, v in E: D[k] = v\n\
In either case, this is followed by: for k in F:  D[k] = F[k]");

PyDoc_STRVAR(clear__doc__,
"D.clear() -> None.  Remove all items from D.");

PyDoc_STRVAR(copy__doc__,
"D.copy() -> a shallow copy of D");

/* Forward */
static PyObject *dictkeys_new(PyObject *, PyObject *);
static PyObject *dictitems_new(PyObject *, PyObject *);
static PyObject *dictvalues_new(PyObject *, PyObject *);

PyDoc_STRVAR(keys__doc__,
             "D.keys() -> a set-like object providing a view on D's keys");
PyDoc_STRVAR(items__doc__,
             "D.items() -> a set-like object providing a view on D's items");
PyDoc_STRVAR(values__doc__,
             "D.values() -> an object providing a view on D's values");

/* Return 1 if `key` is in dict `op`, 0 if not, and -1 on error. */
static int
CiCheckedDict_Contains(PyObject *op, PyObject *key)
{
    Py_hash_t hash;
    Py_ssize_t ix;
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;

    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return -1;
    }
    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 0);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return -1;
    return (ix != DKIX_EMPTY && value != NULL);
}

/* Hack to implement "key in dict" */
static PySequenceMethods dict_as_sequence = {
    0,                          /* sq_length */
    0,                          /* sq_concat */
    0,                          /* sq_repeat */
    0,                          /* sq_item */
    0,                          /* sq_slice */
    0,                          /* sq_ass_item */
    0,                          /* sq_ass_slice */
    CiCheckedDict_Contains,     /* sq_contains */
    0,                          /* sq_inplace_concat */
    0,                          /* sq_inplace_repeat */
};

static PyObject *
dict_popitem_impl(PyDictObject *self)
{
    Py_ssize_t i, j;
    PyDictKeyEntry *ep0, *ep;
    PyObject *res;

    /* Allocate the result tuple before checking the size.  Believe it
     * or not, this allocation could trigger a garbage collection which
     * could empty the dict, so if we checked the size first and that
     * happened, the result would be an infinite loop (searching for an
     * entry that no longer exists).  Note that the usual popitem()
     * idiom is "while d: k, v = d.popitem()". so needing to throw the
     * tuple away if the dict *is* empty isn't a significant
     * inefficiency -- possible, but unlikely in practice.
     */
    res = PyTuple_New(2);
    if (res == NULL)
        return NULL;
    if (self->ma_used == 0) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_KeyError, "popitem(): dictionary is empty");
        return NULL;
    }
    /* Convert split table to combined table */
    if (self->ma_keys->dk_lookup == lookdict_split) {
        if (dictresize(self, DK_SIZE(self->ma_keys))) {
            Py_DECREF(res);
            return NULL;
        }
    }
    ENSURE_ALLOWS_DELETIONS(self);

    /* Pop last item */
    ep0 = DK_ENTRIES(self->ma_keys);
    i = self->ma_keys->dk_nentries - 1;
    while (i >= 0 && ep0[i].me_value == NULL) {
        i--;
    }
    assert(i >= 0);

    ep = &ep0[i];
    PyObject *old_key = ep->me_key;
    uint64_t new_version = _PyDict_NotifyEvent(
        PyDict_EVENT_DELETED, self, old_key, NULL);
    j = lookdict_index(self->ma_keys, ep->me_hash, i);
    assert(j >= 0);
    assert(dictkeys_get_index(self->ma_keys, j) == i);
    dictkeys_set_index(self->ma_keys, j, DKIX_DUMMY);

    PyObject *old_value = ep->me_value;
    ep->me_key = NULL;
    ep->me_value = NULL;
    /* We can't dk_usable++ since there is DKIX_DUMMY in indices */
    self->ma_keys->dk_nentries = i;
    self->ma_used--;
    self->ma_version_tag = new_version;
    ASSERT_CONSISTENT(self);

    PyTuple_SET_ITEM(res, 0, old_key);
    PyTuple_SET_ITEM(res, 1, old_value);
    return res;
}

static int
dict_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyDictObject *mp = (PyDictObject *)op;
    PyDictKeysObject *keys = mp->ma_keys;
    PyDictKeyEntry *entries = DK_ENTRIES(keys);
    Py_ssize_t i, n = keys->dk_nentries;

    if (keys->dk_lookup == lookdict) {
        for (i = 0; i < n; i++) {
            if (entries[i].me_value != NULL) {
                Py_VISIT(entries[i].me_value);
                Py_VISIT(entries[i].me_key);
            }
        }
    }
    else {
        if (mp->ma_values != NULL) {
            for (i = 0; i < n; i++) {
                Py_VISIT(mp->ma_values[i]);
            }
        }
        else {
            for (i = 0; i < n; i++) {
                Py_VISIT(entries[i].me_value);
            }
        }
    }
    return 0;
}

static int
dict_tp_clear(PyObject *op)
{
    Ci_CheckedDict_Clear(op);
    return 0;
}

static PyObject *dictiter_new(PyDictObject *, PyTypeObject *);

static PyObject *
dict_iter(PyDictObject *dict)
{
    return dictiter_new(dict, &Ci_CheckedDictIterKey_Type);
}

PyDoc_STRVAR(dictionary_doc,
"dict() -> new empty dictionary\n"
"dict(mapping) -> new dictionary initialized from a mapping object's\n"
"    (key, value) pairs\n"
"dict(iterable) -> new dictionary initialized as if via:\n"
"    d = {}\n"
"    for k, v in iterable:\n"
"        d[k] = v\n"
"dict(**kwargs) -> new dictionary initialized with the name=value pairs\n"
"    in the keyword argument list.  For example:  dict(one=1, two=2)");

/* === End copied from dictobject.c === */

#define IS_CHECKED_DICT(x)                                                    \
    (_PyClassLoader_GetGenericTypeDef((PyObject *)x) == &Ci_CheckedDict_Type)

static inline int
Ci_Dict_CheckIncludingChecked(PyObject *x)
{
    return PyDict_Check(x) || IS_CHECKED_DICT(x);
}

int
Ci_CheckedDict_Check(PyObject *x)
{
    return IS_CHECKED_DICT(x);
}

int
Ci_CheckedDict_TypeCheck(PyTypeObject *type)
{
    return _PyClassLoader_GetGenericTypeDefFromType(type) == &Ci_CheckedDict_Type;
}

static void
chkdict_dealloc(PyDictObject *mp)
{
    /* Let the dict go onto the free list */
    Py_TYPE(mp) = &PyDict_Type;
    dict_dealloc(mp);
}

/* Consumes a reference to the keys object */
static PyObject *
chknew_dict(PyTypeObject *type, PyDictKeysObject *keys, PyObject **values)
{
    struct _Py_dict_state *state = get_dict_state();
    PyDictObject *mp;
    assert(keys != NULL);
    if (state->numfree) {
        mp = state->free_list[--state->numfree];
        assert(mp != NULL);
        Py_TYPE(mp) = type;
        _Py_NewReference((PyObject *)mp);
        /* Generic types are a heap allocated, so we need to bump the
         * ref count here. */
        Py_INCREF(type);
    } else {
        mp = PyObject_GC_New(PyDictObject, type);
        if (mp == NULL) {
            dictkeys_decref(keys);
            if (values != empty_values) {
                free_values(values);
            }
            return NULL;
        }
    }
    mp->ma_keys = keys;
    mp->ma_values = values;
    mp->ma_used = 0;
    mp->ma_version_tag = DICT_NEXT_VERSION();
    ASSERT_CONSISTENT(mp);
    return (PyObject *)mp;
}

PyObject *
Ci_CheckedDict_New(PyTypeObject *type)
{
    dictkeys_incref(Py_EMPTY_KEYS);
    return chknew_dict(type, Py_EMPTY_KEYS, empty_values);
}

static PyObject *
chkdict_alloc(PyTypeObject *type, Py_ssize_t nitems)
{
    return Ci_CheckedDict_New(type);
}

PyObject *
Ci_CheckedDict_NewPresized(PyTypeObject *type, Py_ssize_t minused)
{
    const Py_ssize_t max_presize = 128 * 1024;
    Py_ssize_t newsize;
    PyDictKeysObject *new_keys;

    if (minused <= USABLE_FRACTION(PyDict_MINSIZE)) {
        return Ci_CheckedDict_New(type);
    }
    /* There are no strict guarantee that returned dict can contain minused
     * items without resize.  So we create medium size dict instead of very
     * large dict or MemoryError.
     */
    if (minused > USABLE_FRACTION(max_presize)) {
        newsize = max_presize;
    } else {
        Py_ssize_t minsize = estimate_keysize(minused);
        newsize = PyDict_MINSIZE * 2;
        while (newsize < minsize) {
            newsize <<= 1;
        }
    }
    assert(IS_POWER_OF_2(newsize));

    new_keys = new_keys_object(newsize);
    if (new_keys == NULL)
        return NULL;
    return chknew_dict(type, new_keys, NULL);
}

static inline int
chkdict_checkkey(PyDictObject *d, PyObject *key)
{
    if (!_PyClassLoader_CheckParamType((PyObject *)d, key, 0)) {
        PyErr_Format(PyExc_TypeError,
                     "bad key '%s' for %s",
                     Py_TYPE(key)->tp_name,
                     Py_TYPE(d)->tp_name);
        return -1;
    }
    return 0;
}

static inline int
chkdict_checkval(PyDictObject *d, PyObject *value)
{
    if (!_PyClassLoader_CheckParamType((PyObject *)d, value, 1)) {
        PyErr_Format(PyExc_TypeError,
                     "bad value '%s' for %s",
                     Py_TYPE(value)->tp_name,
                     Py_TYPE(d)->tp_name);
        return -1;
    }
    return 0;
}

static int
chkdict_ass_sub(PyDictObject *mp, PyObject *key, PyObject *value)
{
    if (chkdict_checkkey(mp, key)) {
        return -1;
    }

    /* we can't use PyDict_SetItem/DelItem directly as they check
     * that we have a dictionary type */

    Py_ssize_t hash;
    assert(key);
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *)key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return -1;
    }

    if (value == NULL) {
        return _PyDict_DelItem_KnownHash((PyObject *)mp, key, hash);
    } else if (!chkdict_checkval(mp, value)) {
        if (mp->ma_keys == Py_EMPTY_KEYS) {
            return insert_to_emptydict(mp, key, hash, value);
        }
        /* insertdict() handles any resizing that might be necessary */
        return insertdict(mp, key, hash, value);
    } else {
        return -1;
    }
}

static int
chkdict_ass_sub_unchecked(PyDictObject *mp, PyObject *key, PyObject *value)
{
    /* we can't use PyDict_SetItem/DelItem directly as they check
     * that we have a dictionary type */

    Py_ssize_t hash;
    assert(key);
    assert(value);
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *)key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return -1;
    }

    if (key == NULL) {
        return _PyDict_DelItem_KnownHash((PyObject *)mp, key, hash);
    }

    if (mp->ma_keys == Py_EMPTY_KEYS) {
        return insert_to_emptydict(mp, key, hash, value);
    }
    /* insertdict() handles any resizing that might be necessary */
    return insertdict(mp, key, hash, value);
}

static PyObject *
chkdict_subscript(PyDictObject *mp, PyObject *key)
{
    if (chkdict_checkkey(mp, key)) {
        return NULL;
    }
    return dict_subscript(mp, key);
}

static PyMappingMethods chkdict_as_mapping = {
    (lenfunc)dict_length,           /*mp_length*/
    (binaryfunc)chkdict_subscript,  /*mp_subscript*/
    (objobjargproc)chkdict_ass_sub, /*mp_ass_subscript*/
};

static int
chkdict_merge(PyObject *a, PyObject *b)
{
    PyDictObject *mp, *other;
    Py_ssize_t i, n;
    PyDictKeyEntry *entry, *ep0;

    /* We accept for the argument either a concrete dictionary object,
     * or an abstract "mapping" object.  For the former, we can do
     * things quite efficiently.  For the latter, we only require that
     * PyMapping_Keys() and PyObject_GetItem() be supported.
     */
    mp = (PyDictObject *)a;
    if (Py_TYPE(b)->tp_iter == (getiterfunc)dict_iter) {
        other = (PyDictObject *)b;
        if (other == mp || other->ma_used == 0)
            /* a.update(a) or a.update({}); nothing to do */
            return 0;
        /* Do one big resize at the start, rather than
         * incrementally resizing as we insert new items.  Expect
         * that there will be no (or few) overlapping keys.
         */
        if (USABLE_FRACTION(mp->ma_keys->dk_size) < other->ma_used) {
            if (dictresize(mp, estimate_keysize(mp->ma_used + other->ma_used))) {
               return -1;
            }
        }
        ep0 = DK_ENTRIES(other->ma_keys);
        for (i = 0, n = other->ma_keys->dk_nentries; i < n; i++) {
            PyObject *key, *value;
            Py_hash_t hash;
            entry = &ep0[i];
            key = entry->me_key;
            hash = entry->me_hash;
            if (other->ma_values)
                value = other->ma_values[i];
            else
                value = entry->me_value;

            if (value != NULL) {
                int err = 0;
                if (chkdict_checkkey(mp, key) || chkdict_checkval(mp, value)) {
                    return -1;
                }

                Py_INCREF(key);
                Py_INCREF(value);
                err = insertdict(mp, key, hash, value);
                Py_DECREF(value);
                Py_DECREF(key);
                if (err != 0)
                    return -1;

                if (n != other->ma_keys->dk_nentries) {
                    PyErr_SetString(PyExc_RuntimeError,
                                    "dict mutated during update");
                    return -1;
                }
            }
        }
    } else {
        /* Do it the generic, slower way */
        PyObject *keys = PyMapping_Keys(b);
        PyObject *iter;
        PyObject *key, *value;
        int status;

        if (keys == NULL)
            /* Docstring says this is equivalent to E.keys() so
             * if E doesn't have a .keys() method we want
             * AttributeError to percolate up.  Might as well
             * do the same for any other error.
             */
            return -1;

        iter = PyObject_GetIter(keys);
        Py_DECREF(keys);
        if (iter == NULL)
            return -1;

        for (key = PyIter_Next(iter); key; key = PyIter_Next(iter)) {
            value = PyObject_GetItem(b, key);
            if (value == NULL) {
                Py_DECREF(iter);
                Py_DECREF(key);
                return -1;
            }
            if (chkdict_checkkey(mp, key) || chkdict_checkval(mp, value)) {
                status = -1;
            } else {
                status = insertdict(mp, key, PyObject_Hash(key), value);
            }

            Py_DECREF(key);
            Py_DECREF(value);
            if (status < 0) {
                Py_DECREF(iter);
                return -1;
            }
        }
        Py_DECREF(iter);
        if (PyErr_Occurred())
            /* Iterator completed, via error */
            return -1;
    }
    ASSERT_CONSISTENT(a);
    return 0;
}

int
chkdict_mergefromseq2(PyObject *d, PyObject *seq2)
{
    PyObject *it;   /* iter(seq2) */
    Py_ssize_t i;   /* index into seq2 of current element */
    PyObject *item; /* seq2[i] */
    PyObject *fast; /* item as a 2-tuple or 2-list */

    assert(d != NULL);
    assert(seq2 != NULL);

    it = PyObject_GetIter(seq2);
    if (it == NULL)
        return -1;

    for (i = 0;; ++i) {
        PyObject *key, *value;
        Py_ssize_t n;

        fast = NULL;
        item = PyIter_Next(it);
        if (item == NULL) {
            if (PyErr_Occurred())
                goto Fail;
            break;
        }

        /* Convert item to sequence, and verify length 2. */
        fast = PySequence_Fast(item, "");
        if (fast == NULL) {
            if (PyErr_ExceptionMatches(PyExc_TypeError))
                PyErr_Format(PyExc_TypeError,
                             "cannot convert dictionary update "
                             "sequence element #%zd to a sequence",
                             i);
            goto Fail;
        }
        n = PySequence_Fast_GET_SIZE(fast);
        if (n != 2) {
            PyErr_Format(PyExc_ValueError,
                         "dictionary update sequence element #%zd "
                         "has length %zd; 2 is required",
                         i,
                         n);
            goto Fail;
        }

        /* Update/merge with this (key, value) pair. */
        key = PySequence_Fast_GET_ITEM(fast, 0);
        value = PySequence_Fast_GET_ITEM(fast, 1);
        Py_INCREF(key);
        Py_INCREF(value);
        int status;
        if (chkdict_checkkey((PyDictObject *)d, key) ||
            chkdict_checkval((PyDictObject *)d, value)) {
            status = -1;
        } else {
            status =
                insertdict((PyDictObject *)d, key, PyObject_Hash(key), value);
        }
        if (status < 0) {
            Py_DECREF(key);
            Py_DECREF(value);
            goto Fail;
        }

        Py_DECREF(key);
        Py_DECREF(value);
        Py_DECREF(fast);
        Py_DECREF(item);
    }

    i = 0;
    ASSERT_CONSISTENT(d);
    goto Return;
Fail:
    Py_XDECREF(item);
    Py_XDECREF(fast);
    i = -1;
Return:
    Py_DECREF(it);
    return Py_SAFE_DOWNCAST(i, Py_ssize_t, int);
}

static int
chkdict_update_common_fast(PyObject *self, PyObject *arg, PyObject *kwds)
{
    int result = 0;

    if (arg != NULL) {
        if (Py_TYPE(arg) == Py_TYPE(self)) {
            /* no type checks necessary */
            result = dict_merge(self, arg, 1);
        } else if (Ci_Dict_CheckIncludingChecked(arg)) {
            result = chkdict_merge(self, arg);
        } else {
            _Py_IDENTIFIER(keys);
            PyObject *func;
            if (_PyObject_LookupAttrId(arg, &PyId_keys, &func) < 0) {
                result = -1;
            } else if (func != NULL) {
                Py_DECREF(func);
                result = chkdict_merge(self, arg);
            } else {
                result = chkdict_mergefromseq2(self, arg);
            }
        }
    }

    if (result == 0 && kwds != NULL) {
        if (PyArg_ValidateKeywordArguments(kwds))
            result = chkdict_merge(self, kwds);
        else
            result = -1;
    }
    return result;
}

static int
chkdict_update_common(PyObject *self,
                      PyObject *args,
                      PyObject *kwds,
                      const char *methname)
{
    PyObject *arg = NULL;

    if (!PyArg_UnpackTuple(args, methname, 0, 1, &arg)) {
        return -1;
    }
    return chkdict_update_common_fast(self, arg, kwds);
}

static PyObject *
chkdict_update(PyObject *self, PyObject *args, PyObject *kwds)
{
    if (chkdict_update_common(self, args, kwds, "update") != -1)
        Py_RETURN_NONE;
    return NULL;
}

static int
chkdict_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    return chkdict_update_common(self, args, kwds, Py_TYPE(self)->tp_name);
}

static PyObject *
chkdict_fromkeys(PyObject *type, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *d;
    PyObject *return_value = NULL;
    PyObject *iterable;
    PyObject *value = Py_None;
    PyObject *it, *key;

    if (!_PyArg_CheckPositional("fromkeys", nargs, 1, 2)) {
        goto exit;
    }
    iterable = args[0];
    if (nargs < 2) {
        goto skip_optional;
    }
    value = args[1];
skip_optional:
    d = _PyObject_CallNoArg(type);
    if (d == NULL)
        return NULL;

    if (!_PyClassLoader_CheckParamType(d, value, 1)) {
        PyErr_SetString(PyExc_TypeError, "bad value type");
        Py_DECREF(d);
        return NULL;
    }
    it = PyObject_GetIter(iterable);
    if (it == NULL) {
        Py_DECREF(d);
        return NULL;
    }

    while ((key = PyIter_Next(it)) != NULL) {
        if (!_PyClassLoader_CheckParamType(d, key, 0)) {
            PyErr_SetString(PyExc_TypeError, "bad key type");
            Py_DECREF(key);
            goto Fail;
        }

        int status =
            insertdict((PyDictObject *)d, key, PyObject_Hash(key), value);
        Py_DECREF(key);
        if (status < 0)
            goto Fail;
    }

    if (PyErr_Occurred())
        goto Fail;
    Py_DECREF(it);
    return d;

Fail:
    Py_DECREF(it);
    Py_DECREF(d);
    return NULL;

exit:
    return return_value;
}

static PyObject *
chkdict_copy(PyObject *mp, PyObject *Py_UNUSED(ignored))
{
    PyObject *copy = Py_TYPE(mp)->tp_alloc(Py_TYPE(mp), 0);
    if (copy == NULL)
        return NULL;
    if (dict_merge(copy, mp, 1) == 0)
        return copy;
    Py_DECREF(copy);
    return NULL;
}

static PyObject *chkdict_cls_getitem(_PyGenericTypeDef *type, PyObject *args) {
    PyObject *item = _PyClassLoader_GtdGetItem(type, args);
    if (item == NULL) {
        return NULL;
    }
    return item;
}

const Ci_Py_SigElement *const chkdict_sig[] = {
    &Ci_Py_Sig_T0, &Ci_Py_Sig_T1_Opt, NULL};

Ci_PyTypedMethodDef chkdict_get_def = {
    dict_get_impl, chkdict_sig, Ci_Py_SIG_TYPE_PARAM_OPT(1)};
Ci_PyTypedMethodDef chkdict_setdefault_def = {
    dict_setdefault_impl, chkdict_sig, Ci_Py_SIG_TYPE_PARAM_OPT(1)};

const Ci_Py_SigElement *const getitem_sig[] = {&Ci_Py_Sig_T0, NULL};
Ci_PyTypedMethodDef chkdict_getitem_def = {
    dict_subscript, getitem_sig, Ci_Py_SIG_TYPE_PARAM_OPT(1)};

const Ci_Py_SigElement *const setitem_sig[] = {&Ci_Py_Sig_T0, &Ci_Py_Sig_T1, NULL};
Ci_PyTypedMethodDef chkdict_setitem_def = {
    chkdict_ass_sub_unchecked, setitem_sig, Ci_Py_SIG_ERROR};

static PyMethodDef chkmapp_methods[] = {
    DICT___CONTAINS___METHODDEF
    {"__getitem__",
        (PyCFunction)&chkdict_getitem_def,
        Ci_METH_TYPED | METH_COEXIST,
        getitem__doc__},
    {"__setitem__",
        (PyCFunction)&chkdict_setitem_def,
        Ci_METH_TYPED | METH_COEXIST,
        "Set self[key] to value."},
    {"__sizeof__",
     (PyCFunction)(void (*)(void))dict_sizeof,
     METH_NOARGS,
     sizeof__doc__},
    {"get", (PyCFunction)&chkdict_get_def, Ci_METH_TYPED, dict_get__doc__},
    {"setdefault",
     (PyCFunction)&chkdict_setdefault_def,
     Ci_METH_TYPED,
     dict_setdefault__doc__},
    DICT_POP_METHODDEF
    DICT_POPITEM_METHODDEF
    {"keys", dictkeys_new, METH_NOARGS, keys__doc__},
    {"items", dictitems_new, METH_NOARGS, items__doc__},
    {"values", dictvalues_new, METH_NOARGS, values__doc__},
    {"update",
     (PyCFunction)(void (*)(void))chkdict_update,
     METH_VARARGS | METH_KEYWORDS,
     update__doc__},
    {"fromkeys",
     (PyCFunction)(void (*)(void))chkdict_fromkeys,
     METH_FASTCALL | METH_CLASS,
     dict_fromkeys__doc__},
    {"clear", (PyCFunction)dict_clear, METH_NOARGS, clear__doc__},
    {"copy", (PyCFunction)chkdict_copy, METH_NOARGS, copy__doc__},
    DICT___REVERSED___METHODDEF{"__class_getitem__",
                                (PyCFunction)chkdict_cls_getitem,
                                METH_VARARGS | METH_CLASS,
                                NULL},
    {NULL, NULL} /* sentinel */
};

static PyObject *
chkdict_richcompare(PyObject *v, PyObject *w, int op)
{
    int cmp;
    PyObject *res;

    if (!Ci_Dict_CheckIncludingChecked(v) ||
        !Ci_Dict_CheckIncludingChecked(w)) {
        res = Py_NotImplemented;
    } else if (op == Py_EQ || op == Py_NE) {
        cmp = dict_equal((PyDictObject *)v, (PyDictObject *)w);
        if (cmp < 0)
            return NULL;
        res = (cmp == (op == Py_EQ)) ? Py_True : Py_False;
    } else
        res = Py_NotImplemented;
    Py_INCREF(res);
    return res;
}

_PyGenericTypeDef Ci_CheckedDict_Type = {
    .gtd_type =
        {
            PyVarObject_HEAD_INIT(&PyType_Type, 0) "__static__.chkdict[K, V]",
            sizeof(PyDictObject),
            0,
            (destructor)chkdict_dealloc, /* tp_dealloc */
            0,                           /* tp_vectorcall_offset */
            0,                           /* tp_getattr */
            0,                           /* tp_setattr */
            0,                           /* tp_as_async */
            (reprfunc)dict_repr,         /* tp_repr */
            0,                           /* tp_as_number */
            &dict_as_sequence,           /* tp_as_sequence */
            &chkdict_as_mapping,         /* tp_as_mapping */
            PyObject_HashNotImplemented, /* tp_hash */
            0,                           /* tp_call */
            0,                           /* tp_str */
            PyObject_GenericGetAttr,     /* tp_getattro */
            0,                           /* tp_setattro */
            0,                           /* tp_as_buffer */
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
                Ci_Py_TPFLAGS_GENERIC_TYPE_DEF, /* tp_flags */
            dictionary_doc,                  /* tp_doc */
            dict_traverse,                   /* tp_traverse */
            dict_tp_clear,                   /* tp_clear */
            chkdict_richcompare,             /* tp_richcompare */
            0,                               /* tp_weaklistoffset */
            (getiterfunc)dict_iter,          /* tp_iter */
            0,                               /* tp_iternext */
            chkmapp_methods,                 /* tp_methods */
            0,                               /* tp_members */
            0,                               /* tp_getset */
            0,                               /* tp_base */
            0,                               /* tp_dict */
            0,                               /* tp_descr_get */
            0,                               /* tp_descr_set */
            0,                               /* tp_dictoffset */
            chkdict_init,                    /* tp_init */
            chkdict_alloc,                   /* tp_alloc */
            NULL,                            /* tp_new */
            PyObject_GC_Del,                 /* tp_free */
        },
    .gtd_size = 2
};

/* === Copied from dictobject.c === */

/* Dictionary iterator types */

typedef struct {
    PyObject_HEAD
    PyDictObject *di_dict; /* Set to NULL when iterator is exhausted */
    Py_ssize_t di_used;
    Py_ssize_t di_pos;
    PyObject* di_result; /* reusable result tuple for iteritems */
    Py_ssize_t len;
} dictiterobject;

static PyObject *
dictiter_new(PyDictObject *dict, PyTypeObject *itertype)
{
    dictiterobject *di;
    di = PyObject_GC_New(dictiterobject, itertype);
    if (di == NULL) {
        return NULL;
    }
    Py_INCREF(dict);
    di->di_dict = dict;
    di->di_used = dict->ma_used;
    di->len = dict->ma_used;
    if (itertype == &Ci_CheckedDictRevIterKey_Type ||
         itertype == &Ci_CheckedDictRevIterItem_Type ||
         itertype == &Ci_CheckedDictRevIterValue_Type) {
        if (dict->ma_values) {
            di->di_pos = dict->ma_used - 1;
        }
        else {
            di->di_pos = dict->ma_keys->dk_nentries - 1;
        }
    }
    else {
        di->di_pos = 0;
    }
    if (itertype == &Ci_CheckedDictIterItem_Type ||
        itertype == &Ci_CheckedDictRevIterItem_Type) {
        di->di_result = PyTuple_Pack(2, Py_None, Py_None);
        if (di->di_result == NULL) {
            Py_DECREF(di);
            return NULL;
        }
    }
    else {
        di->di_result = NULL;
    }
    _PyObject_GC_TRACK(di);
    return (PyObject *)di;
}

static void
dictiter_dealloc(dictiterobject *di)
{
    /* bpo-31095: UnTrack is needed before calling any callbacks */
    _PyObject_GC_UNTRACK(di);
    Py_XDECREF(di->di_dict);
    Py_XDECREF(di->di_result);
    PyObject_GC_Del(di);
}

static int
dictiter_traverse(dictiterobject *di, visitproc visit, void *arg)
{
    Py_VISIT(di->di_dict);
    Py_VISIT(di->di_result);
    return 0;
}

static PyObject *
dictiter_len(dictiterobject *di, PyObject *Py_UNUSED(ignored))
{
    Py_ssize_t len = 0;
    if (di->di_dict != NULL && di->di_used == di->di_dict->ma_used)
        len = di->len;
    return PyLong_FromSize_t(len);
}

PyDoc_STRVAR(length_hint_doc,
             "Private method returning an estimate of len(list(it)).");

static PyObject *
dictiter_reduce(dictiterobject *di, PyObject *Py_UNUSED(ignored));

PyDoc_STRVAR(reduce_doc, "Return state information for pickling.");

static PyMethodDef dictiter_methods[] = {
    {"__length_hint__", (PyCFunction)(void(*)(void))dictiter_len, METH_NOARGS,
     length_hint_doc},
     {"__reduce__", (PyCFunction)(void(*)(void))dictiter_reduce, METH_NOARGS,
     reduce_doc},
    {NULL,              NULL}           /* sentinel */
};

static PyObject*
dictiter_iternextkey(dictiterobject *di)
{
    PyObject *key;
    Py_ssize_t i;
    PyDictKeysObject *k;
    PyDictObject *d = di->di_dict;

    if (d == NULL)
        return NULL;
    assert (Ci_CheckedDict_Check((PyObject*) d));

    if (di->di_used != d->ma_used) {
        PyErr_SetString(PyExc_RuntimeError,
                        "dictionary changed size during iteration");
        di->di_used = -1; /* Make this state sticky */
        return NULL;
    }

    i = di->di_pos;
    k = d->ma_keys;
    assert(i >= 0);
    if (d->ma_values) {
        if (i >= d->ma_used)
            goto fail;
        key = DK_ENTRIES(k)[i].me_key;
        assert(d->ma_values[i] != NULL);
    }
    else {
        Py_ssize_t n = k->dk_nentries;
        PyDictKeyEntry *entry_ptr = &DK_ENTRIES(k)[i];
        while (i < n && entry_ptr->me_value == NULL) {
            entry_ptr++;
            i++;
        }
        if (i >= n)
            goto fail;
        key = entry_ptr->me_key;
    }
    // We found an element (key), but did not expect it
    if (di->len == 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "dictionary keys changed during iteration");
        goto fail;
    }
    di->di_pos = i+1;
    di->len--;
    Py_INCREF(key);
    return key;

fail:
    di->di_dict = NULL;
    Py_DECREF(d);
    return NULL;
}

PyTypeObject Ci_CheckedDictIterKey_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_keyiterator",                         /* tp_name */
    sizeof(dictiterobject),                     /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dictiter_dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,/* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)dictiter_traverse,            /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    PyObject_SelfIter,                          /* tp_iter */
    (iternextfunc)dictiter_iternextkey,         /* tp_iternext */
    dictiter_methods,                           /* tp_methods */
    0,
};

static PyObject *
dictiter_iternextvalue(dictiterobject *di)
{
    PyObject **value_ptr;
    PyDictKeysObject *dk;
    PyDictKeyEntry *entry_ptr;
    PyObject *value;
    Py_ssize_t i;
    PyDictObject *d = di->di_dict;

    if (d == NULL)
        return NULL;
    assert (Ci_CheckedDict_Check((PyObject *) d));

    if (di->di_used != d->ma_used) {
        PyErr_SetString(PyExc_RuntimeError,
                        "dictionary changed size during iteration");
        di->di_used = -1; /* Make this state sticky */
        return NULL;
    }

    dk = d->ma_keys;
    i = di->di_pos;
    assert(i >= 0);
    if (d->ma_values) {
        if (i >= d->ma_used)
            goto fail;
        entry_ptr = &DK_ENTRIES(dk)[i];
        value_ptr = &d->ma_values[i];
        value = *value_ptr;
        assert(value != NULL);
    }
    else {
        Py_ssize_t n = d->ma_keys->dk_nentries;
        entry_ptr = &DK_ENTRIES(d->ma_keys)[i];
        while (i < n && entry_ptr->me_value == NULL) {
            entry_ptr++;
            i++;
        }
        if (i >= n)
            goto fail;
        value_ptr = &entry_ptr->me_value;
        value = *value_ptr;
    }
    // We found an element, but did not expect it
    if (di->len == 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "dictionary keys changed during iteration");
        goto fail;
    }
    Py_INCREF(value);
    di->di_pos = i+1;
    di->len--;
    return value;

fail:
    di->di_dict = NULL;
    Py_DECREF(d);
    return NULL;
}

PyTypeObject Ci_CheckedDictIterValue_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_valueiterator",                       /* tp_name */
    sizeof(dictiterobject),                     /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dictiter_dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)dictiter_traverse,            /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    PyObject_SelfIter,                          /* tp_iter */
    (iternextfunc)dictiter_iternextvalue,       /* tp_iternext */
    dictiter_methods,                           /* tp_methods */
    0,
};

static PyObject *
dictiter_iternextitem(dictiterobject *di)
{
    PyObject **value_ptr;
    PyDictKeysObject *dk;
    PyDictKeyEntry *entry_ptr;
    PyObject *key, *value, *result;
    Py_ssize_t i;
    PyDictObject *d = di->di_dict;

    if (d == NULL)
        return NULL;
    assert (Ci_CheckedDict_Check((PyObject *) d));

    if (di->di_used != d->ma_used) {
        PyErr_SetString(PyExc_RuntimeError,
                        "dictionary changed size during iteration");
        di->di_used = -1; /* Make this state sticky */
        return NULL;
    }

    dk = d->ma_keys;
    i = di->di_pos;
    assert(i >= 0);
    if (d->ma_values) {
        if (i >= d->ma_used)
            goto fail;
        entry_ptr = &DK_ENTRIES(dk)[i];
        key = entry_ptr->me_key;
        value_ptr = &d->ma_values[i];
        value = *value_ptr;
        assert(value != NULL);
    }
    else {
        entry_ptr = &DK_ENTRIES(dk)[i];
        Py_ssize_t n = dk->dk_nentries;
        while (i < n && entry_ptr->me_value == NULL) {
            entry_ptr++;
            i++;
        }
        if (i >= n)
            goto fail;
        key = entry_ptr->me_key;
        value_ptr = &entry_ptr->me_value;
        value = *value_ptr;
    }
    // We found an element, but did not expect it
    if (di->len == 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "dictionary keys changed during iteration");
        goto fail;
    }
    Py_INCREF(key);
    Py_INCREF(value);
    di->di_pos = i+1;
    di->len--;
    result = di->di_result;
    if (Py_REFCNT(result) == 1) {
        PyObject *oldkey = PyTuple_GET_ITEM(result, 0);
        PyObject *oldvalue = PyTuple_GET_ITEM(result, 1);
        PyTuple_SET_ITEM(result, 0, key);  /* steals reference */
        PyTuple_SET_ITEM(result, 1, value);  /* steals reference */
        Py_INCREF(result);
        Py_DECREF(oldkey);
        Py_DECREF(oldvalue);
        // bpo-42536: The GC may have untracked this result tuple. Since we're
        // recycling it, make sure it's tracked again:
        if (!_PyObject_GC_IS_TRACKED(result)) {
            _PyObject_GC_TRACK(result);
        }
    }
    else {
        result = PyTuple_New(2);
        if (result == NULL)
            return NULL;
        PyTuple_SET_ITEM(result, 0, key);  /* steals reference */
        PyTuple_SET_ITEM(result, 1, value);  /* steals reference */
    }
    return result;

fail:
    di->di_dict = NULL;
    Py_DECREF(d);
    return NULL;
}

PyTypeObject Ci_CheckedDictIterItem_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_itemiterator",                        /* tp_name */
    sizeof(dictiterobject),                     /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dictiter_dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,/* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)dictiter_traverse,            /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    PyObject_SelfIter,                          /* tp_iter */
    (iternextfunc)dictiter_iternextitem,        /* tp_iternext */
    dictiter_methods,                           /* tp_methods */
    0,
};


/* dictreviter */

static PyObject *
dictreviter_iternext(dictiterobject *di)
{
    PyObject **value_ptr;
    PyDictKeyEntry *entry_ptr;
    PyDictObject *d = di->di_dict;

    if (d == NULL) {
        return NULL;
    }
    assert (Ci_CheckedDict_Check((PyObject *) d));

    if (di->di_used != d->ma_used) {
        PyErr_SetString(PyExc_RuntimeError,
                         "dictionary changed size during iteration");
        di->di_used = -1; /* Make this state sticky */
        return NULL;
    }

    Py_ssize_t i = di->di_pos;
    PyDictKeysObject *dk = d->ma_keys;
    PyObject *key, *value, *result;

    if (i < 0) {
        goto fail;
    }
    if (d->ma_values) {
        entry_ptr = &DK_ENTRIES(dk)[i];
        key = entry_ptr->me_key;
        value_ptr = &d->ma_values[i];
        value = *value_ptr;
        assert (value != NULL);
    }
    else {
        entry_ptr = &DK_ENTRIES(dk)[i];
        while (entry_ptr->me_value == NULL) {
            if (--i < 0) {
                goto fail;
            }
            entry_ptr--;
        }
        key = entry_ptr->me_key;
        value_ptr = &entry_ptr->me_value;
        value = *value_ptr;
    }
    Py_INCREF(key);
    Py_INCREF(value);
    di->di_pos = i-1;
    di->len--;

    if (Py_IS_TYPE(di, &Ci_CheckedDictRevIterKey_Type)) {
        Py_DECREF(value);
        return key;
    }
    else if (Py_IS_TYPE(di, &Ci_CheckedDictRevIterValue_Type)) {
        Py_DECREF(key);
        return value;
    }
    else if (Py_IS_TYPE(di, &Ci_CheckedDictRevIterItem_Type)) {
        result = di->di_result;
        if (Py_REFCNT(result) == 1) {
            PyObject *oldkey = PyTuple_GET_ITEM(result, 0);
            PyObject *oldvalue = PyTuple_GET_ITEM(result, 1);
            PyTuple_SET_ITEM(result, 0, key);  /* steals reference */
            PyTuple_SET_ITEM(result, 1, value);  /* steals reference */
            Py_INCREF(result);
            Py_DECREF(oldkey);
            Py_DECREF(oldvalue);
            // bpo-42536: The GC may have untracked this result tuple. Since
            // we're recycling it, make sure it's tracked again:
            if (!_PyObject_GC_IS_TRACKED(result)) {
                _PyObject_GC_TRACK(result);
            }
        }
        else {
            result = PyTuple_New(2);
            if (result == NULL) {
                Py_DECREF(key);
                Py_DECREF(value);
                return NULL;
            }
            PyTuple_SET_ITEM(result, 0, key); /* steals reference */
            PyTuple_SET_ITEM(result, 1, value); /* steals reference */
        }
        return result;
    }
    else {
        Py_UNREACHABLE();
    }

fail:
    di->di_dict = NULL;
    Py_DECREF(d);
    return NULL;
}

PyTypeObject Ci_CheckedDictRevIterKey_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_reversekeyiterator",
    sizeof(dictiterobject),
    .tp_dealloc = (destructor)dictiter_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)dictiter_traverse,
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc)dictreviter_iternext,
    .tp_methods = dictiter_methods
};

static PyObject *
dict___reversed___impl(PyDictObject *self)
{
    assert (Ci_CheckedDict_Check((PyObject *) self));
    return dictiter_new(self, &Ci_CheckedDictRevIterKey_Type);
}

static PyObject *
dictiter_reduce(dictiterobject *di, PyObject *Py_UNUSED(ignored))
{
    _Py_IDENTIFIER(iter);
    /* copy the iterator state */
    dictiterobject tmp = *di;
    Py_XINCREF(tmp.di_dict);

    PyObject *list = PySequence_List((PyObject*)&tmp);
    Py_XDECREF(tmp.di_dict);
    if (list == NULL) {
        return NULL;
    }
    return Py_BuildValue("N(N)", _PyEval_GetBuiltinId(&PyId_iter), list);
}

PyTypeObject Ci_CheckedDictRevIterItem_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_reverseitemiterator",
    sizeof(dictiterobject),
    .tp_dealloc = (destructor)dictiter_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)dictiter_traverse,
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc)dictreviter_iternext,
    .tp_methods = dictiter_methods
};

PyTypeObject Ci_CheckedDictRevIterValue_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_reversevalueiterator",
    sizeof(dictiterobject),
    .tp_dealloc = (destructor)dictiter_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)dictiter_traverse,
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc)dictreviter_iternext,
    .tp_methods = dictiter_methods
};

/***********************************************/
/* View objects for keys(), items(), values(). */
/***********************************************/

/* The instance lay-out is the same for all three; but the type differs. */

static void
dictview_dealloc(_PyDictViewObject *dv)
{
    /* bpo-31095: UnTrack is needed before calling any callbacks */
    _PyObject_GC_UNTRACK(dv);
    Py_XDECREF(dv->dv_dict);
    PyObject_GC_Del(dv);
}

static int
dictview_traverse(_PyDictViewObject *dv, visitproc visit, void *arg)
{
    Py_VISIT(dv->dv_dict);
    return 0;
}

static Py_ssize_t
dictview_len(_PyDictViewObject *dv)
{
    Py_ssize_t len = 0;
    if (dv->dv_dict != NULL)
        len = dv->dv_dict->ma_used;
    return len;
}

static PyObject *
Ci_CheckedDictView_New(PyObject *dict, PyTypeObject *type)
{
    PyDictObject *d;
    _PyDictViewObject *dv;
    if (dict == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!Ci_CheckedDict_Check(dict)) {
        /* XXX Get rid of this restriction later */
        PyErr_Format(PyExc_TypeError,
                     "%s() requires a dict argument, not '%s'",
                     type->tp_name, Py_TYPE(dict)->tp_name);
        return NULL;
    }
    dv = PyObject_GC_New(_PyDictViewObject, type);
    if (dv == NULL)
        return NULL;
    Py_INCREF(dict);
    d = (PyDictObject *)dict;
    if (type == &Ci_CheckedDictItems_Type ||
        type == &Ci_CheckedDictValues_Type) {
    }
    dv->dv_dict = d;
    _PyObject_GC_TRACK(dv);
    return (PyObject *)dv;
}

static PyObject *
dictview_mapping(PyObject *view, void *Py_UNUSED(ignored)) {
    assert(view != NULL);
    assert(Ci_CheckedDictKeys_Check(view)
           || Ci_CheckedDictValues_Check(view)
           || Ci_CheckedDictItems_Check(view));
    PyObject *mapping = (PyObject *)((_PyDictViewObject *)view)->dv_dict;
    return PyDictProxy_New(mapping);
}

static PyGetSetDef dictview_getset[] = {
    {"mapping", dictview_mapping, (setter)NULL,
     "dictionary that this view refers to", NULL},
    {0}
};

/* TODO(guido): The views objects are not complete:

 * support more set operations
 * support arbitrary mappings?
   - either these should be static or exported in dictobject.h
   - if public then they should probably be in builtins
*/

/* Return 1 if self is a subset of other, iterating over self;
   0 if not; -1 if an error occurred. */
static int
all_contained_in(PyObject *self, PyObject *other)
{
    PyObject *iter = PyObject_GetIter(self);
    int ok = 1;

    if (iter == NULL)
        return -1;
    for (;;) {
        PyObject *next = PyIter_Next(iter);
        if (next == NULL) {
            if (PyErr_Occurred())
                ok = -1;
            break;
        }
        ok = PySequence_Contains(other, next);
        Py_DECREF(next);
        if (ok <= 0)
            break;
    }
    Py_DECREF(iter);
    return ok;
}

static PyObject *
dictview_richcompare(PyObject *self, PyObject *other, int op)
{
    Py_ssize_t len_self, len_other;
    int ok;
    PyObject *result;

    assert(self != NULL);
    assert(Ci_CheckedDictViewSet_Check(self));
    assert(other != NULL);

    if (!PyAnySet_Check(other) && !Ci_CheckedDictViewSet_Check(other))
        Py_RETURN_NOTIMPLEMENTED;

    len_self = PyObject_Size(self);
    if (len_self < 0)
        return NULL;
    len_other = PyObject_Size(other);
    if (len_other < 0)
        return NULL;

    ok = 0;
    switch(op) {

    case Py_NE:
    case Py_EQ:
        if (len_self == len_other)
            ok = all_contained_in(self, other);
        if (op == Py_NE && ok >= 0)
            ok = !ok;
        break;

    case Py_LT:
        if (len_self < len_other)
            ok = all_contained_in(self, other);
        break;

      case Py_LE:
          if (len_self <= len_other)
              ok = all_contained_in(self, other);
          break;

    case Py_GT:
        if (len_self > len_other)
            ok = all_contained_in(other, self);
        break;

    case Py_GE:
        if (len_self >= len_other)
            ok = all_contained_in(other, self);
        break;

    }
    if (ok < 0)
        return NULL;
    result = ok ? Py_True : Py_False;
    Py_INCREF(result);
    return result;
}

static PyObject *
dictview_repr(_PyDictViewObject *dv)
{
    PyObject *seq;
    PyObject *result = NULL;
    Py_ssize_t rc;

    rc = Py_ReprEnter((PyObject *)dv);
    if (rc != 0) {
        return rc > 0 ? PyUnicode_FromString("...") : NULL;
    }
    seq = PySequence_List((PyObject *)dv);
    if (seq == NULL) {
        goto Done;
    }
    result = PyUnicode_FromFormat("%s(%R)", Py_TYPE(dv)->tp_name, seq);
    Py_DECREF(seq);

Done:
    Py_ReprLeave((PyObject *)dv);
    return result;
}

/*** dict_keys ***/

static PyObject *
dictkeys_iter(_PyDictViewObject *dv)
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &Ci_CheckedDictIterKey_Type);
}

static int
dictkeys_contains(_PyDictViewObject *dv, PyObject *obj)
{
    if (dv->dv_dict == NULL)
        return 0;
    return PyDict_Contains((PyObject *)dv->dv_dict, obj);
}

static PySequenceMethods dictkeys_as_sequence = {
    (lenfunc)dictview_len,              /* sq_length */
    0,                                  /* sq_concat */
    0,                                  /* sq_repeat */
    0,                                  /* sq_item */
    0,                                  /* sq_slice */
    0,                                  /* sq_ass_item */
    0,                                  /* sq_ass_slice */
    (objobjproc)dictkeys_contains,      /* sq_contains */
};

// Create an set object from dictviews object.
// Returns a new reference.
// This utility function is used by set operations.
static PyObject*
dictviews_to_set(PyObject *self)
{
    PyObject *left = self;
    if (Ci_CheckedDictKeys_Check(self)) {
        // PySet_New() has fast path for the dict object.
        PyObject *dict = (PyObject *)((_PyDictViewObject *)self)->dv_dict;
        if (PyDict_CheckExact(dict)) {
            left = dict;
        }
    }
    return PySet_New(left);
}

static PyObject*
dictviews_sub(PyObject *self, PyObject *other)
{
    PyObject *result = dictviews_to_set(self);
    if (result == NULL) {
        return NULL;
    }

    _Py_IDENTIFIER(difference_update);
    PyObject *tmp = _PyObject_CallMethodIdOneArg(
            result, &PyId_difference_update, other);
    if (tmp == NULL) {
        Py_DECREF(result);
        return NULL;
    }

    Py_DECREF(tmp);
    return result;
}

static int
dictitems_contains(_PyDictViewObject *dv, PyObject *obj);

static PyObject *
Ci_CheckedDictView_Intersect(PyObject* self, PyObject *other)
{
    PyObject *result;
    PyObject *it;
    PyObject *key;
    Py_ssize_t len_self;
    int rv;
    int (*dict_contains)(_PyDictViewObject *, PyObject *);

    /* Python interpreter swaps parameters when dict view
       is on right side of & */
    if (!Ci_CheckedDictViewSet_Check(self)) {
        PyObject *tmp = other;
        other = self;
        self = tmp;
    }

    len_self = dictview_len((_PyDictViewObject *)self);

    /* if other is a set and self is smaller than other,
       reuse set intersection logic */
    if (PySet_CheckExact(other) && len_self <= PyObject_Size(other)) {
        _Py_IDENTIFIER(intersection);
        return _PyObject_CallMethodIdObjArgs(other, &PyId_intersection, self, NULL);
    }

    /* if other is another dict view, and it is bigger than self,
       swap them */
    if (Ci_CheckedDictViewSet_Check(other)) {
        Py_ssize_t len_other = dictview_len((_PyDictViewObject *)other);
        if (len_other > len_self) {
            PyObject *tmp = other;
            other = self;
            self = tmp;
        }
    }

    /* at this point, two things should be true
       1. self is a dictview
       2. if other is a dictview then it is smaller than self */
    result = PySet_New(NULL);
    if (result == NULL)
        return NULL;

    it = PyObject_GetIter(other);
    if (it == NULL) {
        Py_DECREF(result);
        return NULL;
    }

    if (Ci_CheckedDictKeys_Check(self)) {
        dict_contains = dictkeys_contains;
    }
    /* else Ci_CheckedDictItems_Check(self) */
    else {
        dict_contains = dictitems_contains;
    }

    while ((key = PyIter_Next(it)) != NULL) {
        rv = dict_contains((_PyDictViewObject *)self, key);
        if (rv < 0) {
            goto error;
        }
        if (rv) {
            if (PySet_Add(result, key)) {
                goto error;
            }
        }
        Py_DECREF(key);
    }
    Py_DECREF(it);
    if (PyErr_Occurred()) {
        Py_DECREF(result);
        return NULL;
    }
    return result;

error:
    Py_DECREF(it);
    Py_DECREF(result);
    Py_DECREF(key);
    return NULL;
}

static PyObject*
dictviews_or(PyObject* self, PyObject *other)
{
    PyObject *result = dictviews_to_set(self);
    if (result == NULL) {
        return NULL;
    }

    if (_PySet_Update(result, other) < 0) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject *
dictitems_xor(PyObject *self, PyObject *other)
{
    assert(Ci_CheckedDictItems_Check(self));
    assert(Ci_CheckedDictItems_Check(other));
    PyObject *d1 = (PyObject *)((_PyDictViewObject *)self)->dv_dict;
    PyObject *d2 = (PyObject *)((_PyDictViewObject *)other)->dv_dict;

    PyObject *temp_dict = PyDict_Copy(d1);
    if (temp_dict == NULL) {
        return NULL;
    }
    PyObject *result_set = PySet_New(NULL);
    if (result_set == NULL) {
        Py_CLEAR(temp_dict);
        return NULL;
    }

    PyObject *key = NULL, *val1 = NULL, *val2 = NULL;
    Py_ssize_t pos = 0;
    Py_hash_t hash;

    while (Ci_CheckedDict_Next(d2, &pos, &key, &val2, &hash)) {
        Py_INCREF(key);
        Py_INCREF(val2);
        val1 = _PyDict_GetItem_KnownHash(temp_dict, key, hash);

        int to_delete;
        if (val1 == NULL) {
            if (PyErr_Occurred()) {
                goto error;
            }
            to_delete = 0;
        }
        else {
            Py_INCREF(val1);
            to_delete = PyObject_RichCompareBool(val1, val2, Py_EQ);
            if (to_delete < 0) {
                goto error;
            }
        }

        if (to_delete) {
            if (_PyDict_DelItem_KnownHash(temp_dict, key, hash) < 0) {
                goto error;
            }
        }
        else {
            PyObject *pair = PyTuple_Pack(2, key, val2);
            if (pair == NULL) {
                goto error;
            }
            if (PySet_Add(result_set, pair) < 0) {
                Py_DECREF(pair);
                goto error;
            }
            Py_DECREF(pair);
        }
        Py_DECREF(key);
        Py_XDECREF(val1);
        Py_DECREF(val2);
    }
    key = val1 = val2 = NULL;

    _Py_IDENTIFIER(items);
    PyObject *remaining_pairs = _PyObject_CallMethodIdNoArgs(temp_dict,
                                                             &PyId_items);
    if (remaining_pairs == NULL) {
        goto error;
    }
    if (_PySet_Update(result_set, remaining_pairs) < 0) {
        Py_DECREF(remaining_pairs);
        goto error;
    }
    Py_DECREF(temp_dict);
    Py_DECREF(remaining_pairs);
    return result_set;

error:
    Py_XDECREF(temp_dict);
    Py_XDECREF(result_set);
    Py_XDECREF(key);
    Py_XDECREF(val1);
    Py_XDECREF(val2);
    return NULL;
}

static PyObject*
dictviews_xor(PyObject* self, PyObject *other)
{
    if (Ci_CheckedDictItems_Check(self) && Ci_CheckedDictItems_Check(other)) {
        return dictitems_xor(self, other);
    }
    PyObject *result = dictviews_to_set(self);
    if (result == NULL) {
        return NULL;
    }

    _Py_IDENTIFIER(symmetric_difference_update);
    PyObject *tmp = _PyObject_CallMethodIdOneArg(
            result, &PyId_symmetric_difference_update, other);
    if (tmp == NULL) {
        Py_DECREF(result);
        return NULL;
    }

    Py_DECREF(tmp);
    return result;
}

static PyNumberMethods dictviews_as_number = {
    0,                                  /*nb_add*/
    (binaryfunc)dictviews_sub,          /*nb_subtract*/
    0,                                  /*nb_multiply*/
    0,                                  /*nb_remainder*/
    0,                                  /*nb_divmod*/
    0,                                  /*nb_power*/
    0,                                  /*nb_negative*/
    0,                                  /*nb_positive*/
    0,                                  /*nb_absolute*/
    0,                                  /*nb_bool*/
    0,                                  /*nb_invert*/
    0,                                  /*nb_lshift*/
    0,                                  /*nb_rshift*/
    (binaryfunc)Ci_CheckedDictView_Intersect,  /*nb_and*/
    (binaryfunc)dictviews_xor,          /*nb_xor*/
    (binaryfunc)dictviews_or,           /*nb_or*/
};

static PyObject*
dictviews_isdisjoint(PyObject *self, PyObject *other)
{
    PyObject *it;
    PyObject *item = NULL;

    if (self == other) {
        if (dictview_len((_PyDictViewObject *)self) == 0)
            Py_RETURN_TRUE;
        else
            Py_RETURN_FALSE;
    }

    /* Iterate over the shorter object (only if other is a set,
     * because PySequence_Contains may be expensive otherwise): */
    if (PyAnySet_Check(other) || Ci_CheckedDictViewSet_Check(other)) {
        Py_ssize_t len_self = dictview_len((_PyDictViewObject *)self);
        Py_ssize_t len_other = PyObject_Size(other);
        if (len_other == -1)
            return NULL;

        if ((len_other > len_self)) {
            PyObject *tmp = other;
            other = self;
            self = tmp;
        }
    }

    it = PyObject_GetIter(other);
    if (it == NULL)
        return NULL;

    while ((item = PyIter_Next(it)) != NULL) {
        int contains = PySequence_Contains(self, item);
        Py_DECREF(item);
        if (contains == -1) {
            Py_DECREF(it);
            return NULL;
        }

        if (contains) {
            Py_DECREF(it);
            Py_RETURN_FALSE;
        }
    }
    Py_DECREF(it);
    if (PyErr_Occurred())
        return NULL; /* PyIter_Next raised an exception. */
    Py_RETURN_TRUE;
}

PyDoc_STRVAR(isdisjoint_doc,
"Return True if the view and the given iterable have a null intersection.");

static PyObject* dictkeys_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored));

PyDoc_STRVAR(reversed_keys_doc,
"Return a reverse iterator over the dict keys.");

static PyMethodDef dictkeys_methods[] = {
    {"isdisjoint",      (PyCFunction)dictviews_isdisjoint,  METH_O,
     isdisjoint_doc},
    {"__reversed__",    (PyCFunction)(void(*)(void))dictkeys_reversed,    METH_NOARGS,
     reversed_keys_doc},
    {NULL,              NULL}           /* sentinel */
};

PyTypeObject Ci_CheckedDictKeys_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_keys",                                /* tp_name */
    sizeof(_PyDictViewObject),                  /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dictview_dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)dictview_repr,                    /* tp_repr */
    &dictviews_as_number,                       /* tp_as_number */
    &dictkeys_as_sequence,                      /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,/* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)dictview_traverse,            /* tp_traverse */
    0,                                          /* tp_clear */
    dictview_richcompare,                       /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    (getiterfunc)dictkeys_iter,                 /* tp_iter */
    0,                                          /* tp_iternext */
    dictkeys_methods,                           /* tp_methods */
    .tp_getset = dictview_getset,
};

static PyObject *
dictkeys_new(PyObject *dict, PyObject *Py_UNUSED(ignored))
{
    return Ci_CheckedDictView_New(dict, &Ci_CheckedDictKeys_Type);
}

static PyObject *
dictkeys_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored))
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &Ci_CheckedDictRevIterKey_Type);
}

/*** dict_items ***/

static PyObject *
dictitems_iter(_PyDictViewObject *dv)
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &Ci_CheckedDictIterItem_Type);
}

static int
dictitems_contains(_PyDictViewObject *dv, PyObject *obj)
{
    int result;
    PyObject *key, *value, *found;
    if (dv->dv_dict == NULL)
        return 0;
    if (!PyTuple_Check(obj) || PyTuple_GET_SIZE(obj) != 2)
        return 0;
    key = PyTuple_GET_ITEM(obj, 0);
    value = PyTuple_GET_ITEM(obj, 1);
    found = PyDict_GetItemWithError((PyObject *)dv->dv_dict, key);
    if (found == NULL) {
        if (PyErr_Occurred())
            return -1;
        return 0;
    }
    Py_INCREF(found);
    result = PyObject_RichCompareBool(found, value, Py_EQ);
    Py_DECREF(found);
    return result;
}

static PySequenceMethods dictitems_as_sequence = {
    (lenfunc)dictview_len,              /* sq_length */
    0,                                  /* sq_concat */
    0,                                  /* sq_repeat */
    0,                                  /* sq_item */
    0,                                  /* sq_slice */
    0,                                  /* sq_ass_item */
    0,                                  /* sq_ass_slice */
    (objobjproc)dictitems_contains,     /* sq_contains */
};

static PyObject* dictitems_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored));

PyDoc_STRVAR(reversed_items_doc,
"Return a reverse iterator over the dict items.");

static PyMethodDef dictitems_methods[] = {
    {"isdisjoint",      (PyCFunction)dictviews_isdisjoint,  METH_O,
     isdisjoint_doc},
    {"__reversed__",    (PyCFunction)dictitems_reversed,    METH_NOARGS,
     reversed_items_doc},
    {NULL,              NULL}           /* sentinel */
};

PyTypeObject Ci_CheckedDictItems_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_items",                               /* tp_name */
    sizeof(_PyDictViewObject),                  /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dictview_dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)dictview_repr,                    /* tp_repr */
    &dictviews_as_number,                       /* tp_as_number */
    &dictitems_as_sequence,                     /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,/* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)dictview_traverse,            /* tp_traverse */
    0,                                          /* tp_clear */
    dictview_richcompare,                       /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    (getiterfunc)dictitems_iter,                /* tp_iter */
    0,                                          /* tp_iternext */
    dictitems_methods,                          /* tp_methods */
    .tp_getset = dictview_getset,
};

static PyObject *
dictitems_new(PyObject *dict, PyObject *Py_UNUSED(ignored))
{
    return Ci_CheckedDictView_New(dict, &Ci_CheckedDictItems_Type);
}

static PyObject *
dictitems_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored))
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &Ci_CheckedDictRevIterItem_Type);
}

/*** dict_values ***/

static PyObject *
dictvalues_iter(_PyDictViewObject *dv)
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &Ci_CheckedDictIterValue_Type);
}

static PySequenceMethods dictvalues_as_sequence = {
    (lenfunc)dictview_len,              /* sq_length */
    0,                                  /* sq_concat */
    0,                                  /* sq_repeat */
    0,                                  /* sq_item */
    0,                                  /* sq_slice */
    0,                                  /* sq_ass_item */
    0,                                  /* sq_ass_slice */
    (objobjproc)0,                      /* sq_contains */
};

static PyObject* dictvalues_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored));

PyDoc_STRVAR(reversed_values_doc,
"Return a reverse iterator over the dict values.");

static PyMethodDef dictvalues_methods[] = {
    {"__reversed__",    (PyCFunction)dictvalues_reversed,    METH_NOARGS,
     reversed_values_doc},
    {NULL,              NULL}           /* sentinel */
};

PyTypeObject Ci_CheckedDictValues_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict_values",                              /* tp_name */
    sizeof(_PyDictViewObject),                  /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dictview_dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)dictview_repr,                    /* tp_repr */
    0,                                          /* tp_as_number */
    &dictvalues_as_sequence,                    /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,/* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)dictview_traverse,            /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    (getiterfunc)dictvalues_iter,               /* tp_iter */
    0,                                          /* tp_iternext */
    dictvalues_methods,                         /* tp_methods */
    .tp_getset = dictview_getset,
};

static PyObject *
dictvalues_new(PyObject *dict, PyObject *Py_UNUSED(ignored))
{
    return Ci_CheckedDictView_New(dict, &Ci_CheckedDictValues_Type);
}

static PyObject *
dictvalues_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored))
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &Ci_CheckedDictRevIterValue_Type);
}


/* === End copied from dictobject.c === */
