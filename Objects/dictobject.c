/* Dictionary object implementation using a hash table */

/* The distribution includes a separate file, Objects/dictnotes.txt,
   describing explorations into dictionary design and optimization.
   It covers typical dictionary use patterns, the parameters for
   tuning dictionaries, and several ideas for possible optimizations.
*/

/* PyDictKeysObject

This implements the dictionary's hashtable.

As of Python 3.6, this is compact and ordered. Basic idea is described here:
* https://mail.python.org/pipermail/python-dev/2012-December/123028.html
* https://morepypy.blogspot.com/2015/01/faster-more-memory-efficient-and-more.html

layout:

+---------------+
| dk_refcnt     |
| dk_size       |
| dk_lookup     |
| dk_usable     |
| dk_nentries   |
+---------------+
| dk_indices    |
|               |
+---------------+
| dk_entries    |
|               |
+---------------+

dk_indices is actual hashtable.  It holds index in entries, or DKIX_EMPTY(-1)
or DKIX_DUMMY(-2).
Size of indices is dk_size.  Type of each index in indices is vary on dk_size:

* int8  for          dk_size <= 128
* int16 for 256   <= dk_size <= 2**15
* int32 for 2**16 <= dk_size <= 2**31
* int64 for 2**32 <= dk_size

dk_entries is array of PyDictKeyEntry.  Its size is USABLE_FRACTION(dk_size).
DK_ENTRIES(dk) can be used to get pointer to entries.

NOTE: Since negative value is used for DKIX_EMPTY and DKIX_DUMMY, type of
dk_indices entry is signed integer and int16 is used for table which
dk_size == 256.
*/


/*
The DictObject can be in one of two forms.

Either:
  A combined table:
    ma_values == NULL, dk_refcnt == 1.
    Values are stored in the me_value field of the PyDictKeysObject.
Or:
  A split table:
    ma_values != NULL, dk_refcnt >= 1
    Values are stored in the ma_values array.
    Only string (unicode) keys are allowed.
    All dicts sharing same key must have same insertion order.

There are four kinds of slots in the table (slot is index, and
DK_ENTRIES(keys)[index] if index >= 0):

1. Unused.  index == DKIX_EMPTY
   Does not hold an active (key, value) pair now and never did.  Unused can
   transition to Active upon key insertion.  This is each slot's initial state.

2. Active.  index >= 0, me_key != NULL and me_value != NULL
   Holds an active (key, value) pair.  Active can transition to Dummy or
   Pending upon key deletion (for combined and split tables respectively).
   This is the only case in which me_value != NULL.

3. Dummy.  index == DKIX_DUMMY  (combined only)
   Previously held an active (key, value) pair, but that was deleted and an
   active pair has not yet overwritten the slot.  Dummy can transition to
   Active upon key insertion.  Dummy slots cannot be made Unused again
   else the probe sequence in case of collision would have no way to know
   they were once active.

4. Pending. index >= 0, key != NULL, and value == NULL  (split only)
   Not yet inserted in split-table.
*/

/*
Preserving insertion order

It's simple for combined table.  Since dk_entries is mostly append only, we can
get insertion order by just iterating dk_entries.

One exception is .popitem().  It removes last item in dk_entries and decrement
dk_nentries to achieve amortized O(1).  Since there are DKIX_DUMMY remains in
dk_indices, we can't increment dk_usable even though dk_nentries is
decremented.

In split table, inserting into pending entry is allowed only for dk_entries[ix]
where ix == mp->ma_used. Inserting into other index and deleting item cause
converting the dict to the combined table.
*/

/* PyDict_MINSIZE is the starting size for any new dict.
 * 8 allows dicts with no more than 5 active entries; experiments suggested
 * this suffices for the majority of dicts (consisting mostly of usually-small
 * dicts created to pass keyword arguments).
 * Making this 8, rather than 4 reduces the number of resizes for most
 * dictionaries, without any significant extra memory use.
 */
#define PyDict_MINSIZE 8

#include "Python.h"
#include "pycore_bitutils.h" // _Py_bit_length
#include "pycore_gc.h"       // _PyObject_GC_IS_TRACKED()
#include "pycore_object.h"   // _PyObject_GC_TRACK()
#include "pycore_pyerrors.h" // _PyErr_Fetch()
#include "pycore_pystate.h"  // _PyThreadState_GET()
#include "dict-common.h"
#include "stringlib/eq.h"    // unicode_eq()
#include "classloader.h"
#include "cinder/exports.h"

/* TODO(T113261295): Remove this once dict watchers are upstreamed. */
#include "Jit/pyjit.h"

/*[clinic input]
class dict "PyDictObject *" "&PyDict_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=f157a5a0ce9589d6]*/


/*
To ensure the lookup algorithm terminates, there must be at least one Unused
slot (NULL key) in the table.
To avoid slowing down lookups on a near-full table, we resize the table when
it's USABLE_FRACTION (currently two-thirds) full.
*/

#define PERTURB_SHIFT 5

/*
Major subtleties ahead:  Most hash schemes depend on having a "good" hash
function, in the sense of simulating randomness.  Python doesn't:  its most
important hash functions (for ints) are very regular in common
cases:

  >>>[hash(i) for i in range(4)]
  [0, 1, 2, 3]

This isn't necessarily bad!  To the contrary, in a table of size 2**i, taking
the low-order i bits as the initial table index is extremely fast, and there
are no collisions at all for dicts indexed by a contiguous range of ints. So
this gives better-than-random behavior in common cases, and that's very
desirable.

OTOH, when collisions occur, the tendency to fill contiguous slices of the
hash table makes a good collision resolution strategy crucial.  Taking only
the last i bits of the hash code is also vulnerable:  for example, consider
the list [i << 16 for i in range(20000)] as a set of keys.  Since ints are
their own hash codes, and this fits in a dict of size 2**15, the last 15 bits
 of every hash code are all 0:  they *all* map to the same table index.

But catering to unusual cases should not slow the usual ones, so we just take
the last i bits anyway.  It's up to collision resolution to do the rest.  If
we *usually* find the key we're looking for on the first try (and, it turns
out, we usually do -- the table load factor is kept under 2/3, so the odds
are solidly in our favor), then it makes best sense to keep the initial index
computation dirt cheap.

The first half of collision resolution is to visit table indices via this
recurrence:

    j = ((5*j) + 1) mod 2**i

For any initial j in range(2**i), repeating that 2**i times generates each
int in range(2**i) exactly once (see any text on random-number generation for
proof).  By itself, this doesn't help much:  like linear probing (setting
j += 1, or j -= 1, on each loop trip), it scans the table entries in a fixed
order.  This would be bad, except that's not the only thing we do, and it's
actually *good* in the common cases where hash keys are consecutive.  In an
example that's really too small to make this entirely clear, for a table of
size 2**3 the order of indices is:

    0 -> 1 -> 6 -> 7 -> 4 -> 5 -> 2 -> 3 -> 0 [and here it's repeating]

If two things come in at index 5, the first place we look after is index 2,
not 6, so if another comes in at index 6 the collision at 5 didn't hurt it.
Linear probing is deadly in this case because there the fixed probe order
is the *same* as the order consecutive keys are likely to arrive.  But it's
extremely unlikely hash codes will follow a 5*j+1 recurrence by accident,
and certain that consecutive hash codes do not.

The other half of the strategy is to get the other bits of the hash code
into play.  This is done by initializing a (unsigned) vrbl "perturb" to the
full hash code, and changing the recurrence to:

    perturb >>= PERTURB_SHIFT;
    j = (5*j) + 1 + perturb;
    use j % 2**i as the next table index;

Now the probe sequence depends (eventually) on every bit in the hash code,
and the pseudo-scrambling property of recurring on 5*j+1 is more valuable,
because it quickly magnifies small differences in the bits that didn't affect
the initial index.  Note that because perturb is unsigned, if the recurrence
is executed often enough perturb eventually becomes and remains 0.  At that
point (very rarely reached) the recurrence is on (just) 5*j+1 again, and
that's certain to find an empty slot eventually (since it generates every int
in range(2**i), and we make sure there's always at least one empty slot).

Selecting a good value for PERTURB_SHIFT is a balancing act.  You want it
small so that the high bits of the hash code continue to affect the probe
sequence across iterations; but you want it large so that in really bad cases
the high-order hash bits have an effect on early iterations.  5 was "the
best" in minimizing total collisions across experiments Tim Peters ran (on
both normal and pathological cases), but 4 and 6 weren't significantly worse.

Historical: Reimer Behrends contributed the idea of using a polynomial-based
approach, using repeated multiplication by x in GF(2**n) where an irreducible
polynomial for each table size was chosen such that x was a primitive root.
Christian Tismer later extended that to use division by x instead, as an
efficient way to get the high bits of the hash code into play.  This scheme
also gave excellent collision statistics, but was more expensive:  two
if-tests were required inside the loop; computing "the next" index took about
the same number of operations but without as much potential parallelism
(e.g., computing 5*j can go on at the same time as computing 1+perturb in the
above, and then shifting perturb can be done while the table index is being
masked); and the PyDictObject struct required a member to hold the table's
polynomial.  In Tim's experiments the current scheme ran faster, produced
equally good collision statistics, needed less code & used less memory.

*/

/* forward declarations */
static Py_ssize_t lookdict(PyDictObject *mp, PyObject *key,
                           Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports);
static Py_ssize_t lookdict_with_lazy_imports(PyDictObject *mp, PyObject *key,
                                             Py_hash_t hash, PyObject **value_addr,
                                             int resolve_lazy_imports);
static Py_ssize_t lookdict_unicode(PyDictObject *mp, PyObject *key,
                                   Py_hash_t hash, PyObject **value_addr,
                                   int resolve_lazy_imports);
static Py_ssize_t lookdict_with_lazy_imports_unicode(PyDictObject *mp, PyObject *key,
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

/*Global counter used to set ma_version_tag field of dictionary.
 * It is incremented each time that a dictionary is created and each
 * time that a dictionary is modified.
 *
 * Modifications for JIT dict watching support: the global version is
 * incremented by 2 with each modification, and the low bit is reserved to
 * indicate dicts that are being watched. */
static uint64_t pydict_global_version = 0;

#define DICT_NEXT_VERSION() (pydict_global_version += 2)

static const uint64_t PyDict_VERSION_WATCH_TAG = 1;

#define DICT_NEXT_WATCHED_VERSION()                                           \
    (DICT_NEXT_VERSION() | PyDict_VERSION_WATCH_TAG)

#define UNLIKELY(x) __builtin_expect((x), 0)

static inline int
dict_is_watched(PyDictObject *dict)
{
    return (dict->ma_version_tag & PyDict_VERSION_WATCH_TAG) != 0;
}

static inline void
dict_modify_key(PyDictObject *dict, PyObject *key, PyObject *new_value)
{
    if (UNLIKELY(dict_is_watched(dict))) {
        dict->ma_version_tag = DICT_NEXT_WATCHED_VERSION();
        /* TODO(T113261295): Replace this with the generic hook once dict
         * watchers are upstreamed. */
#ifdef ENABLE_CINDERVM
        _PyJIT_NotifyDictKey((PyObject *)dict, key, new_value);
#endif
    } else {
        dict->ma_version_tag = DICT_NEXT_VERSION();
    }
}

static inline void
dict_set_lookup(PyDictObject *dict, dict_lookup_func new_lookup)
{
    if (UNLIKELY(dict_is_watched(dict))) {
        /* TODO(T113261295): Replace this with the generic hook once dict
         * watchers are upstreamed. */
#ifdef ENABLE_CINDERVM
        _PyJIT_NotifyDictUnwatch((PyObject *)dict);
#endif
        dict->ma_version_tag = DICT_NEXT_VERSION();
    }
    dict->ma_keys->dk_lookup = new_lookup;
}

#define DICT_HAS_DEFERRED(d) ( \
    ((PyDictObject *)(d))->ma_keys->dk_lookup == lookdict_with_lazy_imports || \
    ((PyDictObject *)(d))->ma_keys->dk_lookup == lookdict_with_lazy_imports_unicode)

#include "clinic/dictobject.c.h"

void
_PyDict_IncVersionForSet(PyDictObject *d, PyObject *key, PyObject *value)
{
    dict_modify_key(d, key, value);
}

int
_PyDict_CanWatch(PyObject *dict)
{
    dict_lookup_func lookup = ((PyDictObject *)dict)->ma_keys->dk_lookup;
    return lookup != lookdict && lookup != lookdict_with_lazy_imports;
}

int
_PyDict_IsWatched(PyObject *dict)
{
    return dict_is_watched((PyDictObject *)dict);
}

void
_PyDict_Watch(PyObject *dict)
{
    assert(_PyDict_CanWatch(dict));
    ((PyDictObject *)dict)->ma_version_tag |= PyDict_VERSION_WATCH_TAG;
}

void
_PyDict_Unwatch(PyObject *dict)
{
    assert(_PyDict_IsWatched(dict));
    ((PyDictObject *)dict)->ma_version_tag = DICT_NEXT_VERSION();
}

int
_PyDict_HasUnsafeKeys(PyObject *dict)
{
    assert(PyDict_Check(dict));
    dict_lookup_func lookup = ((PyDictObject *)dict)->ma_keys->dk_lookup;
    return lookup != lookdict_unicode && lookup != lookdict_unicode_nodummy &&
        lookup != lookdict_split;
}

int
_PyDict_HasDeferredObjects(PyObject *dict)
{
    assert(PyDict_Check(dict));
    return DICT_HAS_DEFERRED(dict);
}

static struct _Py_dict_state *
get_dict_state(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return &interp->dict_state;
}


void
_PyDict_ClearFreeList(PyInterpreterState *interp)
{
    struct _Py_dict_state *state = &interp->dict_state;
    while (state->numfree) {
        PyDictObject *op = state->free_list[--state->numfree];
        assert(PyDict_CheckExact(op));
        PyObject_GC_Del(op);
    }
    while (state->keys_numfree) {
        PyObject_Free(state->keys_free_list[--state->keys_numfree]);
    }
}


void
_PyDict_Fini(PyInterpreterState *interp)
{
    _PyDict_ClearFreeList(interp);
#ifdef Py_DEBUG
    struct _Py_dict_state *state = &interp->dict_state;
    state->numfree = -1;
    state->keys_numfree = -1;
#endif
}


/* Print summary info about the state of the optimized allocator */
void
_PyDict_DebugMallocStats(FILE *out)
{
    struct _Py_dict_state *state = get_dict_state();
    _PyDebugAllocatorStats(out, "free PyDictObject",
                           state->numfree, sizeof(PyDictObject));
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

static void free_keys_object(PyDictKeysObject *keys);

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

/* Uncomment to check the dict content in _PyDict_CheckConsistency() */
/* #define DEBUG_PYDICT */

#ifdef DEBUG_PYDICT
#  define ASSERT_CONSISTENT(op) assert(_PyDict_CheckConsistency((PyObject *)(op), 1))
#else
#  define ASSERT_CONSISTENT(op) assert(_PyDict_CheckConsistency((PyObject *)(op), 0))
#endif

void
_PyDict_SetHasDeferredObjects(PyObject *dict)
{
    assert(PyDict_Check(dict));
    PyDictObject *mp = (PyDictObject *)dict;

    if (!DICT_HAS_DEFERRED(mp)) {
        if (mp->ma_keys->dk_lookup == lookdict) {
            assert(!dict_is_watched(mp));
            mp->ma_keys->dk_lookup = lookdict_with_lazy_imports;
        }
        else
        if (mp->ma_keys->dk_lookup == lookdict_unicode ||
            mp->ma_keys->dk_lookup == lookdict_unicode_nodummy) {
            mp->ma_keys->dk_lookup = lookdict_with_lazy_imports_unicode;
        }
        else {
            Py_UNREACHABLE();
        }
    }
}

void
_PyDict_UnsetHasDeferredObjects(PyObject *dict)
{
    assert(PyDict_Check(dict));
    PyDictObject *mp = (PyDictObject *)dict;

    if (DICT_HAS_DEFERRED(mp)) {
        if (mp->ma_keys->dk_lookup == lookdict_with_lazy_imports) {
            assert(!dict_is_watched(mp));
            mp->ma_keys->dk_lookup = lookdict;
        }
        else
        if (mp->ma_keys->dk_lookup == lookdict_with_lazy_imports_unicode) {
            mp->ma_keys->dk_lookup = lookdict_unicode;
        }
    }
}

#ifdef ENABLE_CINDERVM
static inline int Ci_Dict_CheckIncludingChecked(PyObject *x);
#endif

int
_PyDict_CheckConsistency(PyObject *op, int check_content)
{
#define CHECK(expr) \
    do { if (!(expr)) { _PyObject_ASSERT_FAILED_MSG(op, Py_STRINGIFY(expr)); } } while (0)

    assert(op != NULL);
#ifdef ENABLE_CINDERVM
    CHECK(Ci_Dict_CheckIncludingChecked(op));
#else
    CHECK(PyDict_Check(op));
#endif
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

#define new_values(size) PyMem_NEW(PyObject *, size)
#define free_values(values) PyMem_Free(values)

/* Consumes a reference to the keys object */
static PyObject *
new_dict(PyDictKeysObject *keys, PyObject **values)
{
    PyDictObject *mp;
    assert(keys != NULL);
    struct _Py_dict_state *state = get_dict_state();
#ifdef Py_DEBUG
    // new_dict() must not be called after _PyDict_Fini()
    assert(state->numfree != -1);
#endif
    if (state->numfree) {
        mp = state->free_list[--state->numfree];
        assert (mp != NULL);
        assert (Py_IS_TYPE(mp, &PyDict_Type));
        _Py_NewReference((PyObject *)mp);
    }
    else {
        mp = PyObject_GC_New(PyDictObject, &PyDict_Type);
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

/* Consumes a reference to the keys object */
static PyObject *
new_dict_with_shared_keys(PyDictKeysObject *keys)
{
    PyObject **values;
    Py_ssize_t i, size;

    size = USABLE_FRACTION(DK_SIZE(keys));
    values = new_values(size);
    if (values == NULL) {
        dictkeys_decref(keys);
        return PyErr_NoMemory();
    }
    for (i = 0; i < size; i++) {
        values[i] = NULL;
    }
    return new_dict(keys, values);
}


static PyDictKeysObject *
clone_combined_dict_keys(PyDictObject *orig)
{
    assert(PyDict_Check(orig));
    assert(Py_TYPE(orig)->tp_iter == (getiterfunc)dict_iter);
    assert(orig->ma_values == NULL);
    assert(orig->ma_keys->dk_refcnt == 1);

    Py_ssize_t keys_size = _PyDict_KeysSize(orig->ma_keys);
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

PyObject *
PyDict_New(void)
{
    dictkeys_incref(Py_EMPTY_KEYS);
    return new_dict(Py_EMPTY_KEYS, empty_values);
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
lookdict_with_lazy_imports(): general-purpose with deferred values, may return
DKIX_ERROR if (and only if) a comparison raises an exception. On
deferred object resolution errors, it may return DKIX_VALUE_ERROR.
lookdict_unicode() below is specialized to string keys, comparison of which can
never raise an exception; that function can never return DKIX_ERROR when key
is string.  Otherwise, it falls back to lookdict().
lookdict_with_lazy_imports_unicode(): specialized to Unicode string keys,
comparison of which can never raise an exception; that function can
never return DKIX_ERROR. On deferred object resolution errors, it may
return DKIX_VALUE_ERROR.
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

/* Specialized version for globals, with deferred objects */
static Py_ssize_t _Py_HOT_FUNCTION
lookdict_with_lazy_imports(PyDictObject *mp, PyObject *key,
                  Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports)
{
    Py_ssize_t ix;
    size_t i, mask, perturb;
    PyDictKeysObject *dk;
    PyDictKeyEntry *ep0;
    PyDictKeyEntry *ep;
    PyObject *value;
    PyObject *new_value = NULL;

top:
    dk = mp->ma_keys;
    ep0 = DK_ENTRIES(dk);
    mask = DK_MASK(dk);
    perturb = hash;
    i = (size_t)hash & mask;

    for (;;) {
        ix = dictkeys_get_index(dk, i);
        if (ix == DKIX_EMPTY) {
            *value_addr = NULL;
            return ix;
        }
        if (ix >= 0) {
            ep = &ep0[ix];
            assert(ep->me_key != NULL);
            if (ep->me_key == key) {
                goto found;
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
                        goto found;
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

found:
    value = ep->me_value;
    if (new_value || (resolve_lazy_imports && PyLazyImport_CheckExact(value))) {
        if (new_value == NULL) {
            PyObject *startkey = ep->me_key;
            Py_INCREF(startkey);
            Py_INCREF(value);
            new_value = PyImport_LoadLazyObject(value);
            Py_XINCREF(new_value);
            Py_DECREF(value);
            Py_DECREF(startkey);
            if (new_value == NULL) {
                *value_addr = NULL;
                return DKIX_VALUE_ERROR;
            }
            if (dk != mp->ma_keys || ep->me_key != startkey) {
                /* The dict has mutated, restart to update new_value */
                goto top;
            }
        }
        if (ep->me_value == new_value) {
            Py_DECREF(new_value);
        } else {
            Py_DECREF(ep->me_value);
            ep->me_value = new_value;
        }
        value = new_value;
    }
    *value_addr = value;
    return ix;
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

/* Specialized version for globals with string-only keys, with deferred objects */
static Py_ssize_t _Py_HOT_FUNCTION
lookdict_with_lazy_imports_unicode(PyDictObject *mp, PyObject *key,
                          Py_hash_t hash, PyObject **value_addr, int resolve_lazy_imports)
{
    Py_ssize_t ix;
    size_t i, mask, perturb;
    PyDictKeysObject *dk;
    PyDictKeyEntry *ep0;
    PyDictKeyEntry *ep;
    PyObject *value;
    PyObject *new_value = NULL;

    assert(mp->ma_values == NULL);
    /* Make sure this function doesn't have to handle non-unicode keys,
       including subclasses of str; e.g., one reason to subclass
       unicodes is to override __eq__, and for speed we don't cater to
       that here. */
    if (!PyUnicode_CheckExact(key)) {
        return lookdict_with_lazy_imports(mp, key, hash, value_addr, resolve_lazy_imports);
    }

top:
    dk = mp->ma_keys;
    ep0 = DK_ENTRIES(dk);
    mask = DK_MASK(dk);
    perturb = hash;
    i = (size_t)hash & mask;

    for (;;) {
        ix = dictkeys_get_index(mp->ma_keys, i);
            *value_addr = NULL;
        if (ix == DKIX_EMPTY) {
            return DKIX_EMPTY;
        }
        if (ix >= 0) {
            ep = &ep0[ix];
            assert(ep->me_key != NULL);
            assert(PyUnicode_CheckExact(ep->me_key));
            if (ep->me_key == key ||
                    (ep->me_hash == hash && unicode_eq(ep->me_key, key))) {
                goto found;
            }
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i*5 + perturb + 1);
    }
    Py_UNREACHABLE();

found:
    value = ep->me_value;
    if (new_value || (resolve_lazy_imports && PyLazyImport_CheckExact(value))) {
        if (new_value == NULL) {
            PyObject *startkey = ep->me_key;
            Py_INCREF(startkey);
            Py_INCREF(value);
            new_value = PyImport_LoadLazyObject(value);
            Py_XINCREF(new_value);
            Py_DECREF(value);
            Py_DECREF(startkey);
            if (new_value == NULL) {
                *value_addr = NULL;
                return DKIX_VALUE_ERROR;
            }
            if (dk != mp->ma_keys || ep->me_key != startkey) {
                /* The dict has mutated, restart to update new_value */
                goto top;
            }
        }
        if (ep->me_value == new_value) {
            Py_DECREF(new_value);
        } else {
            Py_DECREF(ep->me_value);
            ep->me_value = new_value;
        }
        value = new_value;
    }
    *value_addr = value;
    return ix;
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

int
_PyDict_HasOnlyStringKeys(PyObject *dict)
{
    Py_ssize_t pos = 0;
    PyObject *key, *value;
#ifdef ENABLE_CINDERVM
    assert(Ci_Dict_CheckIncludingChecked(dict));
#else
    assert(PyDict_Check(dict));
#endif
    /* Shortcut */
    if (((PyDictObject *)dict)->ma_keys->dk_lookup != lookdict &&
        ((PyDictObject *)dict)->ma_keys->dk_lookup != lookdict_with_lazy_imports)
        return 1;
    while (PyDict_NextKeepLazy(dict, &pos, &key, &value))
        if (!PyUnicode_Check(key))
            return 0;
    return 1;
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

void
_PyDict_MaybeUntrack(PyObject *op)
{
    PyDictObject *mp;
    PyObject *value;
    Py_ssize_t i, numentries;
    PyDictKeyEntry *ep0;

    if (!PyDict_CheckExact(op) || !_PyObject_GC_IS_TRACKED(op))
        return;

    mp = (PyDictObject *) op;
    ep0 = DK_ENTRIES(mp->ma_keys);
    numentries = mp->ma_keys->dk_nentries;
    if (_PyDict_HasSplitTable(mp)) {
        for (i = 0; i < numentries; i++) {
            if ((value = mp->ma_values[i]) == NULL)
                continue;
            if (_PyObject_GC_MAY_BE_TRACKED(value)) {
                assert(!_PyObject_GC_MAY_BE_TRACKED(ep0[i].me_key));
                return;
            }
        }
    }
    else {
        for (i = 0; i < numentries; i++) {
            if ((value = ep0[i].me_value) == NULL)
                continue;
            if (_PyObject_GC_MAY_BE_TRACKED(value) ||
                _PyObject_GC_MAY_BE_TRACKED(ep0[i].me_key))
                return;
        }
    }
    _PyObject_GC_UNTRACK(op);
}

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
        /* Insert into new slot. */
        assert(old_value == NULL);
        if (mp->ma_keys->dk_usable <= 0) {
            /* Need to resize. */
            if (insertion_resize(mp) < 0)
                goto Fail;
        }
        if (!PyUnicode_CheckExact(key)) {
            if (mp->ma_keys->dk_lookup == lookdict_with_lazy_imports_unicode) {
                dict_set_lookup(mp, lookdict_with_lazy_imports);
            }
            else if (mp->ma_keys->dk_lookup != lookdict) {
                dict_set_lookup(mp, lookdict);
            }
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
        mp->ma_keys->dk_usable--;
        mp->ma_keys->dk_nentries++;
        dict_modify_key(mp, key, value);
        assert(mp->ma_keys->dk_usable >= 0);
        ASSERT_CONSISTENT(mp);
        return 0;
    }

    if (old_value != value) {
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
        dict_modify_key(mp, key, value);
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

    PyDictKeysObject *newkeys = new_keys_object(PyDict_MINSIZE);
    if (newkeys == NULL) {
        return -1;
    }
    dictkeys_decref(Py_EMPTY_KEYS);
    mp->ma_keys = newkeys;
    mp->ma_values = NULL;

    if (!PyUnicode_CheckExact(key)) {
        if (mp->ma_keys->dk_lookup == lookdict_with_lazy_imports_unicode) {
            dict_set_lookup(mp, lookdict_with_lazy_imports);
        }
        else {
            dict_set_lookup(mp, lookdict);
        }
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
    dict_modify_key(mp, key, value);
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
    if (oldkeys->dk_lookup == lookdict ||
        oldkeys->dk_lookup == lookdict_with_lazy_imports ||
        oldkeys->dk_lookup == lookdict_with_lazy_imports_unicode)
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

/* Returns NULL if unable to split table.
 * A NULL return does not necessarily indicate an error */
PyDictKeysObject *
_PyDict_MakeKeysShared(PyObject *op)
{
    Py_ssize_t i;
    Py_ssize_t size;
    PyDictObject *mp = (PyDictObject *)op;

    if (!PyDict_CheckExact(op))
        return NULL;
    if (!_PyDict_HasSplitTable(mp)) {
        PyDictKeyEntry *ep0;
        PyObject **values;
        assert(mp->ma_keys->dk_refcnt == 1);
        if (mp->ma_keys->dk_lookup == lookdict ||
            mp->ma_keys->dk_lookup == lookdict_with_lazy_imports ||
            mp->ma_keys->dk_lookup == lookdict_with_lazy_imports_unicode) {
            return NULL;
        }
        else if (mp->ma_keys->dk_lookup == lookdict_unicode) {
            /* Remove dummy keys */
            if (dictresize(mp, DK_SIZE(mp->ma_keys)))
                return NULL;
        }
        assert(mp->ma_keys->dk_lookup == lookdict_unicode_nodummy);
        /* Copy values into a new array */
        ep0 = DK_ENTRIES(mp->ma_keys);
        size = USABLE_FRACTION(DK_SIZE(mp->ma_keys));
        values = new_values(size);
        if (values == NULL) {
            PyErr_SetString(PyExc_MemoryError,
                "Not enough memory to allocate new values array");
            return NULL;
        }
        for (i = 0; i < size; i++) {
            values[i] = ep0[i].me_value;
            ep0[i].me_value = NULL;
        }
        mp->ma_keys->dk_lookup = lookdict_split;
        mp->ma_values = values;
    }
    dictkeys_incref(mp->ma_keys);
    return mp->ma_keys;
}

PyObject *
_PyDict_NewPresized(Py_ssize_t minused)
{
    const Py_ssize_t max_presize = 128 * 1024;
    Py_ssize_t newsize;
    PyDictKeysObject *new_keys;

    if (minused <= USABLE_FRACTION(PyDict_MINSIZE)) {
        return PyDict_New();
    }
    /* There are no strict guarantee that returned dict can contain minused
     * items without resize.  So we create medium size dict instead of very
     * large dict or MemoryError.
     */
    if (minused > USABLE_FRACTION(max_presize)) {
        newsize = max_presize;
    }
    else {
        newsize = estimate_keysize(minused);
    }

    new_keys = new_keys_object(newsize);
    if (new_keys == NULL)
        return NULL;
    return new_dict(new_keys, NULL);
}

/* Note that, for historical reasons, PyDict_GetItem() suppresses all errors
 * that may occur (originally dicts supported only string keys, and exceptions
 * weren't possible).  So, while the original intent was that a NULL return
 * meant the key wasn't present, in reality it can mean that, or that an error
 * (suppressed) occurred while computing the key's hash, or that some error
 * (suppressed) occurred when comparing keys in the dict's internal probe
 * sequence.  A nasty example of the latter is when a Python-coded comparison
 * function hits a stack-depth error, which can cause this to return NULL
 * even if the key is present.
 */
PyObject *
PyDict_GetItem(PyObject *op, PyObject *key)
{
    if (!PyDict_Check(op)) {
        return NULL;
    }
    PyDictObject *mp = (PyDictObject *)op;

    Py_hash_t hash;
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1)
    {
        hash = PyObject_Hash(key);
        if (hash == -1) {
            PyErr_Clear();
            return NULL;
        }
    }

    PyThreadState *tstate = _PyThreadState_GET();
#ifdef Py_DEBUG
    // bpo-40839: Before Python 3.10, it was possible to call PyDict_GetItem()
    // with the GIL released.
    _Py_EnsureTstateNotNULL(tstate);
#endif

    /* Preserve the existing exception */
    PyObject *exc_type, *exc_value, *exc_tb;
    PyObject *value;
    Py_ssize_t ix;

    _PyErr_Fetch(tstate, &exc_type, &exc_value, &exc_tb);
    ix = (mp->ma_keys->dk_lookup)(mp, key, hash, &value, 1);

    if (ix == DKIX_VALUE_ERROR) {
        /* propagate value errors */
        Py_XDECREF(exc_type);
        Py_XDECREF(exc_value);
        Py_XDECREF(exc_tb);
    } else {
        /* Ignore any exception raised by the lookup */
        _PyErr_Restore(tstate, exc_type, exc_value, exc_tb);
    }

    if (ix < 0) {
        return NULL;
    }
    return value;
}

Py_ssize_t
_PyDict_GetItemHint(PyDictObject *mp, PyObject *key,
                    Py_ssize_t hint, PyObject **value)
{
    assert(*value == NULL);
    assert(PyDict_CheckExact((PyObject*)mp));
    assert(PyUnicode_CheckExact(key));

    if (hint >= 0 && hint < mp->ma_keys->dk_nentries) {
        PyObject *res = NULL;

        PyDictKeyEntry *ep = DK_ENTRIES(mp->ma_keys) + (size_t)hint;
        if (ep->me_key == key) {
            if (mp->ma_keys->dk_lookup == lookdict_split) {
                assert(mp->ma_values != NULL);
                res = mp->ma_values[(size_t)hint];
            }
            else {
                res = ep->me_value;
            }
            if (res != NULL) {
                *value = res;
                return hint;
            }
        }
    }

    Py_hash_t hash = ((PyASCIIObject *) key)->hash;
    if (hash == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1) {
            return -1;
        }
    }

    return (mp->ma_keys->dk_lookup)(mp, key, hash, value, 1);
}

PyObject *
PyDict_GetItemKeepLazy(PyObject *op, PyObject *key)
{
    Py_hash_t hash;
    Py_ssize_t ix;
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;

    if (!PyDict_Check(op))
        return NULL;
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1)
    {
        hash = PyObject_Hash(key);
        if (hash == -1) {
            return NULL;
        }
    }

    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 0);
    if (ix < 0) {
        return NULL;
    }
    return value;
}

/* Same as PyDict_GetItemWithError() but with hash supplied by caller.
   This returns NULL *with* an exception set if an exception occurred.
   It returns NULL *without* an exception set if the key wasn't present.
*/
PyObject *
_PyDict_GetItem_KnownHash(PyObject *op, PyObject *key, Py_hash_t hash)
{
    Py_ssize_t ix;
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;

    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }

    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 1);
    if (ix < 0) {
        return NULL;
    }
    return value;
}

/* Variant of PyDict_GetItem() that doesn't suppress exceptions.
   This returns NULL *with* an exception set if an exception occurred.
   It returns NULL *without* an exception set if the key wasn't present.
*/
PyObject *
PyDict_GetItemWithError(PyObject *op, PyObject *key)
{
    Py_ssize_t ix;
    Py_hash_t hash;
    PyDictObject*mp = (PyDictObject *)op;
    PyObject *value;

#ifdef ENABLE_CINDERVM
    if (!Ci_Dict_CheckIncludingChecked(op)) {
#else
    if (!PyDict_Check(op)) {
#endif
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1)
    {
        hash = PyObject_Hash(key);
        if (hash == -1) {
            return NULL;
        }
    }

    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 1);
    if (ix < 0)
        return NULL;
    return value;
}

PyObject *
_PyDict_GetItemIdWithError(PyObject *dp, struct _Py_Identifier *key)
{
    PyObject *kv;
    kv = _PyUnicode_FromId(key); /* borrowed */
    if (kv == NULL)
        return NULL;
    Py_hash_t hash = ((PyASCIIObject *) kv)->hash;
    assert (hash != -1);  /* interned strings have their hash value initialised */
    return _PyDict_GetItem_KnownHash(dp, kv, hash);
}

PyObject *
_PyDict_GetItemStringWithError(PyObject *v, const char *key)
{
    PyObject *kv, *rv;
    kv = PyUnicode_FromString(key);
    if (kv == NULL) {
        return NULL;
    }
    rv = PyDict_GetItemWithError(v, kv);
    Py_DECREF(kv);
    return rv;
}

/* Fast version of global value lookup (LOAD_GLOBAL).
 * Lookup in globals, then builtins.
 *
 * Raise an exception and return NULL if an error occurred (ex: computing the
 * key hash failed, key comparison failed, ...). Return NULL if the key doesn't
 * exist. Return the value if the key exists.
 */
PyObject *
_PyDict_LoadGlobal(PyDictObject *globals, PyDictObject *builtins, PyObject *key)
{
    Py_ssize_t ix;
    Py_hash_t hash;
    PyObject *value;

    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1)
    {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return NULL;
    }

    /* namespace 1: globals */
    ix = globals->ma_keys->dk_lookup(globals, key, hash, &value, 1);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return NULL;
    if (ix != DKIX_EMPTY && value != NULL)
        return value;

    /* namespace 2: builtins */
    ix = builtins->ma_keys->dk_lookup(builtins, key, hash, &value, 1);
    if (ix < 0)
        return NULL;
    return value;
}

/* Cinder-exposed internal setitem that elides type checks. */
inline int Ci_Dict_SetItemInternal(PyObject *op, PyObject *key, PyObject *value)
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

/* CAUTION: PyDict_SetItem() must guarantee that it won't resize the
 * dictionary if it's merely replacing the value for an existing key.
 * This means that it's safe to loop over a dictionary with PyDict_Next()
 * and occasionally replace a value -- but you can't insert new keys or
 * remove them.
 */
int
PyDict_SetItem(PyObject *op, PyObject *key, PyObject *value)
{
    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return Ci_Dict_SetItemInternal(op, key, value);
}


int
_PyDict_SetItem_KnownHash(PyObject *op, PyObject *key, PyObject *value,
                         Py_hash_t hash)
{
    PyDictObject *mp;

    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    assert(key);
    assert(value);
    assert(hash != -1);
    mp = (PyDictObject *)op;

    if (mp->ma_keys == Py_EMPTY_KEYS) {
        return insert_to_emptydict(mp, key, hash, value);
    }
    /* insertdict() handles any resizing that might be necessary */
    return insertdict(mp, key, hash, value);
}

static int
delitem_common(PyDictObject *mp, Py_hash_t hash, Py_ssize_t ix,
               PyObject *old_value)
{
    PyObject *old_key;
    PyDictKeyEntry *ep;

    Py_ssize_t hashpos = lookdict_index(mp->ma_keys, hash, ix);
    assert(hashpos >= 0);

    mp->ma_used--;
    ep = &DK_ENTRIES(mp->ma_keys)[ix];
    dictkeys_set_index(mp->ma_keys, hashpos, DKIX_DUMMY);
    ENSURE_ALLOWS_DELETIONS(mp);
    old_key = ep->me_key;
    ep->me_key = NULL;
    ep->me_value = NULL;
    dict_modify_key(mp, old_key, NULL);
    Py_DECREF(old_key);
    Py_DECREF(old_value);

    ASSERT_CONSISTENT(mp);
    return 0;
}

int
PyDict_DelItem(PyObject *op, PyObject *key)
{
    Py_hash_t hash;
    assert(key);
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
        if (hash == -1)
            return -1;
    }

    return _PyDict_DelItem_KnownHash(op, key, hash);
}

int
_PyDict_DelItem_KnownHash(PyObject *op, PyObject *key, Py_hash_t hash)
{
    Py_ssize_t ix;
    PyDictObject *mp;
    PyObject *old_value;

    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    assert(key);
    assert(hash != -1);
    mp = (PyDictObject *)op;
    ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 0);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return -1;
    if (ix == DKIX_EMPTY || old_value == NULL) {
        _PyErr_SetKeyError(key);
        return -1;
    }

    // Split table doesn't allow deletion.  Combine it.
    if (_PyDict_HasSplitTable(mp)) {
        if (dictresize(mp, DK_SIZE(mp->ma_keys))) {
            return -1;
        }
        ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 0);
        assert(ix >= 0);
    }

    return delitem_common(mp, hash, ix, old_value);
}

/* This function promises that the predicate -> deletion sequence is atomic
 * (i.e. protected by the GIL), assuming the predicate itself doesn't
 * release the GIL.
 */
int
_PyDict_DelItemIf(PyObject *op, PyObject *key,
                  int (*predicate)(PyObject *value))
{
    Py_ssize_t hashpos, ix;
    PyDictObject *mp;
    Py_hash_t hash;
    PyObject *old_value;
    int res;

    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    assert(key);
    hash = PyObject_Hash(key);
    if (hash == -1)
        return -1;
    mp = (PyDictObject *)op;
    ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 0);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return -1;
    if (ix == DKIX_EMPTY || old_value == NULL) {
        _PyErr_SetKeyError(key);
        return -1;
    }

    // Split table doesn't allow deletion.  Combine it.
    if (_PyDict_HasSplitTable(mp)) {
        if (dictresize(mp, DK_SIZE(mp->ma_keys))) {
            return -1;
        }
        ix = mp->ma_keys->dk_lookup(mp, key, hash, &old_value, 0);
        assert(ix >= 0);
    }

    res = predicate(old_value);
    if (res == -1)
        return -1;

    hashpos = lookdict_index(mp->ma_keys, hash, ix);
    assert(hashpos >= 0);

    if (res > 0)
        return delitem_common(mp, hashpos, ix, old_value);
    else
        return 0;
}


void
PyDict_Clear(PyObject *op)
{
    PyDictObject *mp;
    PyDictKeysObject *oldkeys;
    PyObject **oldvalues;
    Py_ssize_t i, n;

#ifdef ENABLE_CINDERVM
    if (!Ci_Dict_CheckIncludingChecked(op))
#else
    if (!PyDict_Check(op))
#endif
        return;
    mp = ((PyDictObject *)op);
    oldkeys = mp->ma_keys;
    oldvalues = mp->ma_values;
    if (oldvalues == empty_values)
        return;
    if (UNLIKELY(dict_is_watched(mp))) {
        mp->ma_version_tag = DICT_NEXT_WATCHED_VERSION();
        /* TODO(T113261295): Replace this with the generic hook once dict
         * watchers are upstreamed. */
#ifdef ENABLE_CINDERVM
        _PyJIT_NotifyDictClear((PyObject *)mp);
#endif
    } else {
        mp->ma_version_tag = DICT_NEXT_VERSION();
    }
    /* Empty the dict... */
    dictkeys_incref(Py_EMPTY_KEYS);
    mp->ma_keys = Py_EMPTY_KEYS;
    mp->ma_values = empty_values;
    mp->ma_used = 0;
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

int
dict_next(PyObject *op, Py_ssize_t *ppos, PyObject **pkey,
          PyObject **pvalue, Py_hash_t *phash, int resolve)
{
    Py_ssize_t i;
    PyDictObject *mp;
    PyDictKeysObject *dk;
    PyDictKeyEntry *ep;
    PyObject **value_ptr;
    PyObject *value;

#ifdef ENABLE_CINDERVM
    if (!Ci_Dict_CheckIncludingChecked(op))
#else
    if (!PyDict_Check(op))
#endif
        return 0;
    mp = (PyDictObject *)op;
    dk = mp->ma_keys;

    i = *ppos;
    if (mp->ma_values) {
        if (i < 0 || i >= mp->ma_used)
            return 0;
        /* values of split table is always dense */
        ep = &DK_ENTRIES(dk)[i];
        value_ptr = &mp->ma_values[i];
        value = *value_ptr;
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
        value_ptr = &ep->me_value;
        value = *value_ptr;
    }
    if (resolve && DICT_HAS_DEFERRED(mp)) {
        if (PyLazyImport_CheckExact(value)) {
            PyObject *key = ep->me_key;
            Py_INCREF(key);
            Py_INCREF(value);
            PyObject *new_value = PyImport_LoadLazyObject(value);
            if (new_value == NULL) {
                Py_DECREF(key);
                Py_DECREF(value);
                return 0;
            }
            if (dk != mp->ma_keys || ep->me_key != key) {
                /* TODO: for correctness, we should update here new_value
                    into the dict but we're only doing it if the dictionary
                    didn't change and bailing otherwise. */
                Py_DECREF(key);
                Py_DECREF(value);
                return 0;
            }
            if (*value_ptr != new_value) {
                Py_INCREF(new_value);
                Py_DECREF(*value_ptr);
                *value_ptr = new_value;
            }
            Py_DECREF(key);
            Py_DECREF(value);
            value = new_value;
        }
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

/* Internal version of PyDict_Next that returns a hash value in addition
 * to the key and value.
 * Return 1 on success, return 0 when the reached the end of the dictionary
 * (or if op is not a dictionary)
 */
int
_PyDict_Next(PyObject *op, Py_ssize_t *ppos, PyObject **pkey,
             PyObject **pvalue, Py_hash_t *phash)
{
    return dict_next(op, ppos, pkey, pvalue, phash, 1);
}

/*
 * Iterate over a dict.  Use like so:
 *
 *     Py_ssize_t i;
 *     PyObject *key, *value;
 *     i = 0;   # important!  i should not otherwise be changed by you
 *     while (PyDict_Next(yourdict, &i, &key, &value)) {
 *         Refer to borrowed references in key and value.
 *     }
 *
 * Return 1 on success, return 0 when the reached the end of the dictionary
 * (or if op is not a dictionary)
 *
 * CAUTION:  In general, it isn't safe to use PyDict_Next in a loop that
 * mutates the dict.  One exception:  it is safe if the loop merely changes
 * the values associated with the keys (but doesn't insert new keys or
 * delete keys), via PyDict_SetItem().
 */
int
PyDict_Next(PyObject *op, Py_ssize_t *ppos, PyObject **pkey, PyObject **pvalue)
{
    return dict_next(op, ppos, pkey, pvalue, NULL, 1);
}

int
PyDict_NextKeepLazy(PyObject *op, Py_ssize_t *ppos, PyObject **pkey, PyObject **pvalue)
{
    if (!PyDict_Check(op))
        return 0;
    return dict_next(op, ppos, pkey, pvalue, NULL, 0);
}

/* Internal version of dict.pop(). */
PyObject *
_PyDict_Pop_KnownHash(PyObject *dict, PyObject *key, Py_hash_t hash, PyObject *deflt)
{
    Py_ssize_t ix, hashpos;
    PyObject *old_value, *old_key;
    PyDictKeyEntry *ep;
    PyDictObject *mp;

#ifdef ENABLE_CINDERVM
    assert(Ci_Dict_CheckIncludingChecked(dict));
#else
    assert(PyDict_Check(dict));
#endif
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

    hashpos = lookdict_index(mp->ma_keys, hash, ix);
    assert(hashpos >= 0);
    assert(old_value != NULL);
    mp->ma_used--;
    dictkeys_set_index(mp->ma_keys, hashpos, DKIX_DUMMY);
    ep = &DK_ENTRIES(mp->ma_keys)[ix];
    ENSURE_ALLOWS_DELETIONS(mp);
    old_key = ep->me_key;
    ep->me_key = NULL;
    ep->me_value = NULL;
    dict_modify_key(mp, old_key, NULL);
    Py_DECREF(old_key);

    ASSERT_CONSISTENT(mp);
    return old_value;
}

PyObject *
_PyDict_Pop(PyObject *dict, PyObject *key, PyObject *deflt)
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
    return _PyDict_Pop_KnownHash(dict, key, hash, deflt);
}

/* Internal version of dict.from_keys().  It is subclass-friendly. */
PyObject *
_PyDict_FromKeys(PyObject *cls, PyObject *iterable, PyObject *value)
{
    PyObject *it;       /* iter(iterable) */
    PyObject *key;
    PyObject *d;
    int status;

    d = _PyObject_CallNoArg(cls);
    if (d == NULL)
        return NULL;

    if (PyDict_CheckExact(d) && ((PyDictObject *)d)->ma_used == 0) {
        if (PyDict_CheckExact(iterable)) {
            PyDictObject *mp = (PyDictObject *)d;
            PyObject *oldvalue;
            Py_ssize_t pos = 0;
            PyObject *key;
            Py_hash_t hash;

            if (dictresize(mp, estimate_keysize(PyDict_GET_SIZE(iterable)))) {
                Py_DECREF(d);
                return NULL;
            }

            while (_PyDict_Next(iterable, &pos, &key, &oldvalue, &hash)) {
                if (insertdict(mp, key, hash, value)) {
                    Py_DECREF(d);
                    return NULL;
                }
            }
            return d;
        }
        if (PyAnySet_CheckExact(iterable)) {
            PyDictObject *mp = (PyDictObject *)d;
            Py_ssize_t pos = 0;
            PyObject *key;
            Py_hash_t hash;

            if (dictresize(mp, estimate_keysize(PySet_GET_SIZE(iterable)))) {
                Py_DECREF(d);
                return NULL;
            }

            while (_PySet_NextEntry(iterable, &pos, &key, &hash)) {
                if (insertdict(mp, key, hash, value)) {
                    Py_DECREF(d);
                    return NULL;
                }
            }
            return d;
        }
    }

    it = PyObject_GetIter(iterable);
    if (it == NULL){
        Py_DECREF(d);
        return NULL;
    }

    if (PyDict_CheckExact(d)) {
        while ((key = PyIter_Next(it)) != NULL) {
            status = PyDict_SetItem(d, key, value);
            Py_DECREF(key);
            if (status < 0)
                goto Fail;
        }
    } else {
        while ((key = PyIter_Next(it)) != NULL) {
            status = PyObject_SetItem(d, key, value);
            Py_DECREF(key);
            if (status < 0)
                goto Fail;
        }
    }

    if (PyErr_Occurred())
        goto Fail;
    Py_DECREF(it);
    return d;

Fail:
    Py_DECREF(it);
    Py_DECREF(d);
    return NULL;
}

/* Methods */

static void
dict_dealloc(PyDictObject *mp)
{
    PyObject **values = mp->ma_values;
    PyDictKeysObject *keys = mp->ma_keys;
    Py_ssize_t i, n;

    if (UNLIKELY(dict_is_watched(mp))) {
        /* TODO(T113261295): Replace this with the generic hook once dict
         * watchers are upstreamed. */
#ifdef ENABLE_CINDERVM
        _PyJIT_NotifyDictUnwatch((PyObject *)mp);
#endif
    }

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
    while (PyDict_Next((PyObject *)mp, &i, &key, &value)) {
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

PyObject *Ci_dict_subscript(PyObject *mp, PyObject *key)
{
    return dict_subscript((PyDictObject *)mp, key);
}

static int
dict_ass_sub(PyDictObject *mp, PyObject *v, PyObject *w)
{
    if (w == NULL)
        return PyDict_DelItem((PyObject *)mp, v);
    else
        return PyDict_SetItem((PyObject *)mp, v, w);
}

static PyMappingMethods dict_as_mapping = {
    (lenfunc)dict_length, /*mp_length*/
    (binaryfunc)dict_subscript, /*mp_subscript*/
    (objobjargproc)dict_ass_sub, /*mp_ass_subscript*/
};

static PyObject *
dict_keys(PyDictObject *mp)
{
    PyObject *v;
    Py_ssize_t i, j;
    PyDictKeyEntry *ep;
    Py_ssize_t n, offset;
    PyObject **value_ptr;

  again:
    n = mp->ma_used;
    v = PyList_New(n);
    if (v == NULL)
        return NULL;
    if (n != mp->ma_used) {
        /* Durnit.  The allocations caused the dict to resize.
         * Just start over, this shouldn't normally happen.
         */
        Py_DECREF(v);
        goto again;
    }
    ep = DK_ENTRIES(mp->ma_keys);
    if (mp->ma_values) {
        value_ptr = mp->ma_values;
        offset = sizeof(PyObject *);
    }
    else {
        value_ptr = &ep[0].me_value;
        offset = sizeof(PyDictKeyEntry);
    }
    for (i = 0, j = 0; j < n; i++) {
        if (*value_ptr != NULL) {
            PyObject *key = ep[i].me_key;
            Py_INCREF(key);
            PyList_SET_ITEM(v, j, key);
            j++;
        }
        value_ptr = (PyObject **)(((char *)value_ptr) + offset);
    }
    assert(j == n);
    return v;
}

static PyObject *
dict_values(PyDictObject *mp)
{
    PyObject *v;
    Py_ssize_t i, j;
    Py_ssize_t n, offset;
    PyDictKeysObject *dk;
    PyDictKeyEntry *ep0;
    PyObject **value_ptr;

  again:
    n = mp->ma_used;
    v = PyList_New(n);
    if (v == NULL)
        return NULL;
    if (n != mp->ma_used) {
        /* Durnit.  The allocations caused the dict to resize.
         * Just start over, this shouldn't normally happen.
         */
        Py_DECREF(v);
        goto again;
    }
    dk = mp->ma_keys;
    ep0 = DK_ENTRIES(dk);
    if (mp->ma_values) {
        value_ptr = mp->ma_values;
        offset = sizeof(PyObject *);
    }
    else {
        value_ptr = &ep0[0].me_value;
        offset = sizeof(PyDictKeyEntry);
    }
    if (DICT_HAS_DEFERRED(mp)
        && _PyDict_LoadDeferred(mp) != 0) {
        return NULL;
    }
    for (i = 0, j = 0; j < n; i++) {
        PyObject *value = *value_ptr;
        if (value != NULL) {
            Py_INCREF(value);
            PyList_SET_ITEM(v, j, value);
            j++;
        }
        value_ptr = (PyObject **)(((char *)value_ptr) + offset);
    }
    assert(j == n);
    return v;
}

static PyObject *
dict_items(PyDictObject *mp)
{
    PyObject *v;
    Py_ssize_t i, j;
    Py_ssize_t n, offset;
    PyDictKeysObject *dk;
    PyObject *item;
    PyDictKeyEntry *ep0;
    PyObject **value_ptr;

    /* Preallocate the list of tuples, to avoid allocations during
     * the loop over the items, which could trigger GC, which
     * could resize the dict. :-(
     */
  again:
    n = mp->ma_used;
    v = PyList_New(n);
    if (v == NULL)
        return NULL;
    for (i = 0; i < n; i++) {
        item = PyTuple_New(2);
        if (item == NULL) {
            Py_DECREF(v);
            return NULL;
        }
        PyList_SET_ITEM(v, i, item);
    }
    if (n != mp->ma_used) {
        /* Durnit.  The allocations caused the dict to resize.
         * Just start over, this shouldn't normally happen.
         */
        Py_DECREF(v);
        goto again;
    }
    /* Nothing we do below makes any function calls. */
    dk = mp->ma_keys;
    ep0 = DK_ENTRIES(dk);
    if (mp->ma_values) {
        value_ptr = mp->ma_values;
        offset = sizeof(PyObject *);
    }
    else {
        value_ptr = &ep0[0].me_value;
        offset = sizeof(PyDictKeyEntry);
    }
    if (DICT_HAS_DEFERRED(mp)
        && _PyDict_LoadDeferred(mp) != 0) {
        return NULL;
    }
    for (i = 0, j = 0; j < n; i++) {
        PyObject *value = *value_ptr;
        if (value != NULL) {
            PyDictKeyEntry *ep = &ep0[i];
            PyObject *key = ep->me_key;
            Py_INCREF(key);
            Py_INCREF(value);
            item = PyList_GET_ITEM(v, j);
            PyTuple_SET_ITEM(item, 0, key);
            PyTuple_SET_ITEM(item, 1, value);
            j++;
        }
        value_ptr = (PyObject **)(((char *)value_ptr) + offset);
    }
    assert(j == n);
    return v;
}

static PyObject *
dict_deferred_items(PyDictObject *mp)
{
    PyObject *v;
    PyObject *s;
    Py_ssize_t i, j, k;
    Py_ssize_t n, offset;
    PyDictKeysObject *dk;
    PyObject *item, *key;
    PyDictKeyEntry *ep0;
    PyObject **value_ptr;

    assert(DICT_HAS_DEFERRED(mp));

  again:
    n = mp->ma_used;
    v = PyList_New(n);
    if (v == NULL)
        return NULL;
    for (i = 0; i < n; i++) {
        item = PyTuple_New(2);
        if (item == NULL) {
            Py_DECREF(v);
            return NULL;
        }
        PyList_SET_ITEM(v, i, item);
    }
    if (n != mp->ma_used) {
        /* Durnit.  The allocations caused the dict to resize.
         * Just start over, this shouldn't normally happen.
         */
        Py_DECREF(v);
        goto again;
    }
    dk = mp->ma_keys;
    ep0 = DK_ENTRIES(dk);
    if (mp->ma_values) {
        value_ptr = mp->ma_values;
        offset = sizeof(PyObject *);
    }
    else {
        value_ptr = &ep0[0].me_value;
        offset = sizeof(PyDictKeyEntry);
    }
    for (i = 0, j = 0, k = 0; j < n; i++) {
        PyObject *value = *value_ptr;
        if (value != NULL) {
            if (PyLazyImport_CheckExact(value)) {
                PyDictKeyEntry *ep = &ep0[i];
                key = ep->me_key;
                PyObject *new_value = ((PyLazyImport *)value)->lz_obj;
                if (new_value != NULL) {
                    Py_INCREF(new_value);
                    Py_DECREF(*value_ptr);
                    *value_ptr = new_value;
                } else {
                    Py_INCREF(key);
                    Py_INCREF(value);
                    item = PyList_GET_ITEM(v, k);
                    PyTuple_SET_ITEM(item, 0, key);
                    PyTuple_SET_ITEM(item, 1, value);
                    k++;
                }
            }
            j++;
        }
        value_ptr = (PyObject **)(((char *)value_ptr) + offset);
    }
    s = PyList_GetSlice(v, 0, k);
    Py_DECREF(v);
    return s;
}

int
_PyDict_LoadDeferred(PyDictObject *dp)
{
    PyObject *v;
    PyObject *item, *key, *value;
    PyObject *new_value;
    Py_ssize_t i, n = 0;
    Py_ssize_t nentries;

    assert(DICT_HAS_DEFERRED(dp));

top:
    nentries = dp->ma_keys->dk_nentries;

    /* try importing as many deferredd objects as possible */
    v = dict_deferred_items(dp);
    if (v == NULL) {
        return -1;
    }
    n = PyList_Size(v);
    for (i = 0; i < n; i++) {
        item = PyList_GET_ITEM(v, i);
        value = PyTuple_GET_ITEM(item, 1);
        assert(PyLazyImport_CheckExact(value));
        new_value = PyImport_LoadLazyObject(value);
        if (new_value == NULL) {
            return -1;
        }
        key = PyTuple_GET_ITEM(item, 0);
        PyDict_SetItem((PyObject *)dp, key, new_value);
    }
    Py_DECREF(v);

    if (nentries != dp->ma_keys->dk_nentries) {
        /* The dict has mutated, try again */
        goto top;
    }

    /* check to see if we're done with the deferred objects in the dictionary */
    v = dict_deferred_items(dp);
    if (v == NULL) {
        return -1;
    }
    n = PyList_Size(v);
    if (n == 0) {
        _PyDict_UnsetHasDeferredObjects((PyObject *)dp);
    }
    return n;
}

/*[clinic input]
@classmethod
dict.fromkeys
    iterable: object
    value: object=None
    /

Create a new dictionary with keys from iterable and values set to value.
[clinic start generated code]*/

static PyObject *
dict_fromkeys_impl(PyTypeObject *type, PyObject *iterable, PyObject *value)
/*[clinic end generated code: output=8fb98e4b10384999 input=382ba4855d0f74c3]*/
{
    return _PyDict_FromKeys((PyObject *)type, iterable, value);
}

/* Single-arg dict update; used by dict_update_common and operators. */
static int
dict_update_arg(PyObject *self, PyObject *arg)
{
    if (PyDict_CheckExact(arg)) {
        return PyDict_Merge(self, arg, 1);
    }
    _Py_IDENTIFIER(keys);
    PyObject *func;
    if (_PyObject_LookupAttrId(arg, &PyId_keys, &func) < 0) {
        return -1;
    }
    if (func != NULL) {
        Py_DECREF(func);
        return PyDict_Merge(self, arg, 1);
    }
    return PyDict_MergeFromSeq2(self, arg, 1);
}

static int
dict_update_common(PyObject *self, PyObject *args, PyObject *kwds,
                   const char *methname)
{
    PyObject *arg = NULL;
    int result = 0;

    if (!PyArg_UnpackTuple(args, methname, 0, 1, &arg)) {
        result = -1;
    }
    else if (arg != NULL) {
        result = dict_update_arg(self, arg);
    }

    if (result == 0 && kwds != NULL) {
        if (PyArg_ValidateKeywordArguments(kwds))
            result = PyDict_Merge(self, kwds, 1);
        else
            result = -1;
    }
    return result;
}

/* Note: dict.update() uses the METH_VARARGS|METH_KEYWORDS calling convention.
   Using METH_FASTCALL|METH_KEYWORDS would make dict.update(**dict2) calls
   slower, see the issue #29312. */
static PyObject *
dict_update(PyObject *self, PyObject *args, PyObject *kwds)
{
    if (dict_update_common(self, args, kwds, "update") != -1)
        Py_RETURN_NONE;
    return NULL;
}

/* Update unconditionally replaces existing items.
   Merge has a 3rd argument 'override'; if set, it acts like Update,
   otherwise it leaves existing items unchanged.

   PyDict_{Update,Merge} update/merge from a mapping object.

   PyDict_MergeFromSeq2 updates/merges from any iterable object
   producing iterable objects of length 2.
*/

int
PyDict_MergeFromSeq2(PyObject *d, PyObject *seq2, int override)
{
    PyObject *it;       /* iter(seq2) */
    Py_ssize_t i;       /* index into seq2 of current element */
    PyObject *item;     /* seq2[i] */
    PyObject *fast;     /* item as a 2-tuple or 2-list */

    assert(d != NULL);
    assert(PyDict_Check(d));
    assert(seq2 != NULL);

    it = PyObject_GetIter(seq2);
    if (it == NULL)
        return -1;

    for (i = 0; ; ++i) {
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
                         i, n);
            goto Fail;
        }

        /* Update/merge with this (key, value) pair. */
        key = PySequence_Fast_GET_ITEM(fast, 0);
        value = PySequence_Fast_GET_ITEM(fast, 1);
        Py_INCREF(key);
        Py_INCREF(value);
        if (override) {
            if (PyDict_SetItem(d, key, value) < 0) {
                Py_DECREF(key);
                Py_DECREF(value);
                goto Fail;
            }
        }
        else {
            if (PyDict_SetDefault(d, key, value) == NULL) {
                Py_DECREF(key);
                Py_DECREF(value);
                goto Fail;
            }
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
#ifdef ENABLE_CINDERVM
    if (a == NULL || !Ci_Dict_CheckIncludingChecked(a) || b == NULL) {
#else
    if (a == NULL || !PyDict_Check(a) || b == NULL) {
#endif
        PyErr_BadInternalCall();
        return -1;
    }
    mp = (PyDictObject*)a;
    if (PyDict_Check(b) && (Py_TYPE(b)->tp_iter == (getiterfunc)dict_iter)) {
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
                if (UNLIKELY(dict_is_watched(mp))) {
                  /* TODO(T113261295): Replace this with the generic hook once
                   * dict watchers are upstreamed. */
#ifdef ENABLE_CINDERVM
                    _PyJIT_NotifyDictUnwatch((PyObject *)mp);
#endif
                }
                mp->ma_version_tag = DICT_NEXT_VERSION();
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
        if (PyDict_Check(b) && _PyDict_HasDeferredObjects(b)) {
            _PyDict_SetHasDeferredObjects(a);
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

int
PyDict_Update(PyObject *a, PyObject *b)
{
    return dict_merge(a, b, 1);
}

int
PyDict_Merge(PyObject *a, PyObject *b, int override)
{
    /* XXX Deprecate override not in (0, 1). */
    return dict_merge(a, b, override != 0);
}

int
_PyDict_MergeEx(PyObject *a, PyObject *b, int override)
{
    return dict_merge(a, b, override);
}

static PyObject *
dict_copy(PyDictObject *mp, PyObject *Py_UNUSED(ignored))
{
    return PyDict_Copy((PyObject*)mp);
}

PyObject *
PyDict_Copy(PyObject *o)
{
    PyObject *copy;
    PyDictObject *mp;
    Py_ssize_t i, n;

    if (o == NULL || !PyDict_Check(o)) {
        PyErr_BadInternalCall();
        return NULL;
    }

    mp = (PyDictObject *)o;
    if (mp->ma_used == 0) {
        /* The dict is empty; just return a new dict. */
        return PyDict_New();
    }

    if (_PyDict_HasSplitTable(mp)) {
        PyDictObject *split_copy;
        Py_ssize_t size = USABLE_FRACTION(DK_SIZE(mp->ma_keys));
        PyObject **newvalues;
        newvalues = new_values(size);
        if (newvalues == NULL)
            return PyErr_NoMemory();
        split_copy = PyObject_GC_New(PyDictObject, &PyDict_Type);
        if (split_copy == NULL) {
            free_values(newvalues);
            return NULL;
        }
        split_copy->ma_values = newvalues;
        split_copy->ma_keys = mp->ma_keys;
        split_copy->ma_used = mp->ma_used;
        split_copy->ma_version_tag = DICT_NEXT_VERSION();
        dictkeys_incref(mp->ma_keys);
        for (i = 0, n = size; i < n; i++) {
            PyObject *value = mp->ma_values[i];
            Py_XINCREF(value);
            split_copy->ma_values[i] = value;
        }
        if (_PyObject_GC_IS_TRACKED(mp))
            _PyObject_GC_TRACK(split_copy);
        return (PyObject *)split_copy;
    }

    if (Py_TYPE(mp)->tp_iter == (getiterfunc)dict_iter &&
            mp->ma_values == NULL &&
            (mp->ma_used >= (mp->ma_keys->dk_nentries * 2) / 3))
    {
        /* Use fast-copy if:

           (1) type(mp) doesn't override tp_iter; and

           (2) 'mp' is not a split-dict; and

           (3) if 'mp' is non-compact ('del' operation does not resize dicts),
               do fast-copy only if it has at most 1/3 non-used keys.

           The last condition (3) is important to guard against a pathological
           case when a large dict is almost emptied with multiple del/pop
           operations and copied after that.  In cases like this, we defer to
           PyDict_Merge, which produces a compacted copy.
        */
        PyDictKeysObject *keys = clone_combined_dict_keys(mp);
        if (keys == NULL) {
            return NULL;
        }
        PyDictObject *new = (PyDictObject *)new_dict(keys, NULL);
        if (new == NULL) {
            /* In case of an error, `new_dict()` takes care of
               cleaning up `keys`. */
            return NULL;
        }

        new->ma_used = mp->ma_used;
        ASSERT_CONSISTENT(new);
        if (_PyObject_GC_IS_TRACKED(mp)) {
            /* Maintain tracking. */
            _PyObject_GC_TRACK(new);
        }

        return (PyObject *)new;
    }

    copy = PyDict_New();
    if (copy == NULL)
        return NULL;
    if (dict_merge(copy, o, 1) == 0)
        return copy;
    Py_DECREF(copy);
    return NULL;
}

Py_ssize_t
PyDict_Size(PyObject *mp)
{
    if (mp == NULL || !PyDict_Check(mp)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return ((PyDictObject *)mp)->ma_used;
}

PyObject *
PyDict_Keys(PyObject *mp)
{
    if (mp == NULL || !PyDict_Check(mp)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return dict_keys((PyDictObject *)mp);
}

PyObject *
PyDict_Values(PyObject *mp)
{
    if (mp == NULL || !PyDict_Check(mp)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return dict_values((PyDictObject *)mp);
}

PyObject *
PyDict_Items(PyObject *mp)
{
    if (mp == NULL || !PyDict_Check(mp)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return dict_items((PyDictObject *)mp);
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
dict_richcompare(PyObject *v, PyObject *w, int op)
{
    int cmp;
    PyObject *res;

    if (!PyDict_Check(v) || !PyDict_Check(w)) {
        res = Py_NotImplemented;
    }
    else if (op == Py_EQ || op == Py_NE) {
        cmp = dict_equal((PyDictObject *)v, (PyDictObject *)w);
        if (cmp < 0)
            return NULL;
        res = (cmp == (op == Py_EQ)) ? Py_True : Py_False;
    }
    else
        res = Py_NotImplemented;
    Py_INCREF(res);
    return res;
}

/*[clinic input]

@coexist
dict.__contains__

  key: object
  /

True if the dictionary has the specified key, else False.
[clinic start generated code]*/

static PyObject *
dict___contains__(PyDictObject *self, PyObject *key)
/*[clinic end generated code: output=a3d03db709ed6e6b input=fe1cb42ad831e820]*/
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

/*[clinic input]
dict.get

    key: object
    default: object = None
    /

Return the value for key if key is in the dictionary, else default.
[clinic start generated code]*/

static PyObject *
dict_get_impl(PyDictObject *self, PyObject *key, PyObject *default_value)
/*[clinic end generated code: output=bba707729dee05bf input=279ddb5790b6b107]*/
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
PyDict_SetDefault(PyObject *d, PyObject *key, PyObject *defaultobj)
{
    PyDictObject *mp = (PyDictObject *)d;
    PyObject *value;
    Py_hash_t hash;

#ifdef ENABLE_CINDERVM
    if (!Ci_Dict_CheckIncludingChecked(d)) {
#else
    if (!PyDict_Check(d)) {
#endif
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
        mp->ma_keys->dk_usable--;
        mp->ma_keys->dk_nentries++;
        dict_modify_key(mp, key, value);
        assert(mp->ma_keys->dk_usable >= 0);
    }
    else if (value == NULL) {
        value = defaultobj;
        assert(_PyDict_HasSplitTable(mp));
        assert(ix == mp->ma_used);
        Py_INCREF(value);
        MAINTAIN_TRACKING(mp, key, value);
        mp->ma_values[ix] = value;
        mp->ma_used++;
        dict_modify_key(mp, key, value);
    }

    ASSERT_CONSISTENT(mp);
    return value;
}

/*[clinic input]
dict.setdefault

    key: object
    default: object = None
    /

Insert key with a value of default if key is not in the dictionary.

Return the value for key if key is in the dictionary, else default.
[clinic start generated code]*/

static PyObject *
dict_setdefault_impl(PyDictObject *self, PyObject *key,
                     PyObject *default_value)
/*[clinic end generated code: output=f8c1101ebf69e220 input=0f063756e815fd9d]*/
{
    PyObject *val;

    val = PyDict_SetDefault((PyObject *)self, key, default_value);
    Py_XINCREF(val);
    return val;
}

static PyObject *
dict_clear(PyDictObject *mp, PyObject *Py_UNUSED(ignored))
{
    PyDict_Clear((PyObject *)mp);
    Py_RETURN_NONE;
}

/*[clinic input]
dict.pop

    key: object
    default: object = NULL
    /

D.pop(k[,d]) -> v, remove specified key and return the corresponding value.

If the key is not found, return the default if given; otherwise,
raise a KeyError.
[clinic start generated code]*/

static PyObject *
dict_pop_impl(PyDictObject *self, PyObject *key, PyObject *default_value)
/*[clinic end generated code: output=3abb47b89f24c21c input=e221baa01044c44c]*/
{
    return _PyDict_Pop((PyObject*)self, key, default_value);
}

/*[clinic input]
dict.popitem

Remove and return a (key, value) pair as a 2-tuple.

Pairs are returned in LIFO (last-in, first-out) order.
Raises KeyError if the dict is empty.
[clinic start generated code]*/

static PyObject *
dict_popitem_impl(PyDictObject *self)
/*[clinic end generated code: output=e65fcb04420d230d input=1c38a49f21f64941]*/
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
    j = lookdict_index(self->ma_keys, ep->me_hash, i);
    assert(j >= 0);
    assert(dictkeys_get_index(self->ma_keys, j) == i);
    dictkeys_set_index(self->ma_keys, j, DKIX_DUMMY);

    PyObject *old_key = ep->me_key;
    PyObject *old_value = ep->me_value;
    ep->me_key = NULL;
    ep->me_value = NULL;
    /* We can't dk_usable++ since there is DKIX_DUMMY in indices */
    self->ma_keys->dk_nentries = i;
    self->ma_used--;
    dict_modify_key(self, old_key, NULL);
    ASSERT_CONSISTENT(self);

    if (DICT_HAS_DEFERRED(self)
        && PyLazyImport_CheckExact(old_value)) {
        PyObject *new_value = PyImport_LoadLazyObject(old_value);
        Py_XINCREF(new_value);
        Py_DECREF(old_value);
        if (new_value == NULL) {
            Py_DECREF(old_key);
            Py_DECREF(res);
            return NULL;
        }
        old_value = new_value;
    }

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

    if (keys->dk_lookup == lookdict || keys->dk_lookup == lookdict_with_lazy_imports) {
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
    PyDict_Clear(op);
    return 0;
}

static PyObject *dictiter_new(PyDictObject *, PyTypeObject *);

Py_ssize_t
_PyDict_SizeOf(PyDictObject *mp)
{
    Py_ssize_t size, usable, res;

    size = DK_SIZE(mp->ma_keys);
    usable = USABLE_FRACTION(size);

    res = _PyObject_SIZE(Py_TYPE(mp));
    if (mp->ma_values)
        res += usable * sizeof(PyObject*);
    /* If the dictionary is split, the keys portion is accounted-for
       in the type object. */
    if (mp->ma_keys->dk_refcnt == 1)
        res += (sizeof(PyDictKeysObject)
                + DK_IXSIZE(mp->ma_keys) * size
                + sizeof(PyDictKeyEntry) * usable);
    return res;
}

Py_ssize_t
_PyDict_KeysSize(PyDictKeysObject *keys)
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

static PyObject *
dict_or(PyObject *self, PyObject *other)
{
    if (!PyDict_Check(self) || !PyDict_Check(other)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    PyObject *new = PyDict_Copy(self);
    if (new == NULL) {
        return NULL;
    }
    if (dict_update_arg(new, other)) {
        Py_DECREF(new);
        return NULL;
    }
    return new;
}

static PyObject *
dict_ior(PyObject *self, PyObject *other)
{
    if (dict_update_arg(self, other)) {
        return NULL;
    }
    Py_INCREF(self);
    return self;
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

static PyMethodDef mapp_methods[] = {
    DICT___CONTAINS___METHODDEF
    {"__getitem__", (PyCFunction)(void(*)(void))dict_subscript,        METH_O | METH_COEXIST,
     getitem__doc__},
    {"__sizeof__",      (PyCFunction)(void(*)(void))dict_sizeof,       METH_NOARGS,
     sizeof__doc__},
    DICT_GET_METHODDEF
    DICT_SETDEFAULT_METHODDEF
    DICT_POP_METHODDEF
    DICT_POPITEM_METHODDEF
    {"keys",            dictkeys_new,                   METH_NOARGS,
    keys__doc__},
    {"items",           dictitems_new,                  METH_NOARGS,
    items__doc__},
    {"values",          dictvalues_new,                 METH_NOARGS,
    values__doc__},
    {"update",          (PyCFunction)(void(*)(void))dict_update, METH_VARARGS | METH_KEYWORDS,
     update__doc__},
    DICT_FROMKEYS_METHODDEF
    {"clear",           (PyCFunction)dict_clear,        METH_NOARGS,
     clear__doc__},
    {"copy",            (PyCFunction)dict_copy,         METH_NOARGS,
     copy__doc__},
    DICT___REVERSED___METHODDEF
    {"__class_getitem__", (PyCFunction)Py_GenericAlias, METH_O|METH_CLASS, PyDoc_STR("See PEP 585")},
    {NULL,              NULL}   /* sentinel */
};


/* Return 1 if `name` is a lazy import object in dict `mp`, 0 if not, and -1 on error. */
int
PyDict_IsLazyImport(PyObject *mp, PyObject *name)
{
    PyObject *value = PyDict_GetItemKeepLazy(mp, name);
    if (value == NULL) {
        return -1;
    }
    if (PyLazyImport_CheckExact(value)) {
        return 1;
    }
    return 0;
}


/* Return 1 if `key` is in dict `op`, 0 if not, and -1 on error. */
int
PyDict_Contains(PyObject *op, PyObject *key)
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

/* Internal version of PyDict_Contains used when the hash value is already known */
int
_PyDict_Contains_KnownHash(PyObject *op, PyObject *key, Py_hash_t hash)
{
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;
    Py_ssize_t ix;

    ix = mp->ma_keys->dk_lookup(mp, key, hash, &value, 0);
    if (ix == DKIX_ERROR || ix == DKIX_VALUE_ERROR)
        return -1;
    return (ix != DKIX_EMPTY && value != NULL);
}

int
_PyDict_ContainsId(PyObject *op, struct _Py_Identifier *key)
{
    PyObject *kv = _PyUnicode_FromId(key); /* borrowed */
    if (kv == NULL) {
        return -1;
    }
    return PyDict_Contains(op, kv);
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
    PyDict_Contains,            /* sq_contains */
    0,                          /* sq_inplace_concat */
    0,                          /* sq_inplace_repeat */
};

static PyNumberMethods dict_as_number = {
    .nb_or = dict_or,
    .nb_inplace_or = dict_ior,
};

static PyObject *
dict_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self;
    PyDictObject *d;

    assert(type != NULL && type->tp_alloc != NULL);
    self = type->tp_alloc(type, 0);
    if (self == NULL)
        return NULL;
    d = (PyDictObject *)self;

    /* The object has been implicitly tracked by tp_alloc */
    if (type == &PyDict_Type) {
        _PyObject_GC_UNTRACK(d);
    }

    d->ma_used = 0;
    d->ma_version_tag = DICT_NEXT_VERSION();
    dictkeys_incref(Py_EMPTY_KEYS);
    d->ma_keys = Py_EMPTY_KEYS;
    d->ma_values = empty_values;
    ASSERT_CONSISTENT(d);
    return self;
}

static int
dict_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    return dict_update_common(self, args, kwds, "dict");
}

static PyObject *
dict_vectorcall(PyObject *type, PyObject * const*args,
                size_t nargsf, PyObject *kwnames)
{
    assert(PyType_Check(type));
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (!_PyArg_CheckPositional("dict", nargs, 0, 1)) {
        return NULL;
    }

    PyObject *self = dict_new((PyTypeObject *)type, NULL, NULL);
    if (self == NULL) {
        return NULL;
    }
    if (nargs == 1) {
        if (dict_update_arg(self, args[0]) < 0) {
            Py_DECREF(self);
            return NULL;
        }
        args++;
    }
    if (kwnames != NULL) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(kwnames); i++) {
            if (PyDict_SetItem(self, PyTuple_GET_ITEM(kwnames, i), args[i]) < 0) {
                Py_DECREF(self);
                return NULL;
            }
        }
    }
    return self;
}

static PyObject *
dict_iter(PyDictObject *dict)
{
    return dictiter_new(dict, &PyDictIterKey_Type);
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

PyTypeObject PyDict_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "dict",
    sizeof(PyDictObject),
    0,
    (destructor)dict_dealloc,                   /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)dict_repr,                        /* tp_repr */
    &dict_as_number,                            /* tp_as_number */
    &dict_as_sequence,                          /* tp_as_sequence */
    &dict_as_mapping,                           /* tp_as_mapping */
    PyObject_HashNotImplemented,                /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_DICT_SUBCLASS |
        _Py_TPFLAGS_MATCH_SELF | Py_TPFLAGS_MAPPING,  /* tp_flags */
    dictionary_doc,                             /* tp_doc */
    dict_traverse,                              /* tp_traverse */
    dict_tp_clear,                              /* tp_clear */
    dict_richcompare,                           /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    (getiterfunc)dict_iter,                     /* tp_iter */
    0,                                          /* tp_iternext */
    mapp_methods,                               /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    dict_init,                                  /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    dict_new,                                   /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
    .tp_vectorcall = dict_vectorcall,
};

/* For backward compatibility with old dictionary interface */

PyObject *
PyDict_GetItemString(PyObject *v, const char *key)
{
    PyObject *kv, *rv;
    kv = PyUnicode_FromString(key);
    if (kv == NULL) {
        PyErr_Clear();
        return NULL;
    }
    rv = PyDict_GetItem(v, kv);
    Py_DECREF(kv);
    return rv;
}

int
_PyDict_SetItemId(PyObject *v, struct _Py_Identifier *key, PyObject *item)
{
    PyObject *kv;
    kv = _PyUnicode_FromId(key); /* borrowed */
    if (kv == NULL)
        return -1;
    return PyDict_SetItem(v, kv, item);
}

int
PyDict_SetItemString(PyObject *v, const char *key, PyObject *item)
{
    PyObject *kv;
    int err;
    kv = PyUnicode_FromString(key);
    if (kv == NULL)
        return -1;
    PyUnicode_InternInPlace(&kv); /* XXX Should we really? */
    err = PyDict_SetItem(v, kv, item);
    Py_DECREF(kv);
    return err;
}

int
_PyDict_DelItemId(PyObject *v, _Py_Identifier *key)
{
    PyObject *kv = _PyUnicode_FromId(key); /* borrowed */
    if (kv == NULL)
        return -1;
    return PyDict_DelItem(v, kv);
}

int
PyDict_DelItemString(PyObject *v, const char *key)
{
    PyObject *kv;
    int err;
    kv = PyUnicode_FromString(key);
    if (kv == NULL)
        return -1;
    err = PyDict_DelItem(v, kv);
    Py_DECREF(kv);
    return err;
}

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
    if (itertype == &PyDictRevIterKey_Type ||
         itertype == &PyDictRevIterItem_Type ||
         itertype == &PyDictRevIterValue_Type) {
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
    if (itertype == &PyDictIterItem_Type ||
        itertype == &PyDictRevIterItem_Type) {
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
#ifdef ENABLE_CINDERVM
    assert (Ci_Dict_CheckIncludingChecked((PyObject*) d));
#else
    assert (PyDict_Check((PyObject*) d));
#endif

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

PyTypeObject PyDictIterKey_Type = {
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
#ifdef ENABLE_CINDERVM
    assert (Ci_Dict_CheckIncludingChecked((PyObject *) d));
#else
    assert (PyDict_Check((PyObject *) d));
#endif

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
    if (DICT_HAS_DEFERRED(d)
        && PyLazyImport_CheckExact(value)) {
        PyObject *key = entry_ptr->me_key;
        Py_INCREF(key);
        PyObject *new_value = PyImport_LoadLazyObject(value);
        if (new_value == NULL) {
            Py_DECREF(key);
            Py_DECREF(value);
            goto fail;
        }
        if (dk != d->ma_keys || entry_ptr->me_key != key) {
            /* TODO: for correctness, we should update here new_value
                into the dict but we're only doing it if the dictionary
                didn't change and bailing otherwise. */
            Py_DECREF(key);
            Py_DECREF(value);
            return NULL;
        }
        if (*value_ptr != new_value) {
            Py_INCREF(new_value);
            Py_DECREF(*value_ptr);
            *value_ptr = new_value;
        }
        Py_INCREF(new_value);
        Py_DECREF(key);
        Py_DECREF(value);
        value = new_value;
    }
    di->di_pos = i+1;
    di->len--;
    return value;

fail:
    di->di_dict = NULL;
    Py_DECREF(d);
    return NULL;
}

PyTypeObject PyDictIterValue_Type = {
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
#ifdef ENABLE_CINDERVM
    assert (Ci_Dict_CheckIncludingChecked((PyObject *) d));
#else
    assert (PyDict_Check((PyObject *) d));
#endif

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
    if (DICT_HAS_DEFERRED(d)
        && PyLazyImport_CheckExact(value)) {
        PyObject *new_value = PyImport_LoadLazyObject(value);
        if (new_value == NULL) {
            Py_DECREF(key);
            Py_DECREF(value);
            goto fail;
        }
        if (dk != d->ma_keys || entry_ptr->me_key != key) {
            /* TODO: for correctness, we should update here new_value
                into the dict but we're only doing it if the dictionary
                didn't change and bailing otherwise. */
            Py_DECREF(key);
            Py_DECREF(value);
            return NULL;
        }
        if (*value_ptr != new_value) {
            Py_INCREF(new_value);
            Py_DECREF(*value_ptr);
            *value_ptr = new_value;
        }
        Py_INCREF(new_value);
        Py_DECREF(value);
        value = new_value;
    }
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

PyTypeObject PyDictIterItem_Type = {
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
#ifdef ENABLE_CINDERVM
    assert (Ci_Dict_CheckIncludingChecked((PyObject *) d));
#else
    assert (PyDict_Check((PyObject *) d));
#endif

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
    if (DICT_HAS_DEFERRED(d)
        && PyLazyImport_CheckExact(value)) {
        PyObject *new_value = PyImport_LoadLazyObject(value);
        if (new_value == NULL) {
            Py_DECREF(key);
            Py_DECREF(value);
            goto fail;
        }
        if (dk != d->ma_keys || entry_ptr->me_key != key) {
            /* TODO: for correctness, we should update here new_value
                into the dict but we're only doing it if the dictionary
                didn't change and bailing otherwise. */
            Py_DECREF(key);
            Py_DECREF(value);
            return NULL;
        }
        if (*value_ptr != new_value) {
            Py_INCREF(new_value);
            Py_DECREF(*value_ptr);
            *value_ptr = new_value;
        }
        Py_INCREF(new_value);
        Py_DECREF(value);
        value = new_value;
    }
    di->di_pos = i-1;
    di->len--;

    if (Py_IS_TYPE(di, &PyDictRevIterKey_Type)) {
        Py_DECREF(value);
        return key;
    }
    else if (Py_IS_TYPE(di, &PyDictRevIterValue_Type)) {
        Py_DECREF(key);
        return value;
    }
    else if (Py_IS_TYPE(di, &PyDictRevIterItem_Type)) {
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

PyTypeObject PyDictRevIterKey_Type = {
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


/*[clinic input]
dict.__reversed__

Return a reverse iterator over the dict keys.
[clinic start generated code]*/

static PyObject *
dict___reversed___impl(PyDictObject *self)
/*[clinic end generated code: output=e674483336d1ed51 input=23210ef3477d8c4d]*/
{
#ifdef ENABLE_CINDERVM
    assert (Ci_Dict_CheckIncludingChecked((PyObject *) self));
#else
    assert (PyDict_Check((PyObject *) self));
#endif
    return dictiter_new(self, &PyDictRevIterKey_Type);
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

PyTypeObject PyDictRevIterItem_Type = {
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

PyTypeObject PyDictRevIterValue_Type = {
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

PyObject *
_PyDictView_New(PyObject *dict, PyTypeObject *type)
{
    PyDictObject *d;
    _PyDictViewObject *dv;
    if (dict == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
#ifdef ENABLE_CINDERVM
    if (!Ci_Dict_CheckIncludingChecked(dict)) {
#else
    if (!PyDict_Check(dict)) {
#endif
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
    if (DICT_HAS_DEFERRED(d)
        && _PyDict_LoadDeferred(d) != 0) {
        return NULL;
    }
    dv->dv_dict = d;
    _PyObject_GC_TRACK(dv);
    return (PyObject *)dv;
}

static PyObject *
dictview_mapping(PyObject *view, void *Py_UNUSED(ignored)) {
    assert(view != NULL);
    assert(PyDictKeys_Check(view)
           || PyDictValues_Check(view)
           || PyDictItems_Check(view));
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
    assert(PyDictViewSet_Check(self));
    assert(other != NULL);

    if (!PyAnySet_Check(other) && !PyDictViewSet_Check(other))
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
    return dictiter_new(dv->dv_dict, &PyDictIterKey_Type);
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
    if (PyDictKeys_Check(self)) {
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

PyObject *
_PyDictView_Intersect(PyObject* self, PyObject *other)
{
    PyObject *result;
    PyObject *it;
    PyObject *key;
    Py_ssize_t len_self;
    int rv;
    int (*dict_contains)(_PyDictViewObject *, PyObject *);

    /* Python interpreter swaps parameters when dict view
       is on right side of & */
    if (!PyDictViewSet_Check(self)) {
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
    if (PyDictViewSet_Check(other)) {
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

    if (PyDictKeys_Check(self)) {
        dict_contains = dictkeys_contains;
    }
    /* else PyDictItems_Check(self) */
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
    assert(PyDictItems_Check(self));
    assert(PyDictItems_Check(other));
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

    while (_PyDict_Next(d2, &pos, &key, &val2, &hash)) {
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
    if (PyDictItems_Check(self) && PyDictItems_Check(other)) {
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
    (binaryfunc)_PyDictView_Intersect,  /*nb_and*/
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
    if (PyAnySet_Check(other) || PyDictViewSet_Check(other)) {
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

PyTypeObject PyDictKeys_Type = {
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
    return _PyDictView_New(dict, &PyDictKeys_Type);
}

static PyObject *
dictkeys_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored))
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &PyDictRevIterKey_Type);
}

/*** dict_items ***/

static PyObject *
dictitems_iter(_PyDictViewObject *dv)
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &PyDictIterItem_Type);
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

PyTypeObject PyDictItems_Type = {
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
    return _PyDictView_New(dict, &PyDictItems_Type);
}

static PyObject *
dictitems_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored))
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &PyDictRevIterItem_Type);
}

/*** dict_values ***/

static PyObject *
dictvalues_iter(_PyDictViewObject *dv)
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &PyDictIterValue_Type);
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

PyTypeObject PyDictValues_Type = {
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
    return _PyDictView_New(dict, &PyDictValues_Type);
}

static PyObject *
dictvalues_reversed(_PyDictViewObject *dv, PyObject *Py_UNUSED(ignored))
{
    if (dv->dv_dict == NULL) {
        Py_RETURN_NONE;
    }
    return dictiter_new(dv->dv_dict, &PyDictRevIterValue_Type);
}


/* Returns NULL if cannot allocate a new PyDictKeysObject,
   but does not set an error */
PyDictKeysObject *
_PyDict_NewKeysForClass(void)
{
    PyDictKeysObject *keys = new_keys_object(PyDict_MINSIZE);
    if (keys == NULL) {
        PyErr_Clear();
    }
    else {
        keys->dk_lookup = lookdict_split;
    }
    return keys;
}

#define CACHED_KEYS(tp) (((PyHeapTypeObject*)tp)->ht_cached_keys)

PyObject *
PyObject_GenericGetDict(PyObject *obj, void *context)
{
    PyObject *dict, **dictptr = _PyObject_GetDictPtr(obj);
    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return NULL;
    }
    dict = *dictptr;
    if (dict == NULL) {
        PyTypeObject *tp = Py_TYPE(obj);
        if ((tp->tp_flags & Py_TPFLAGS_HEAPTYPE) && CACHED_KEYS(tp)) {
            dictkeys_incref(CACHED_KEYS(tp));
            *dictptr = dict = new_dict_with_shared_keys(CACHED_KEYS(tp));
        }
        else {
            *dictptr = dict = PyDict_New();
        }
    }
    Py_XINCREF(dict);
    return dict;
}

int
_PyObjectDict_SetItem(PyTypeObject *tp, PyObject **dictptr,
                      PyObject *key, PyObject *value)
{
    PyObject *dict;
    int res;
    PyDictKeysObject *cached;

    assert(dictptr != NULL);
    if ((tp->tp_flags & Py_TPFLAGS_HEAPTYPE) && (cached = CACHED_KEYS(tp))) {
        assert(dictptr != NULL);
        dict = *dictptr;
        if (dict == NULL) {
            if (tp->tp_flags & Py_TPFLAGS_WARN_ON_SETATTR &&
                _PyErr_RaiseCinderWarning(
                    "WARN001: Dictionary created for flagged instance",
                    (PyObject *)tp,
                    key)) {
                return -1;
            }
            dictkeys_incref(cached);
            dict = new_dict_with_shared_keys(cached);
            if (dict == NULL)
                return -1;
            *dictptr = dict;
        }
        if (value == NULL) {
            res = PyDict_DelItem(dict, key);
            // Since key sharing dict doesn't allow deletion, PyDict_DelItem()
            // always converts dict to combined form.
            if ((cached = CACHED_KEYS(tp)) != NULL) {
                CACHED_KEYS(tp) = NULL;
                PyType_Modified(tp);
                dictkeys_decref(cached);
            }
        }
        else {
            int was_shared = (cached == ((PyDictObject *)dict)->ma_keys);
            res = PyDict_SetItem(dict, key, value);
            if (was_shared &&
                    (cached = CACHED_KEYS(tp)) != NULL &&
                    cached != ((PyDictObject *)dict)->ma_keys) {
                /* PyDict_SetItem() may call dictresize and convert split table
                 * into combined table.  In such case, convert it to split
                 * table again and update type's shared key only when this is
                 * the only dict sharing key with the type.
                 *
                 * This is to allow using shared key in class like this:
                 *
                 *     class C:
                 *         def __init__(self):
                 *             # one dict resize happens
                 *             self.a, self.b, self.c = 1, 2, 3
                 *             self.d, self.e, self.f = 4, 5, 6
                 *     a = C()
                 */
                if (cached->dk_refcnt == 1) {
                    CACHED_KEYS(tp) = _PyDict_MakeKeysShared(dict);
                }
                else {
                    CACHED_KEYS(tp) = NULL;
                }
                PyType_Modified(tp);
                dictkeys_decref(cached);
                if (CACHED_KEYS(tp) == NULL && PyErr_Occurred())
                    return -1;
            }
        }
    } else {
        dict = *dictptr;
        if (dict == NULL) {
            dict = PyDict_New();
            if (dict == NULL)
                return -1;
            *dictptr = dict;
        }
        if (value == NULL) {
            res = PyDict_DelItem(dict, key);
        } else {
            res = PyDict_SetItem(dict, key, value);
        }
    }
    return res;
}

/* Similar to PyDict_GetItemWithError, but it resolves any deferred
 * objects only if the deferred object is not resolving to itself, otherwise
 * it returns as missing item. */
PyObject *
_PyDict_GetAttrItem(PyObject *op, PyObject *key)
{
    Py_ssize_t ix;
    Py_hash_t hash;
    PyDictKeysObject *dk;
    PyObject *value;
    PyObject *new_value = NULL;
    PyDictObject *mp = (PyDictObject *)op;

    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1)
    {
        hash = PyObject_Hash(key);
        if (hash == -1) {
            return NULL;
        }
    }

top:
    dk = mp->ma_keys;
    ix = dk->dk_lookup(mp, key, hash, &value, 0);
    if (ix < 0) {
        return NULL;
    }

    if (DICT_HAS_DEFERRED(mp)) {
        if (new_value || PyLazyImport_CheckExact(value)) {
            PyDictKeyEntry *ep = &DK_ENTRIES(dk)[ix];
            if (new_value == NULL) {
                PyObject *startkey = ep->me_key;
                Py_INCREF(startkey);
                Py_INCREF(value);
                if (PyLazyImport_Match((PyLazyImport *)value, op, key)) {
                    if (((PyLazyImport *)value)->lz_resolving) {
                        return NULL;
                    }
                    ((PyLazyImport *)value)->lz_resolving = 1;
                    new_value = PyImport_LoadLazyObject(value);
                    ((PyLazyImport *)value)->lz_resolving = 0;
                } else {
                    new_value = PyImport_LoadLazyObject(value);
                }
                Py_XINCREF(new_value);
                Py_DECREF(value);
                Py_DECREF(startkey);
                if (new_value == NULL) {
                    return NULL;
                }
                if (dk != mp->ma_keys || ep->me_key != startkey) {
                    /* The dict has mutated, restart to update new_value */
                    goto top;
                }
            }
            PyObject **value_ptr = mp->ma_values
                ? &mp->ma_values[ix]
                : &ep->me_value;
            if (*value_ptr == new_value) {
                Py_DECREF(new_value);
            } else {
                Py_DECREF(*value_ptr);
                *value_ptr = new_value;
            }
            value = new_value;
        }
    }

    return value;
}

void
_PyDictKeys_DecRef(PyDictKeysObject *keys)
{
    dictkeys_decref(keys);
}

/* Cinder _PyDict_GetItem_* specializations. */

/* Same as _PyDict_GetItem_KnownHash, but takes a array of objects to be
 * compared as a tuple */
PyObject *
_PyDict_GetItem_StackKnownHash(PyObject *op,
                               PyObject *const *stack,
                               Py_ssize_t nargs,
                               Py_hash_t hash)
{
    PyDictObject *mp = (PyDictObject *)op;
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
            return NULL;
        }
        if (ix >= 0) {
            PyDictKeyEntry *ep = &ep0[ix];
            assert(ep->me_key != NULL);

            if (ep->me_hash == hash) {
                PyObject *startkey = ep->me_key;
                Py_INCREF(startkey);
                if (PyTuple_Check(startkey) &&
                    PyTuple_GET_SIZE(startkey) == nargs) {
                    int differ = 0;
                    for (Py_ssize_t tuple_index = 0; tuple_index < nargs;
                         tuple_index++) {
                        PyObject *l = PyTuple_GET_ITEM(startkey, tuple_index);
                        PyObject *r = stack[tuple_index];
                        if (l == r) {
                            continue;
                        }
                        int cmp = PyObject_RichCompareBool(l, r, Py_EQ);
                        if (cmp < 0) {
                            Py_DECREF(startkey);
                            return NULL;
                        }
                        if (cmp == 0) {
                            differ = 1;
                            break;
                        }
                    }
                    if (dk == mp->ma_keys && ep->me_key == startkey) {
                        if (!differ) {
                            Py_DECREF(startkey);
                            return ep->me_value;
                        }
                    } else {
                        /* The dict was mutated, restart */
                        goto top;
                    }
                }
                Py_DECREF(startkey);
            }
        }
        perturb >>= PERTURB_SHIFT;
        i = (i * 5 + perturb + 1) & mask;
    }
    Py_UNREACHABLE();
}

PyObject *
_PyDict_GetItem_String_KnownHash(PyObject *op,
                                 const char *key,
                                 Py_ssize_t len,
                                 Py_hash_t hash)
{
    Py_ssize_t ix;
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;

    if (!PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (mp->ma_keys->dk_lookup == lookdict) {
        PyObject *name_obj = PyUnicode_FromString(key);
        ix = (mp->ma_keys->dk_lookup)(mp, name_obj, hash, &value, 1);
    } else {
        assert(mp->ma_values == NULL);
        PyDictKeyEntry *ep0 = DK_ENTRIES(mp->ma_keys);
        size_t mask = DK_MASK(mp->ma_keys);
        size_t perturb = (size_t)hash;
        size_t i = (size_t)hash & mask;

        for (;;) {
            ix = dictkeys_get_index(mp->ma_keys, i);
            if (ix == DKIX_EMPTY) {
                value = NULL;
                break;
            }
            if (ix >= 0) {
                PyDictKeyEntry *ep = &ep0[ix];
                assert(ep->me_key != NULL);
                assert(PyUnicode_CheckExact(ep->me_key));
                if (ep->me_hash == hash &&
                    _PyUnicode_EqualToASCIIString(ep->me_key, key)) {
                    if (mp->ma_keys->dk_lookup == lookdict_split) {
                        value = mp->ma_values[ix];
                    } else {
                        value = ep->me_value;
                    }
                    break;
                }
            }
            perturb >>= PERTURB_SHIFT;
            i = mask & (i * 5 + perturb + 1);
        }
    }
    if (ix < 0) {
        return NULL;
    }
    return value;
}

/* Lookup unicode object in dict, optimizing for case when dict
 * keys are also all unicode objects */
PyObject *
_PyDict_GetItem_Unicode(PyObject *op, PyObject *key)
{
    Py_ssize_t ix;
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;

    assert(PyDict_Check(op));

    if (PyUnicode_CheckExact(key) && mp->ma_keys->dk_lookup != lookdict) {
        Py_ssize_t hash = ((PyASCIIObject *)key)->hash;
        if (hash == -1) {
            hash = PyObject_Hash(key);
        }
        ix = (mp->ma_keys->dk_lookup)(mp, key, hash, &value, 1);
        if (ix < 0) {
            return NULL;
        }
        return value;
    }

    return PyDict_GetItem(op, key);
}


PyObject *
_PyDict_GetItem_UnicodeExact(PyObject *op, PyObject *key)
{
    Py_ssize_t ix;
    PyDictObject *mp = (PyDictObject *)op;
    PyObject *value;

    assert(PyDict_Check(op));
    assert(PyUnicode_CheckExact(key));

    if (mp->ma_keys->dk_lookup == lookdict_unicode_nodummy) {
        /* inline the common case, avoid all PyUnicode_CheckExact calls */
        Py_ssize_t hash = ((PyASCIIObject *)key)->hash;
        assert(hash != -1);
        PyDictKeyEntry *ep0 = DK_ENTRIES(mp->ma_keys);
        size_t mask = DK_MASK(mp->ma_keys);
        size_t perturb = (size_t)hash;
        size_t i = (size_t)hash & mask;

        for (;;) {
            Py_ssize_t ix = dictkeys_get_index(mp->ma_keys, i);
            assert(ix != DKIX_DUMMY);
            if (ix == DKIX_EMPTY) {
                return NULL;
            }
            PyDictKeyEntry *ep = &ep0[ix];
            assert(ep->me_key != NULL);
            assert(PyUnicode_CheckExact(ep->me_key));
            if (ep->me_key == key ||
                (ep->me_hash == hash && unicode_eq(ep->me_key, key))) {
                return ep->me_value;
            }
            perturb >>= PERTURB_SHIFT;
            i = mask & (i * 5 + perturb + 1);
        }
        Py_UNREACHABLE();
    } else if (mp->ma_keys->dk_lookup != lookdict) {
        Py_ssize_t hash = ((PyASCIIObject *)key)->hash;
        assert(hash != -1);
        ix = (mp->ma_keys->dk_lookup)(mp, key, hash, &value, 1);
        if (ix < 0) {
            return NULL;
        }
        return value;
    }

    return PyDict_GetItem(op, key);
}

Py_ssize_t
_PyDictKeys_GetSplitIndex(PyDictKeysObject *keys, PyObject *key) {
    Py_hash_t hash;
    if (!PyUnicode_CheckExact(key) ||
        (hash = ((PyASCIIObject *) key)->hash) == -1) {
        hash = PyObject_Hash(key);
    }

    PyDictKeyEntry *ep0 = DK_ENTRIES(keys);
    size_t mask = DK_MASK(keys);
    size_t perturb = (size_t)hash;
    size_t i = (size_t)hash & mask;

    for (;;) {
        Py_ssize_t ix = dictkeys_get_index(keys, i);
        assert (ix != DKIX_DUMMY);
        if (ix == DKIX_EMPTY) {
            return DKIX_EMPTY;
        }
        PyDictKeyEntry *ep = &ep0[ix];
        assert(ep->me_key != NULL);
        assert(PyUnicode_CheckExact(ep->me_key));
        if (ep->me_key == key ||
            (ep->me_hash == hash && unicode_eq(ep->me_key, key))) {
            return ix;
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i*5 + perturb + 1);
    }
}

int
Ci_PyDict_ForceCombined(PyObject *dict) {
    if ((dict == NULL) || !PyDict_Check(dict)) {
        return 0;
    }
    PyDictObject *dictobj = (PyDictObject *) dict;
    if (!_PyDict_HasSplitTable(dictobj)) {
        return 0;
    }
    return dictresize(dictobj, calculate_keysize(dictobj->ma_keys->dk_size));
}

PyDictKeyEntry*
_PyDictKeys_GetEntries(PyDictKeysObject *keys)
{
    return DK_ENTRIES(keys);
}

#ifdef ENABLE_CINDERVM

/***********************************************************************
 * Type-enforced dictionary - shares most of the implementation with the
 * standard builtin dictioanry.  Replaces things which can do mutation
 * with a version that forces type checks when called from un-typed
 * Python code.  Statically Typed Python code will be able to call versions
 * of most functionality in a way that elides the type checks */

CiAPI_DATA(_PyGenericTypeDef) Ci_CheckedDict_Type;

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
    DICT_POP_METHODDEF DICT_POPITEM_METHODDEF{
        "keys", dictkeys_new, METH_NOARGS, keys__doc__},
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
#endif
