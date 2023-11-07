/*

  Reference Cycle Garbage Collection
  ==================================

  Neil Schemenauer <nas@arctrix.com>

  Based on a post on the python-dev list.  Ideas from Guido van Rossum,
  Eric Tiedemann, and various others.

  http://www.arctrix.com/nas/python/gc/

  The following mailing list threads provide a historical perspective on
  the design of this module.  Note that a fair amount of refinement has
  occurred since those discussions.

  http://mail.python.org/pipermail/python-dev/2000-March/002385.html
  http://mail.python.org/pipermail/python-dev/2000-March/002434.html
  http://mail.python.org/pipermail/python-dev/2000-March/002497.html

  For a highlevel view of the collection process, read the collect
  function.

*/

#include "Python.h"
#include "pyconfig.h"
#include "pycore_atomic.h"
#include "pycore_context.h"
#include "pycore_gc.h"
#include "pycore_initconfig.h"
#include "pycore_interp.h"      // PyInterpreterState.gc
#include "pycore_object.h"
#include "pycore_pyerrors.h"
#include "pycore_pystate.h"     // _PyThreadState_GET()
#include "ws_deque.h"
#include "condvar.h"
#include "cinder/cinder.h"
#include "cinder/exports.h"

#if defined(__x86_64__) || defined(__amd64)
#include <immintrin.h>

static inline void
Ci_cpu_pause()
{
    _mm_pause();
}

#else

static inline void
Ci_cpu_pause()
{}

#endif

#ifdef Py_DEBUG
#  define GC_DEBUG
#endif

typedef struct _gc_runtime_state GCState;

#define GC_NEXT _PyGCHead_NEXT
#define GC_PREV _PyGCHead_PREV

// update_refs() set this bit for all objects in current generation.
// subtract_refs() and move_unreachable() uses this to distinguish
// visited object is in GCing or not.
//
// move_unreachable() removes this flag from reachable objects.
// Only unreachable objects have this flag.
//
// No objects in interpreter have this flag after GC ends.
#define PREV_MASK_COLLECTING   _PyGC_PREV_MASK_COLLECTING

// Lowest bit of _gc_next is used for UNREACHABLE flag.
//
// This flag represents the object is in unreachable list in move_unreachable()
//
// Although this flag is used only in move_unreachable(), move_unreachable()
// doesn't clear this flag to skip unnecessary iteration.
// move_legacy_finalizers() removes this flag instead.
// Between them, unreachable list is not normal list and we can not use
// most gc_list_* functions for it.
#define NEXT_MASK_UNREACHABLE  (1)

/* Get an object's GC head */
#define AS_GC(o) ((PyGC_Head *)(o)-1)

/* Get the object given the GC head */
#define FROM_GC(g) ((PyObject *)(((PyGC_Head *)g)+1))

static inline int
gc_is_collecting(PyGC_Head *g)
{
    return (g->_gc_prev & PREV_MASK_COLLECTING) != 0;
}

static inline void
gc_clear_collecting(PyGC_Head *g)
{
    g->_gc_prev &= ~PREV_MASK_COLLECTING;
}

static inline Py_ssize_t
gc_get_refs(PyGC_Head *g)
{
    return (Py_ssize_t)(g->_gc_prev >> _PyGC_PREV_SHIFT);
}

static inline void
gc_set_refs(PyGC_Head *g, Py_ssize_t refs)
{
    g->_gc_prev = (g->_gc_prev & ~_PyGC_PREV_MASK)
        | ((uintptr_t)(refs) << _PyGC_PREV_SHIFT);
}

static inline void
gc_reset_refs(PyGC_Head *g, Py_ssize_t refs)
{
    g->_gc_prev = (g->_gc_prev & _PyGC_PREV_MASK_FINALIZED)
        | PREV_MASK_COLLECTING
        | ((uintptr_t)(refs) << _PyGC_PREV_SHIFT);
}

static inline void
gc_decref(PyGC_Head *g)
{
    _PyObject_ASSERT_WITH_MSG(FROM_GC(g),
                              gc_get_refs(g) > 0,
                              "refcount is too small");
    g->_gc_prev -= 1 << _PyGC_PREV_SHIFT;
}

/* set for debugging information */
#define DEBUG_STATS             (1<<0) /* print collection statistics */
#define DEBUG_COLLECTABLE       (1<<1) /* print collectable objects */
#define DEBUG_UNCOLLECTABLE     (1<<2) /* print uncollectable objects */
#define DEBUG_SAVEALL           (1<<5) /* save all garbage in gc.garbage */
#define DEBUG_LEAK              DEBUG_COLLECTABLE | \
                DEBUG_UNCOLLECTABLE | \
                DEBUG_SAVEALL

#define GEN_HEAD(gcstate, n) (&(gcstate)->generations[n].head)


static GCState *
get_gc_state(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return &interp->gc;
}

/*
_gc_prev values
---------------

Between collections, _gc_prev is used for doubly linked list.

Lowest two bits of _gc_prev are used for flags.
PREV_MASK_COLLECTING is used only while collecting and cleared before GC ends
or _PyObject_GC_UNTRACK() is called.

During a collection, _gc_prev is temporary used for gc_refs, and the gc list
is singly linked until _gc_prev is restored.

gc_refs
    At the start of a collection, update_refs() copies the true refcount
    to gc_refs, for each object in the generation being collected.
    subtract_refs() then adjusts gc_refs so that it equals the number of
    times an object is referenced directly from outside the generation
    being collected.

PREV_MASK_COLLECTING
    Objects in generation being collected are marked PREV_MASK_COLLECTING in
    update_refs().


_gc_next values
---------------

_gc_next takes these values:

0
    The object is not tracked

!= 0
    Pointer to the next object in the GC list.
    Additionally, lowest bit is used temporary for
    NEXT_MASK_UNREACHABLE flag described below.

NEXT_MASK_UNREACHABLE
    move_unreachable() then moves objects not reachable (whether directly or
    indirectly) from outside the generation into an "unreachable" set and
    set this flag.

    Objects that are found to be reachable have gc_refs set to 1.
    When this flag is set for the reachable object, the object must be in
    "unreachable" set.
    The flag is unset and the object is moved back to "reachable" set.

    move_legacy_finalizers() will remove this flag from "unreachable" set.
*/

/*** list functions ***/

static inline void
gc_list_init(PyGC_Head *list)
{
    // List header must not have flags.
    // We can assign pointer by simple cast.
    list->_gc_prev = (uintptr_t)list;
    list->_gc_next = (uintptr_t)list;
}

static inline int
gc_list_is_empty(PyGC_Head *list)
{
    return (list->_gc_next == (uintptr_t)list);
}

/* Append `node` to `list`. */
static inline void
gc_list_append(PyGC_Head *node, PyGC_Head *list)
{
    PyGC_Head *last = (PyGC_Head *)list->_gc_prev;

    // last <-> node
    _PyGCHead_SET_PREV(node, last);
    _PyGCHead_SET_NEXT(last, node);

    // node <-> list
    _PyGCHead_SET_NEXT(node, list);
    list->_gc_prev = (uintptr_t)node;
}

/* Move `node` from the gc list it's currently in (which is not explicitly
 * named here) to the end of `list`.  This is semantically the same as
 * gc_list_remove(node) followed by gc_list_append(node, list).
 */
static void
gc_list_move(PyGC_Head *node, PyGC_Head *list)
{
    /* Unlink from current list. */
    PyGC_Head *from_prev = GC_PREV(node);
    PyGC_Head *from_next = GC_NEXT(node);
    _PyGCHead_SET_NEXT(from_prev, from_next);
    _PyGCHead_SET_PREV(from_next, from_prev);

    /* Relink at end of new list. */
    // list must not have flags.  So we can skip macros.
    PyGC_Head *to_prev = (PyGC_Head*)list->_gc_prev;
    _PyGCHead_SET_PREV(node, to_prev);
    _PyGCHead_SET_NEXT(to_prev, node);
    list->_gc_prev = (uintptr_t)node;
    _PyGCHead_SET_NEXT(node, list);
}

/* append list `from` onto list `to`; `from` becomes an empty list */
static void
gc_list_merge(PyGC_Head *from, PyGC_Head *to)
{
    assert(from != to);
    if (!gc_list_is_empty(from)) {
        PyGC_Head *to_tail = GC_PREV(to);
        PyGC_Head *from_head = GC_NEXT(from);
        PyGC_Head *from_tail = GC_PREV(from);
        assert(from_head != from);
        assert(from_tail != from);

        _PyGCHead_SET_NEXT(to_tail, from_head);
        _PyGCHead_SET_PREV(from_head, to_tail);

        _PyGCHead_SET_NEXT(from_tail, to);
        _PyGCHead_SET_PREV(to, from_tail);
    }
    gc_list_init(from);
}

static Py_ssize_t
gc_list_size(PyGC_Head *list)
{
    PyGC_Head *gc;
    Py_ssize_t n = 0;
    for (gc = GC_NEXT(list); gc != list; gc = GC_NEXT(gc)) {
        n++;
    }
    return n;
}

/* Walk the list and mark all objects as non-collecting */
static inline void
gc_list_clear_collecting(PyGC_Head *collectable)
{
    PyGC_Head *gc;
    for (gc = GC_NEXT(collectable); gc != collectable; gc = GC_NEXT(gc)) {
        gc_clear_collecting(gc);
    }
}

// Constants for validate_list's flags argument.
enum flagstates {collecting_clear_unreachable_clear,
                 collecting_clear_unreachable_set,
                 collecting_set_unreachable_clear,
                 collecting_set_unreachable_set};

#ifdef GC_DEBUG
// validate_list checks list consistency.  And it works as document
// describing when flags are expected to be set / unset.
// `head` must be a doubly-linked gc list, although it's fine (expected!) if
// the prev and next pointers are "polluted" with flags.
// What's checked:
// - The `head` pointers are not polluted.
// - The objects' PREV_MASK_COLLECTING and NEXT_MASK_UNREACHABLE flags are all
//   `set or clear, as specified by the 'flags' argument.
// - The prev and next pointers are mutually consistent.
static void
validate_list(PyGC_Head *head, enum flagstates flags)
{
    assert((head->_gc_prev & PREV_MASK_COLLECTING) == 0);
    assert((head->_gc_next & NEXT_MASK_UNREACHABLE) == 0);
    uintptr_t prev_value = 0, next_value = 0;
    switch (flags) {
        case collecting_clear_unreachable_clear:
            break;
        case collecting_set_unreachable_clear:
            prev_value = PREV_MASK_COLLECTING;
            break;
        case collecting_clear_unreachable_set:
            next_value = NEXT_MASK_UNREACHABLE;
            break;
        case collecting_set_unreachable_set:
            prev_value = PREV_MASK_COLLECTING;
            next_value = NEXT_MASK_UNREACHABLE;
            break;
        default:
            assert(! "bad internal flags argument");
    }
    PyGC_Head *prev = head;
    PyGC_Head *gc = GC_NEXT(head);
    while (gc != head) {
        PyGC_Head *trueprev = GC_PREV(gc);
        PyGC_Head *truenext = (PyGC_Head *)(gc->_gc_next  & ~NEXT_MASK_UNREACHABLE);
        assert(truenext != NULL);
        assert(trueprev == prev);
        assert((gc->_gc_prev & PREV_MASK_COLLECTING) == prev_value);
        assert((gc->_gc_next & NEXT_MASK_UNREACHABLE) == next_value);
        prev = gc;
        gc = truenext;
    }
    assert(prev == GC_PREV(head));
}
#else
#define validate_list(x, y) do{}while(0)
#endif

/*** end of list stuff ***/


/* Set all gc_refs = ob_refcnt.  After this, gc_refs is > 0 and
 * PREV_MASK_COLLECTING bit is set for all objects in containers.
 */
static size_t
update_refs(PyGC_Head *containers)
{
    // PyGC_Head *gc = GC_NEXT(containers);
    // for (; gc != containers; gc = GC_NEXT(gc)) {
    //     gc_reset_refs(gc, Py_REFCNT(FROM_GC(gc)));
    //     /* Python's cyclic gc should never see an incoming refcount
    //      * of 0:  if something decref'ed to 0, it should have been
    //      * deallocated immediately at that time.
    //      * Possible cause (if the assert triggers):  a tp_dealloc
    //      * routine left a gc-aware object tracked during its teardown
    //      * phase, and did something-- or allowed something to happen --
    //      * that called back into Python.  gc can trigger then, and may
    //      * see the still-tracked dying object.  Before this assert
    //      * was added, such mistakes went on to allow gc to try to
    //      * delete the object again.  In a debug build, that caused
    //      * a mysterious segfault, when _Py_ForgetReference tried
    //      * to remove the object from the doubly-linked list of all
    //      * objects a second time.  In a release build, an actual
    //      * double deallocation occurred, which leads to corruption
    //      * of the allocator's internal bookkeeping pointers.  That's
    //      * so serious that maybe this should be a release-build
    //      * check instead of an assert?
    //      */
    //     _PyObject_ASSERT(FROM_GC(gc), gc_get_refs(gc) != 0);
    // }
    PyGC_Head *next;
    PyGC_Head *gc = GC_NEXT(containers);
    GCState *gcstate = get_gc_state();
    size_t num_seen = 0;
    while (gc != containers) {
        next = GC_NEXT(gc);
        /* Move any object that might have become immortal to the
        * permanent generation as the reference count is not accurately
        * reflecting the actual number of live references to this object
        */
        if (_Py_IsImmortal(FROM_GC(gc))) {
            gc_list_move(gc, &gcstate->permanent_generation.head);
            gc = next;
            continue;
        }
        gc_reset_refs(gc, Py_REFCNT(FROM_GC(gc)));
        /* Python's cyclic gc should never see an incoming refcount
         * of 0:  if something decref'ed to 0, it should have been
         * deallocated immediately at that time.
         * Possible cause (if the assert triggers):  a tp_dealloc
         * routine left a gc-aware object tracked during its teardown
         * phase, and did something-- or allowed something to happen --
         * that called back into Python.  gc can trigger then, and may
         * see the still-tracked dying object.  Before this assert
         * was added, such mistakes went on to allow gc to try to
         * delete the object again.  In a debug build, that caused
         * a mysterious segfault, when _Py_ForgetReference tried
         * to remove the object from the doubly-linked list of all
         * objects a second time.  In a release build, an actual
         * double deallocation occurred, which leads to corruption
         * of the allocator's internal bookkeeping pointers.  That's
         * so serious that maybe this should be a release-build
         * check instead of an assert?
         */
        _PyObject_ASSERT(FROM_GC(gc), gc_get_refs(gc) != 0);
        gc = next;
        num_seen++;
    }
    return num_seen;
}

/* A traversal callback for subtract_refs. */
static int
visit_decref(PyObject *op, void *parent)
{
    #ifndef Py_DEBUG
    (void) parent;
    #endif

    _PyObject_ASSERT(_PyObject_CAST(parent), !_PyObject_IsFreed(op));

    if (_PyObject_IS_GC(op)) {
        PyGC_Head *gc = AS_GC(op);
        /* We're only interested in gc_refs for objects in the
         * generation being collected, which can be recognized
         * because only they have positive gc_refs.
         */
        if (gc_is_collecting(gc)) {
            gc_decref(gc);
        }
    }
    return 0;
}

/* Subtract internal references from gc_refs.  After this, gc_refs is >= 0
 * for all objects in containers, and is GC_REACHABLE for all tracked gc
 * objects not in containers.  The ones with gc_refs > 0 are directly
 * reachable from outside containers, and so can't be collected.
 */
static void
subtract_refs(PyGC_Head *containers)
{
    traverseproc traverse;
    PyGC_Head *gc = GC_NEXT(containers);
    for (; gc != containers; gc = GC_NEXT(gc)) {
        PyObject *op = FROM_GC(gc);
        traverse = Py_TYPE(op)->tp_traverse;
        (void) traverse(FROM_GC(gc),
                       (visitproc)visit_decref,
                       op);
    }
}

/* A traversal callback for move_unreachable. */
static int
visit_reachable(PyObject *op, PyGC_Head *reachable)
{
    if (!_PyObject_IS_GC(op)) {
        return 0;
    }

    PyGC_Head *gc = AS_GC(op);
    const Py_ssize_t gc_refs = gc_get_refs(gc);

    // Ignore objects in other generation.
    // This also skips objects "to the left" of the current position in
    // move_unreachable's scan of the 'young' list - they've already been
    // traversed, and no longer have the PREV_MASK_COLLECTING flag.
    if (! gc_is_collecting(gc)) {
        return 0;
    }
    // It would be a logic error elsewhere if the collecting flag were set on
    // an untracked object.
    assert(gc->_gc_next != 0);

    if (gc->_gc_next & NEXT_MASK_UNREACHABLE) {
        /* This had gc_refs = 0 when move_unreachable got
         * to it, but turns out it's reachable after all.
         * Move it back to move_unreachable's 'young' list,
         * and move_unreachable will eventually get to it
         * again.
         */
        // Manually unlink gc from unreachable list because the list functions
        // don't work right in the presence of NEXT_MASK_UNREACHABLE flags.
        PyGC_Head *prev = GC_PREV(gc);
        PyGC_Head *next = (PyGC_Head*)(gc->_gc_next & ~NEXT_MASK_UNREACHABLE);
        _PyObject_ASSERT(FROM_GC(prev),
                         prev->_gc_next & NEXT_MASK_UNREACHABLE);
        _PyObject_ASSERT(FROM_GC(next),
                         next->_gc_next & NEXT_MASK_UNREACHABLE);
        prev->_gc_next = gc->_gc_next;  // copy NEXT_MASK_UNREACHABLE
        _PyGCHead_SET_PREV(next, prev);

        gc_list_append(gc, reachable);
        gc_set_refs(gc, 1);
    }
    else if (gc_refs == 0) {
        /* This is in move_unreachable's 'young' list, but
         * the traversal hasn't yet gotten to it.  All
         * we need to do is tell move_unreachable that it's
         * reachable.
         */
        gc_set_refs(gc, 1);
    }
    /* Else there's nothing to do.
     * If gc_refs > 0, it must be in move_unreachable's 'young'
     * list, and move_unreachable will eventually get to it.
     */
    else {
        _PyObject_ASSERT_WITH_MSG(op, gc_refs > 0, "refcount is too small");
    }
    return 0;
}

/* Move the unreachable objects from young to unreachable.  After this,
 * all objects in young don't have PREV_MASK_COLLECTING flag and
 * unreachable have the flag.
 * All objects in young after this are directly or indirectly reachable
 * from outside the original young; and all objects in unreachable are
 * not.
 *
 * This function restores _gc_prev pointer.  young and unreachable are
 * doubly linked list after this function.
 * But _gc_next in unreachable list has NEXT_MASK_UNREACHABLE flag.
 * So we can not gc_list_* functions for unreachable until we remove the flag.
 */
static void
move_unreachable(PyGC_Head *young, PyGC_Head *unreachable)
{
    // previous elem in the young list, used for restore gc_prev.
    PyGC_Head *prev = young;
    PyGC_Head *gc = GC_NEXT(young);

    /* Invariants:  all objects "to the left" of us in young are reachable
     * (directly or indirectly) from outside the young list as it was at entry.
     *
     * All other objects from the original young "to the left" of us are in
     * unreachable now, and have NEXT_MASK_UNREACHABLE.  All objects to the
     * left of us in 'young' now have been scanned, and no objects here
     * or to the right have been scanned yet.
     */

    while (gc != young) {
        if (gc_get_refs(gc)) {
            /* gc is definitely reachable from outside the
             * original 'young'.  Mark it as such, and traverse
             * its pointers to find any other objects that may
             * be directly reachable from it.  Note that the
             * call to tp_traverse may append objects to young,
             * so we have to wait until it returns to determine
             * the next object to visit.
             */
            PyObject *op = FROM_GC(gc);
            traverseproc traverse = Py_TYPE(op)->tp_traverse;
            _PyObject_ASSERT_WITH_MSG(op, gc_get_refs(gc) > 0,
                                      "refcount is too small");
            // NOTE: visit_reachable may change gc->_gc_next when
            // young->_gc_prev == gc.  Don't do gc = GC_NEXT(gc) before!
            (void) traverse(op,
                    (visitproc)visit_reachable,
                    (void *)young);
            // relink gc_prev to prev element.
            _PyGCHead_SET_PREV(gc, prev);
            // gc is not COLLECTING state after here.
            gc_clear_collecting(gc);
            prev = gc;
        }
        else {
            /* This *may* be unreachable.  To make progress,
             * assume it is.  gc isn't directly reachable from
             * any object we've already traversed, but may be
             * reachable from an object we haven't gotten to yet.
             * visit_reachable will eventually move gc back into
             * young if that's so, and we'll see it again.
             */
            // Move gc to unreachable.
            // No need to gc->next->prev = prev because it is single linked.
            prev->_gc_next = gc->_gc_next;

            // We can't use gc_list_append() here because we use
            // NEXT_MASK_UNREACHABLE here.
            PyGC_Head *last = GC_PREV(unreachable);
            // NOTE: Since all objects in unreachable set has
            // NEXT_MASK_UNREACHABLE flag, we set it unconditionally.
            // But this may pollute the unreachable list head's 'next' pointer
            // too. That's semantically senseless but expedient here - the
            // damage is repaired when this function ends.
            last->_gc_next = (NEXT_MASK_UNREACHABLE | (uintptr_t)gc);
            _PyGCHead_SET_PREV(gc, last);
            gc->_gc_next = (NEXT_MASK_UNREACHABLE | (uintptr_t)unreachable);
            unreachable->_gc_prev = (uintptr_t)gc;
        }
        gc = (PyGC_Head*)prev->_gc_next;
    }
    // young->_gc_prev must be last element remained in the list.
    young->_gc_prev = (uintptr_t)prev;
    // don't let the pollution of the list head's next pointer leak
    unreachable->_gc_next &= ~NEXT_MASK_UNREACHABLE;
}

static void
untrack_tuples(PyGC_Head *head)
{
    PyGC_Head *next, *gc = GC_NEXT(head);
    while (gc != head) {
        PyObject *op = FROM_GC(gc);
        next = GC_NEXT(gc);
        if (PyTuple_CheckExact(op)) {
            _PyTuple_MaybeUntrack(op);
        }
        gc = next;
    }
}

/* Try to untrack all currently tracked dictionaries */
static void
untrack_dicts(PyGC_Head *head)
{
    PyGC_Head *next, *gc = GC_NEXT(head);
    while (gc != head) {
        PyObject *op = FROM_GC(gc);
        next = GC_NEXT(gc);
        if (PyDict_CheckExact(op)) {
            _PyDict_MaybeUntrack(op);
        }
        gc = next;
    }
}

/* Return true if object has a pre-PEP 442 finalization method. */
static int
has_legacy_finalizer(PyObject *op)
{
    return Py_TYPE(op)->tp_del != NULL;
}

/* Move the objects in unreachable with tp_del slots into `finalizers`.
 *
 * This function also removes NEXT_MASK_UNREACHABLE flag
 * from _gc_next in unreachable.
 */
static void
move_legacy_finalizers(PyGC_Head *unreachable, PyGC_Head *finalizers)
{
    PyGC_Head *gc, *next;
    assert((unreachable->_gc_next & NEXT_MASK_UNREACHABLE) == 0);

    /* March over unreachable.  Move objects with finalizers into
     * `finalizers`.
     */
    for (gc = GC_NEXT(unreachable); gc != unreachable; gc = next) {
        PyObject *op = FROM_GC(gc);

        _PyObject_ASSERT(op, gc->_gc_next & NEXT_MASK_UNREACHABLE);
        gc->_gc_next &= ~NEXT_MASK_UNREACHABLE;
        next = (PyGC_Head*)gc->_gc_next;

        if (has_legacy_finalizer(op)) {
            gc_clear_collecting(gc);
            gc_list_move(gc, finalizers);
        }
    }
}

static inline void
clear_unreachable_mask(PyGC_Head *unreachable)
{
    /* Check that the list head does not have the unreachable bit set */
    assert(((uintptr_t)unreachable & NEXT_MASK_UNREACHABLE) == 0);

    PyGC_Head *gc, *next;
    assert((unreachable->_gc_next & NEXT_MASK_UNREACHABLE) == 0);
    for (gc = GC_NEXT(unreachable); gc != unreachable; gc = next) {
        _PyObject_ASSERT((PyObject*)FROM_GC(gc), gc->_gc_next & NEXT_MASK_UNREACHABLE);
        gc->_gc_next &= ~NEXT_MASK_UNREACHABLE;
        next = (PyGC_Head*)gc->_gc_next;
    }
    validate_list(unreachable, collecting_set_unreachable_clear);
}

/* A traversal callback for move_legacy_finalizer_reachable. */
static int
visit_move(PyObject *op, PyGC_Head *tolist)
{
    if (_PyObject_IS_GC(op)) {
        PyGC_Head *gc = AS_GC(op);
        if (gc_is_collecting(gc)) {
            gc_list_move(gc, tolist);
            gc_clear_collecting(gc);
        }
    }
    return 0;
}

/* Move objects that are reachable from finalizers, from the unreachable set
 * into finalizers set.
 */
static void
move_legacy_finalizer_reachable(PyGC_Head *finalizers)
{
    traverseproc traverse;
    PyGC_Head *gc = GC_NEXT(finalizers);
    for (; gc != finalizers; gc = GC_NEXT(gc)) {
        /* Note that the finalizers list may grow during this. */
        traverse = Py_TYPE(FROM_GC(gc))->tp_traverse;
        (void) traverse(FROM_GC(gc),
                        (visitproc)visit_move,
                        (void *)finalizers);
    }
}

/* Clear all weakrefs to unreachable objects, and if such a weakref has a
 * callback, invoke it if necessary.  Note that it's possible for such
 * weakrefs to be outside the unreachable set -- indeed, those are precisely
 * the weakrefs whose callbacks must be invoked.  See gc_weakref.txt for
 * overview & some details.  Some weakrefs with callbacks may be reclaimed
 * directly by this routine; the number reclaimed is the return value.  Other
 * weakrefs with callbacks may be moved into the `old` generation.  Objects
 * moved into `old` have gc_refs set to GC_REACHABLE; the objects remaining in
 * unreachable are left at GC_TENTATIVELY_UNREACHABLE.  When this returns,
 * no object in `unreachable` is weakly referenced anymore.
 */
static int
handle_weakrefs(PyGC_Head *unreachable, PyGC_Head *old)
{
    PyGC_Head *gc;
    PyObject *op;               /* generally FROM_GC(gc) */
    PyWeakReference *wr;        /* generally a cast of op */
    PyGC_Head wrcb_to_call;     /* weakrefs with callbacks to call */
    PyGC_Head *next;
    int num_freed = 0;

    gc_list_init(&wrcb_to_call);

    /* Clear all weakrefs to the objects in unreachable.  If such a weakref
     * also has a callback, move it into `wrcb_to_call` if the callback
     * needs to be invoked.  Note that we cannot invoke any callbacks until
     * all weakrefs to unreachable objects are cleared, lest the callback
     * resurrect an unreachable object via a still-active weakref.  We
     * make another pass over wrcb_to_call, invoking callbacks, after this
     * pass completes.
     */
    for (gc = GC_NEXT(unreachable); gc != unreachable; gc = next) {
        PyWeakReference **wrlist;

        op = FROM_GC(gc);
        next = GC_NEXT(gc);

        if (PyWeakref_Check(op)) {
            /* A weakref inside the unreachable set must be cleared.  If we
             * allow its callback to execute inside delete_garbage(), it
             * could expose objects that have tp_clear already called on
             * them.  Or, it could resurrect unreachable objects.  One way
             * this can happen is if some container objects do not implement
             * tp_traverse.  Then, wr_object can be outside the unreachable
             * set but can be deallocated as a result of breaking the
             * reference cycle.  If we don't clear the weakref, the callback
             * will run and potentially cause a crash.  See bpo-38006 for
             * one example.
             */
            _PyWeakref_ClearRef((PyWeakReference *)op);
        }

        if (! PyType_SUPPORTS_WEAKREFS(Py_TYPE(op)))
            continue;

        /* It supports weakrefs.  Does it have any? */
        wrlist = (PyWeakReference **)
                                _PyObject_GET_WEAKREFS_LISTPTR(op);

        /* `op` may have some weakrefs.  March over the list, clear
         * all the weakrefs, and move the weakrefs with callbacks
         * that must be called into wrcb_to_call.
         */
        for (wr = *wrlist; wr != NULL; wr = *wrlist) {
            PyGC_Head *wrasgc;                  /* AS_GC(wr) */

            /* _PyWeakref_ClearRef clears the weakref but leaves
             * the callback pointer intact.  Obscure:  it also
             * changes *wrlist.
             */
            _PyObject_ASSERT((PyObject *)wr, wr->wr_object == op);
            _PyWeakref_ClearRef(wr);
            _PyObject_ASSERT((PyObject *)wr, wr->wr_object == Py_None);
            if (wr->wr_callback == NULL) {
                /* no callback */
                continue;
            }

            /* Headache time.  `op` is going away, and is weakly referenced by
             * `wr`, which has a callback.  Should the callback be invoked?  If wr
             * is also trash, no:
             *
             * 1. There's no need to call it.  The object and the weakref are
             *    both going away, so it's legitimate to pretend the weakref is
             *    going away first.  The user has to ensure a weakref outlives its
             *    referent if they want a guarantee that the wr callback will get
             *    invoked.
             *
             * 2. It may be catastrophic to call it.  If the callback is also in
             *    cyclic trash (CT), then although the CT is unreachable from
             *    outside the current generation, CT may be reachable from the
             *    callback.  Then the callback could resurrect insane objects.
             *
             * Since the callback is never needed and may be unsafe in this case,
             * wr is simply left in the unreachable set.  Note that because we
             * already called _PyWeakref_ClearRef(wr), its callback will never
             * trigger.
             *
             * OTOH, if wr isn't part of CT, we should invoke the callback:  the
             * weakref outlived the trash.  Note that since wr isn't CT in this
             * case, its callback can't be CT either -- wr acted as an external
             * root to this generation, and therefore its callback did too.  So
             * nothing in CT is reachable from the callback either, so it's hard
             * to imagine how calling it later could create a problem for us.  wr
             * is moved to wrcb_to_call in this case.
             */
            if (gc_is_collecting(AS_GC(wr))) {
                /* it should already have been cleared above */
                assert(wr->wr_object == Py_None);
                continue;
            }

            /* Create a new reference so that wr can't go away
             * before we can process it again.
             */
            Py_INCREF(wr);

            /* Move wr to wrcb_to_call, for the next pass. */
            wrasgc = AS_GC(wr);
            assert(wrasgc != next); /* wrasgc is reachable, but
                                       next isn't, so they can't
                                       be the same */
            gc_list_move(wrasgc, &wrcb_to_call);
        }
    }

    /* Invoke the callbacks we decided to honor.  It's safe to invoke them
     * because they can't reference unreachable objects.
     */
    while (! gc_list_is_empty(&wrcb_to_call)) {
        PyObject *temp;
        PyObject *callback;

        gc = (PyGC_Head*)wrcb_to_call._gc_next;
        op = FROM_GC(gc);
        _PyObject_ASSERT(op, PyWeakref_Check(op));
        wr = (PyWeakReference *)op;
        callback = wr->wr_callback;
        _PyObject_ASSERT(op, callback != NULL);

        /* copy-paste of weakrefobject.c's handle_callback() */
        temp = PyObject_CallOneArg(callback, (PyObject *)wr);
        if (temp == NULL)
            PyErr_WriteUnraisable(callback);
        else
            Py_DECREF(temp);

        /* Give up the reference we created in the first pass.  When
         * op's refcount hits 0 (which it may or may not do right now),
         * op's tp_dealloc will decref op->wr_callback too.  Note
         * that the refcount probably will hit 0 now, and because this
         * weakref was reachable to begin with, gc didn't already
         * add it to its count of freed objects.  Example:  a reachable
         * weak value dict maps some key to this reachable weakref.
         * The callback removes this key->weakref mapping from the
         * dict, leaving no other references to the weakref (excepting
         * ours).
         */
        Py_DECREF(op);
        if (wrcb_to_call._gc_next == (uintptr_t)gc) {
            /* object is still alive -- move it */
            gc_list_move(gc, old);
        }
        else {
            ++num_freed;
        }
    }

    return num_freed;
}

static void
debug_cycle(const char *msg, PyObject *op)
{
    PySys_FormatStderr("gc: %s <%s %p>\n",
                       msg, Py_TYPE(op)->tp_name, op);
}

/* Handle uncollectable garbage (cycles with tp_del slots, and stuff reachable
 * only from such cycles).
 * If DEBUG_SAVEALL, all objects in finalizers are appended to the module
 * garbage list (a Python list), else only the objects in finalizers with
 * __del__ methods are appended to garbage.  All objects in finalizers are
 * merged into the old list regardless.
 */
static void
handle_legacy_finalizers(PyThreadState *tstate,
                         GCState *gcstate,
                         PyGC_Head *finalizers, PyGC_Head *old)
{
    assert(!_PyErr_Occurred(tstate));
    assert(gcstate->garbage != NULL);

    PyGC_Head *gc = GC_NEXT(finalizers);
    for (; gc != finalizers; gc = GC_NEXT(gc)) {
        PyObject *op = FROM_GC(gc);

        if ((gcstate->debug & DEBUG_SAVEALL) || has_legacy_finalizer(op)) {
            if (PyList_Append(gcstate->garbage, op) < 0) {
                _PyErr_Clear(tstate);
                break;
            }
        }
    }

    gc_list_merge(finalizers, old);
}

/* Run first-time finalizers (if any) on all the objects in collectable.
 * Note that this may remove some (or even all) of the objects from the
 * list, due to refcounts falling to 0.
 */
static void
finalize_garbage(PyThreadState *tstate, PyGC_Head *collectable)
{
    destructor finalize;
    PyGC_Head seen;

    #ifdef NDEBUG
    (void) (tstate);
    #endif

    /* While we're going through the loop, `finalize(op)` may cause op, or
     * other objects, to be reclaimed via refcounts falling to zero.  So
     * there's little we can rely on about the structure of the input
     * `collectable` list across iterations.  For safety, we always take the
     * first object in that list and move it to a temporary `seen` list.
     * If objects vanish from the `collectable` and `seen` lists we don't
     * care.
     */
    gc_list_init(&seen);

    while (!gc_list_is_empty(collectable)) {
        PyGC_Head *gc = GC_NEXT(collectable);
        PyObject *op = FROM_GC(gc);
        gc_list_move(gc, &seen);
        if (!_PyGCHead_FINALIZED(gc) &&
                (finalize = Py_TYPE(op)->tp_finalize) != NULL) {
            _PyGCHead_SET_FINALIZED(gc);
            Py_INCREF(op);
            finalize(op);
            assert(!_PyErr_Occurred(tstate));
            Py_DECREF(op);
        }
    }
    gc_list_merge(&seen, collectable);
}

/* Break reference cycles by clearing the containers involved.  This is
 * tricky business as the lists can be changing and we don't know which
 * objects may be freed.  It is possible I screwed something up here.
 */
static void
delete_garbage(PyThreadState *tstate, GCState *gcstate,
               PyGC_Head *collectable, PyGC_Head *old)
{
    assert(!_PyErr_Occurred(tstate));

    while (!gc_list_is_empty(collectable)) {
        PyGC_Head *gc = GC_NEXT(collectable);
        PyObject *op = FROM_GC(gc);

        _PyObject_ASSERT_WITH_MSG(op, Py_REFCNT(op) > 0,
                                  "refcount is too small");

        if (gcstate->debug & DEBUG_SAVEALL) {
            assert(gcstate->garbage != NULL);
            if (PyList_Append(gcstate->garbage, op) < 0) {
                _PyErr_Clear(tstate);
            }
        }
        else {
            inquiry clear;
            if ((clear = Py_TYPE(op)->tp_clear) != NULL) {
                Py_INCREF(op);
                (void) clear(op);
                if (_PyErr_Occurred(tstate)) {
                    _PyErr_WriteUnraisableMsg("in tp_clear of",
                                              (PyObject*)Py_TYPE(op));
                }
                Py_DECREF(op);
            }
        }
        if (GC_NEXT(collectable) == gc) {
            /* object is still alive, move it, it may die later */
            gc_clear_collecting(gc);
            gc_list_move(gc, old);
        }
    }
}

// Show stats for objects in each generations
static void
show_stats_each_generations(GCState *gcstate)
{
    char buf[100];
    size_t pos = 0;

    for (int i = 0; i < NUM_GENERATIONS && pos < sizeof(buf); i++) {
        pos += PyOS_snprintf(buf+pos, sizeof(buf)-pos,
                             " %zd",
                             gc_list_size(GEN_HEAD(gcstate, i)));
    }

    PySys_FormatStderr(
        "gc: objects in each generation:%s\n"
        "gc: objects in permanent generation: %zd\n",
        buf, gc_list_size(&gcstate->permanent_generation.head));
}

/* Deduce which objects among "base" are unreachable from outside the list
   and move them to 'unreachable'. The process consist in the following steps:

1. Copy all reference counts to a different field (gc_prev is used to hold
   this copy to save memory).
2. Traverse all objects in "base" and visit all referred objects using
   "tp_traverse" and for every visited object, subtract 1 to the reference
   count (the one that we copied in the previous step). After this step, all
   objects that can be reached directly from outside must have strictly positive
   reference count, while all unreachable objects must have a count of exactly 0.
3. Identify all unreachable objects (the ones with 0 reference count) and move
   them to the "unreachable" list. This step also needs to move back to "base" all
   objects that were initially marked as unreachable but are referred transitively
   by the reachable objects (the ones with strictly positive reference count).

Contracts:

    * The "base" has to be a valid list with no mask set.

    * The "unreachable" list must be uninitialized (this function calls
      gc_list_init over 'unreachable').

IMPORTANT: This function leaves 'unreachable' with the NEXT_MASK_UNREACHABLE
flag set but it does not clear it to skip unnecessary iteration. Before the
flag is cleared (for example, by using 'clear_unreachable_mask' function or
by a call to 'move_legacy_finalizers'), the 'unreachable' list is not a normal
list and we can not use most gc_list_* functions for it. */
static inline void
deduce_unreachable(PyGC_Head *base, PyGC_Head *unreachable) {
    validate_list(base, collecting_clear_unreachable_clear);
    /* Using ob_refcnt and gc_refs, calculate which objects in the
     * container set are reachable from outside the set (i.e., have a
     * refcount greater than 0 when all the references within the
     * set are taken into account).
     */
    update_refs(base);  // gc_prev is used for gc_refs
    subtract_refs(base);

    /* Leave everything reachable from outside base in base, and move
     * everything else (in base) to unreachable.
     *
     * NOTE:  This used to move the reachable objects into a reachable
     * set instead.  But most things usually turn out to be reachable,
     * so it's more efficient to move the unreachable things.  It "sounds slick"
     * to move the unreachable objects, until you think about it - the reason it
     * pays isn't actually obvious.
     *
     * Suppose we create objects A, B, C in that order.  They appear in the young
     * generation in the same order.  If B points to A, and C to B, and C is
     * reachable from outside, then the adjusted refcounts will be 0, 0, and 1
     * respectively.
     *
     * When move_unreachable finds A, A is moved to the unreachable list.  The
     * same for B when it's first encountered.  Then C is traversed, B is moved
     * _back_ to the reachable list.  B is eventually traversed, and then A is
     * moved back to the reachable list.
     *
     * So instead of not moving at all, the reachable objects B and A are moved
     * twice each.  Why is this a win?  A straightforward algorithm to move the
     * reachable objects instead would move A, B, and C once each.
     *
     * The key is that this dance leaves the objects in order C, B, A - it's
     * reversed from the original order.  On all _subsequent_ scans, none of
     * them will move.  Since most objects aren't in cycles, this can save an
     * unbounded number of moves across an unbounded number of later collections.
     * It can cost more only the first time the chain is scanned.
     *
     * Drawback:  move_unreachable is also used to find out what's still trash
     * after finalizers may resurrect objects.  In _that_ case most unreachable
     * objects will remain unreachable, so it would be more efficient to move
     * the reachable objects instead.  But this is a one-time cost, probably not
     * worth complicating the code to speed just a little.
     */
    gc_list_init(unreachable);
    move_unreachable(base, unreachable);  // gc_prev is pointer again
    validate_list(base, collecting_clear_unreachable_clear);
    validate_list(unreachable, collecting_set_unreachable_set);
}

/* Handle objects that may have resurrected after a call to 'finalize_garbage', moving
   them to 'old_generation' and placing the rest on 'still_unreachable'.

   Contracts:
       * After this function 'unreachable' must not be used anymore and 'still_unreachable'
         will contain the objects that did not resurrect.

       * The "still_unreachable" list must be uninitialized (this function calls
         gc_list_init over 'still_unreachable').

IMPORTANT: After a call to this function, the 'still_unreachable' set will have the
PREV_MARK_COLLECTING set, but the objects in this set are going to be removed so
we can skip the expense of clearing the flag to avoid extra iteration. */
static inline void
handle_resurrected_objects(PyGC_Head *unreachable, PyGC_Head* still_unreachable,
                           PyGC_Head *old_generation)
{
    // Remove the PREV_MASK_COLLECTING from unreachable
    // to prepare it for a new call to 'deduce_unreachable'
    gc_list_clear_collecting(unreachable);

    // After the call to deduce_unreachable, the 'still_unreachable' set will
    // have the PREV_MARK_COLLECTING set, but the objects are going to be
    // removed so we can skip the expense of clearing the flag.
    PyGC_Head* resurrected = unreachable;
    deduce_unreachable(resurrected, still_unreachable);
    clear_unreachable_mask(still_unreachable);

    // Move the resurrected objects to the old generation for future collection.
    gc_list_merge(resurrected, old_generation);
}

typedef struct Ci_ParGCState Ci_ParGCState;

static void
Ci_deduce_unreachable_parallel(Ci_ParGCState *par_gc, PyGC_Head *base, PyGC_Head *unreachable);

static int
Ci_should_use_par_gc(Ci_ParGCState *par_gc, int gen);

/* This is the main function.  Read this to understand how the
 * collection process works. */
static Py_ssize_t
gc_collect_main(Ci_PyGCImpl *gc_impl, PyThreadState *tstate, int generation,
                Py_ssize_t *n_collected, Py_ssize_t *n_uncollectable,
                int nofail)
{
    int i;
    Py_ssize_t m = 0; /* # objects collected */
    Py_ssize_t n = 0; /* # unreachable objects that couldn't be collected */
    PyGC_Head *young; /* the generation we are examining */
    PyGC_Head *old; /* next older generation */
    PyGC_Head unreachable; /* non-problematic unreachable trash */
    PyGC_Head finalizers;  /* objects with, & reachable from, __del__ */
    PyGC_Head *gc;
    _PyTime_t t1 = 0;   /* initialize to prevent a compiler warning */
    GCState *gcstate = &tstate->interp->gc;

    // gc_collect_main() must not be called before _PyGC_Init
    // or after _PyGC_Fini()
    assert(gcstate->garbage != NULL);
    assert(!_PyErr_Occurred(tstate));

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    if (tstate->interp->config._isolated_interpreter) {
        // bpo-40533: The garbage collector must not be run on parallel on
        // Python objects shared by multiple interpreters.
        return 0;
    }
#endif

    if (gcstate->debug & DEBUG_STATS) {
        PySys_WriteStderr("gc: collecting generation %d...\n", generation);
        show_stats_each_generations(gcstate);
        t1 = _PyTime_GetMonotonicClock();
    }

    /* update collection and allocation counters */
    if (generation+1 < NUM_GENERATIONS)
        gcstate->generations[generation+1].count += 1;
    for (i = 0; i <= generation; i++)
        gcstate->generations[i].count = 0;

    /* merge younger generations with one we are currently collecting */
    for (i = 0; i < generation; i++) {
        gc_list_merge(GEN_HEAD(gcstate, i), GEN_HEAD(gcstate, generation));
    }

    /* handy references */
    young = GEN_HEAD(gcstate, generation);
    if (generation < NUM_GENERATIONS-1)
        old = GEN_HEAD(gcstate, generation+1);
    else
        old = young;
    validate_list(old, collecting_clear_unreachable_clear);

    Ci_ParGCState *par_gc = (Ci_ParGCState *) gc_impl;
    if (Ci_should_use_par_gc(par_gc, generation)) {
        Ci_deduce_unreachable_parallel(par_gc, young, &unreachable);
    } else {
        deduce_unreachable(young, &unreachable);
    }

    untrack_tuples(young);
    /* Move reachable objects to next generation. */
    if (young != old) {
        if (generation == NUM_GENERATIONS - 2) {
            gcstate->long_lived_pending += gc_list_size(young);
        }
        gc_list_merge(young, old);
    }
    else {
        /* We only un-track dicts in full collections, to avoid quadratic
           dict build-up. See issue #14775. */
        untrack_dicts(young);
        gcstate->long_lived_pending = 0;
        gcstate->long_lived_total = gc_list_size(young);
    }

    /* All objects in unreachable are trash, but objects reachable from
     * legacy finalizers (e.g. tp_del) can't safely be deleted.
     */
    gc_list_init(&finalizers);
    // NEXT_MASK_UNREACHABLE is cleared here.
    // After move_legacy_finalizers(), unreachable is normal list.
    move_legacy_finalizers(&unreachable, &finalizers);
    /* finalizers contains the unreachable objects with a legacy finalizer;
     * unreachable objects reachable *from* those are also uncollectable,
     * and we move those into the finalizers list too.
     */
    move_legacy_finalizer_reachable(&finalizers);

    validate_list(&finalizers, collecting_clear_unreachable_clear);
    validate_list(&unreachable, collecting_set_unreachable_clear);

    /* Print debugging information. */
    if (gcstate->debug & DEBUG_COLLECTABLE) {
        for (gc = GC_NEXT(&unreachable); gc != &unreachable; gc = GC_NEXT(gc)) {
            debug_cycle("collectable", FROM_GC(gc));
        }
    }

    /* Clear weakrefs and invoke callbacks as necessary. */
    m += handle_weakrefs(&unreachable, old);

    validate_list(old, collecting_clear_unreachable_clear);
    validate_list(&unreachable, collecting_set_unreachable_clear);

    /* Call tp_finalize on objects which have one. */
    finalize_garbage(tstate, &unreachable);

    /* Handle any objects that may have resurrected after the call
     * to 'finalize_garbage' and continue the collection with the
     * objects that are still unreachable */
    PyGC_Head final_unreachable;
    handle_resurrected_objects(&unreachable, &final_unreachable, old);

    /* Call tp_clear on objects in the final_unreachable set.  This will cause
    * the reference cycles to be broken.  It may also cause some objects
    * in finalizers to be freed.
    */
    m += gc_list_size(&final_unreachable);
    delete_garbage(tstate, gcstate, &final_unreachable, old);

    /* Collect statistics on uncollectable objects found and print
     * debugging information. */
    for (gc = GC_NEXT(&finalizers); gc != &finalizers; gc = GC_NEXT(gc)) {
        n++;
        if (gcstate->debug & DEBUG_UNCOLLECTABLE)
            debug_cycle("uncollectable", FROM_GC(gc));
    }
    if (gcstate->debug & DEBUG_STATS) {
        double d = _PyTime_AsSecondsDouble(_PyTime_GetMonotonicClock() - t1);
        PySys_WriteStderr(
            "gc: done, %zd unreachable, %zd uncollectable, %.4fs elapsed\n",
            n+m, n, d);
    }

    /* Append instances in the uncollectable set to a Python
     * reachable list of garbage.  The programmer has to deal with
     * this if they insist on creating this type of structure.
     */
    handle_legacy_finalizers(tstate, gcstate, &finalizers, old);
    validate_list(old, collecting_clear_unreachable_clear);

    /* Clear free list only during the collection of the highest
     * generation */
    if (generation == NUM_GENERATIONS-1) {
        Ci_PyGC_ClearFreeLists(tstate->interp);
    }

    if (_PyErr_Occurred(tstate)) {
        if (nofail) {
            _PyErr_Clear(tstate);
        }
        else {
            _PyErr_WriteUnraisableMsg("in garbage collection", NULL);
        }
    }

    /* Update stats */
    if (n_collected) {
        *n_collected = m;
    }
    if (n_uncollectable) {
        *n_uncollectable = n;
    }

    struct gc_generation_stats *stats = &gcstate->generation_stats[generation];
    stats->collections++;
    stats->collected += m;
    stats->uncollectable += n;

    assert(!_PyErr_Occurred(tstate));
    return n + m;
}

#define MUTEX_INIT(mut) \
    if (PyMUTEX_INIT(&(mut))) { \
        Py_FatalError("PyMUTEX_INIT(" #mut ") failed"); };
#define MUTEX_FINI(mut) \
    if (PyMUTEX_FINI(&(mut))) { \
        Py_FatalError("PyMUTEX_FINI(" #mut ") failed"); };
#define MUTEX_LOCK(mut) \
    if (PyMUTEX_LOCK(&(mut))) { \
        Py_FatalError("PyMUTEX_LOCK(" #mut ") failed"); };
#define MUTEX_UNLOCK(mut) \
    if (PyMUTEX_UNLOCK(&(mut))) { \
        Py_FatalError("PyMUTEX_UNLOCK(" #mut ") failed"); };

#define COND_INIT(cond) \
    if (PyCOND_INIT(&(cond))) { \
        Py_FatalError("PyCOND_INIT(" #cond ") failed"); };
#define COND_FINI(cond) \
    if (PyCOND_FINI(&(cond))) { \
        Py_FatalError("PyCOND_FINI(" #cond ") failed"); };
#define COND_BROADCAST(cond) \
    if (PyCOND_BROADCAST(&(cond))) { \
        Py_FatalError("PyCOND_BROADCAST(" #cond ") failed"); };
#define COND_SIGNAL(cond) \
    if (PyCOND_SIGNAL(&(cond))) { \
        Py_FatalError("PyCOND_SIGNAL(" #cond ") failed"); };
#define COND_WAIT(cond, mut) \
    if (PyCOND_WAIT(&(cond), &(mut))) { \
        Py_FatalError("PyCOND_WAIT(" #cond ") failed"); };

static PyMUTEX_T ci_log_lock;
static int ci_log_lock_initialized = 0;

#define CI_LOG_DISABLED 0
#define CI_LOG_STAT 25
#define CI_LOG_DEBUG 50
#define CI_LOG_TRACE 100

#define CI_LOG_LEVEL CI_LOG_DISABLED

// Must only be called from a python thread with the GIL held
#define CI_INIT_LOGGING() do {                              \
    if (CI_LOG_LEVEL && !ci_log_lock_initialized) {         \
        MUTEX_INIT(ci_log_lock);                            \
        ci_log_lock_initialized = 1;                        \
    }                                                       \
} while (0)

#define CI_VLOG(level, ...) do {                                         \
    if (level <= CI_LOG_LEVEL) {                                         \
        MUTEX_LOCK(ci_log_lock);                                         \
        fprintf(stderr, "PARGC: T%lu -- ", PyThread_get_thread_ident()); \
        fprintf(stderr, __VA_ARGS__);                                    \
        fprintf(stderr, "\n");                                           \
        MUTEX_UNLOCK(ci_log_lock);                                       \
    }                                                                    \
} while (0)

#define CI_DLOG(...) CI_VLOG(CI_LOG_DEBUG, __VA_ARGS__)
#define CI_STAT(...) CI_VLOG(CI_LOG_STAT, __VA_ARGS__)
#define CI_TRACE(...) CI_VLOG(CI_LOG_TRACE, __VA_ARGS__)

// A portable semaphore
typedef struct {
    size_t tokens_left;
    PyMUTEX_T lock;
    PyCOND_T cond;
} Ci_Sema;

static void
Ci_Sema_Init(Ci_Sema *sema)
{
    sema->tokens_left = 0;
    MUTEX_INIT(sema->lock);
    COND_INIT(sema->cond);
}

static void
Ci_Sema_Post(Ci_Sema *sema, size_t tokens)
{
    MUTEX_LOCK(sema->lock);
    sema->tokens_left += tokens;
    for (size_t i = 0; i < tokens; i++) {
        COND_SIGNAL(sema->cond);
    }
    MUTEX_UNLOCK(sema->lock);
}

static void
Ci_Sema_Wait(Ci_Sema *sema)
{
    MUTEX_LOCK(sema->lock);
    while (sema->tokens_left == 0) {
        COND_WAIT(sema->cond, sema->lock);
    }
    sema->tokens_left--;
    MUTEX_UNLOCK(sema->lock);
}

static void
Ci_Sema_Fini(Ci_Sema *sema)
{
    MUTEX_FINI(sema->lock);
    COND_FINI(sema->cond);
}

// A barrier coordinates arrival between N threads.
//
// All N threads must reach the barrier before it is lifted, unblocking all
// threads.
typedef struct {
    // Number of threads left to reach the barrier before it can be lifted.
    unsigned int num_left;

    // Total number of threads managed by the barrier
    unsigned int capacity;

    // The epoch advances once all threads reach the barrier; it
    // disambiguates spurious wakeups from true wakeups that happen once all
    // threads have reached the barrier.
    unsigned int epoch;

    PyMUTEX_T lock;
    PyCOND_T cond;
} Ci_Barrier;

static void
Ci_Barrier_Init(Ci_Barrier *barrier, int capacity)
{
    barrier->capacity = capacity;
    barrier->num_left = capacity;
    barrier->epoch = 0;
    MUTEX_INIT(barrier->lock);
    COND_INIT(barrier->cond);
}

static void
Ci_Barrier_Fini(Ci_Barrier *barrier)
{
    MUTEX_FINI(barrier->lock);
    COND_FINI(barrier->cond);
}

// Wait for all threads to reach the barrier before continuing.
static void
Ci_Barrier_Wait(Ci_Barrier *barrier)
{
    MUTEX_LOCK(barrier->lock);
    barrier->num_left--;
    if (barrier->num_left == 0) {
        // We were the last one to get to the barrier; reset it and unblock
        // everyone else.
        barrier->num_left = barrier->capacity;
        barrier->epoch++;
        COND_BROADCAST(barrier->cond);
    } else {
        unsigned int epoch = barrier->epoch;
        while (epoch == barrier->epoch) {
            COND_WAIT(barrier->cond, barrier->lock);
        }
    }
    MUTEX_UNLOCK(barrier->lock);
}

// A slice of the GC list. This represents the half open interval [start, end)
typedef struct {
    PyGC_Head *start;
    PyGC_Head *end;
} Ci_GCSlice;

typedef struct {
    // The worker's portion of the GC list
    Ci_GCSlice gc_slice;

    Ci_WSDeque deque;

    // Counts the number of objects that were visited by the worker during the
    // subtract_refs phase of marking.
    unsigned long subtract_refs_load;

    // Counts the number of objects that were visited by the worker while
    // marking transitively reachable objects.
    unsigned long mark_load;

    unsigned long steal_attempts;
    unsigned long steal_successes;

    // Randomizes stealing order between workers
    unsigned int seed;

    Ci_ParGCState *par_gc;

    unsigned long thread_id;
} Ci_ParGCWorker;

struct Ci_ParGCState {
    Ci_PyGCImpl gc_impl;

    Ci_PyGCImpl *old_impl;

    // Only use the parallel collector when collecting generations >= this
    // value.
    int min_gen;

    // GC state to which this is bound
    struct _gc_runtime_state *gc_state;
    struct Ci_ParGCState *next;
    struct Ci_ParGCState *prev;

    // Synchronizes all workers before marking reachable objects
    Ci_Barrier mark_barrier;

    _Py_atomic_int num_workers_marking;

    PyMUTEX_T steal_coord_lock;
    Ci_ParGCWorker *steal_coordinator;
    Ci_Sema steal_sema;

    // Synchronizes all worker threads and the main thread at the end of parallel
    // collection
    Ci_Barrier done_barrier;

    // Tracks the number of workers actively running. When this reaches zero
    // it is safe to destroy shared state.
    _Py_atomic_int num_workers_active;

    size_t num_workers;
    Ci_ParGCWorker workers[];
};

static int
Ci_should_use_par_gc(Ci_ParGCState *par_gc, int gen)
{
    return par_gc != NULL && gen >= par_gc->min_gen;
}

static inline int
Ci_gc_is_collecting_atomic(PyGC_Head *g)
{
    uintptr_t prev = _Py_atomic_load_relaxed(((_Py_atomic_address *) &g->_gc_prev));
    return (prev & PREV_MASK_COLLECTING) != 0;
}

static inline void
Ci_gc_get_collecting_and_finalized_atomic(PyGC_Head *g, int *collecting, int *finalized)
{
    uintptr_t prev = _Py_atomic_load_relaxed(((_Py_atomic_address *) &g->_gc_prev));
    *collecting = (prev & PREV_MASK_COLLECTING) != 0;
    *finalized = (prev & _PyGC_PREV_MASK_FINALIZED) != 0;
}

static inline void
Ci_gc_decref_atomic(PyGC_Head *g)
{
   _Py_atomic_fetch_sub_relaxed(((_Py_atomic_address *) &g->_gc_prev), 1 << _PyGC_PREV_SHIFT);
}

static inline int
Ci_gc_is_collecting_and_reachable_atomic(PyGC_Head *g, int *finalized)
{
    uintptr_t prev = _Py_atomic_load_relaxed(((_Py_atomic_address *) &g->_gc_prev));
    *finalized = (prev & _PyGC_PREV_MASK_FINALIZED) != 0;
    return (prev >> _PyGC_PREV_SHIFT) && (prev & PREV_MASK_COLLECTING);
}

static inline void
Ci_gc_mark_reachable_and_clear_collecting_atomic(PyGC_Head *g, int finalized)
{
    assert(finalized == 0 || finalized == 1);
    uintptr_t val = (1 << _PyGC_PREV_SHIFT) | (finalized);
    _Py_atomic_store_relaxed(((_Py_atomic_address *) &g->_gc_prev), val);
}

// Subtract an incoming ref to op
static int
Ci_subtract_incoming_ref(PyObject *obj, Ci_ParGCWorker *worker)
{
    worker->subtract_refs_load++;
    assert(!_PyObject_IsFreed(obj));

    if (_PyObject_IS_GC(obj)) {
        PyGC_Head *gc = AS_GC(obj);
        /* We're only interested in gc_refs for objects in the generation being
         * collected.
         */
        if (Ci_gc_is_collecting_atomic(gc)) {
            CI_TRACE("Subtracting incoming ref to %p", obj);
            Ci_gc_decref_atomic(gc);
        }
    }

    return 0;
}

static void
Ci_ParGCWorker_SubtractRefs(Ci_ParGCWorker *worker)
{
    Ci_GCSlice *slice = &worker->gc_slice;
    for (PyGC_Head *gc = slice->start; gc != slice->end; gc = GC_NEXT(gc)) {
        PyObject *op = FROM_GC(gc);
        assert(!_PyObject_IsFreed(op));
        Py_TYPE(op)->tp_traverse(op, (visitproc) Ci_subtract_incoming_ref, worker);
        worker->subtract_refs_load++;
    }
}

static int
Ci_queue_obj_for_marking(PyObject *op, Ci_ParGCWorker *worker)
{
    worker->mark_load++;
    if (!_PyObject_IS_GC(op)) {
        CI_TRACE("%p not gc", op);
        return 0;
    }

    // Ignore objects in other generations and skip objects that were already
    // processed as part of marking transitively reachable objects.
    PyGC_Head *gc = AS_GC(op);
    int is_collecting, is_finalized;
    Ci_gc_get_collecting_and_finalized_atomic(gc, &is_collecting, &is_finalized);
    if (!is_collecting) {
        CI_TRACE("%p not collecting", op);
        return 0;
    }

    // Mark the object as being processed and reachable
    CI_TRACE("%p marked and queued", op);
    Ci_gc_mark_reachable_and_clear_collecting_atomic(gc, is_finalized);
    Ci_WSDeque_Push(&worker->deque, op);

    return 0;
}

// Attempt to steal a work item from another worker
static PyObject *
Ci_ParGCWorker_MaybeSteal(Ci_ParGCWorker *worker)
{
    Ci_ParGCWorker *victims = worker->par_gc->workers;
    int num_victims = worker->par_gc->num_workers;
    int start = rand_r(&worker->seed) % num_victims;
    PyObject *obj = NULL;
    for (int i = 0; i < num_victims && obj == NULL; i++) {
        Ci_ParGCWorker *victim = &victims[(start + i) % num_victims];
        if (victim == worker) {
            continue;
        }
        obj = (PyObject *) Ci_WSDeque_Steal(&victim->deque);
    }
    worker->steal_attempts++;
    if (obj != NULL) {
        worker->steal_successes++;
    }
    return obj;
}

static void
Ci_ParGCWorker_MarkGCSlice(Ci_ParGCWorker *worker)
{
    // At this point the GC list contains a mix of objects that are definitely
    // reachable (gc_refs > 0) and that may be unreachable (gc_refs == 0).
    for (PyGC_Head *gc = worker->gc_slice.start;
         gc != worker->gc_slice.end; gc = GC_NEXT(gc)) {
        int is_finalized;
        if (Ci_gc_is_collecting_and_reachable_atomic(gc, &is_finalized)) {
            CI_TRACE("Marking %p from gc list slice", FROM_GC(gc));
            Ci_gc_mark_reachable_and_clear_collecting_atomic(gc, is_finalized);

            // This object is reachable. Mark anything reachable from it.
            PyObject *obj = FROM_GC(gc);
            Py_TYPE(obj)->tp_traverse(obj,
                                      (visitproc) Ci_queue_obj_for_marking,
                                      worker);
        } else {
            CI_TRACE("Ignoring %p from gc list slice", FROM_GC(gc));
        }
        worker->mark_load++;
    }
}

typedef enum Ci_ParGCWorker_MarkState {
    CI_PGCW_MS_START,
    CI_PGCW_MS_MARK,
    CI_PGCW_MS_STEAL,
} Ci_ParGCWorker_MarkState;

// Process the object graph that is reachable from items in the worker's
// mark queue, attempting to steal new work when the queue becomes empty.
//
// This implements the state machine below:
//
//
//
//                          +----------+
//                          |          |
//                          v     q not empty
//                    +-----------+    |
//       +----------->| process q +----+
//       |            +--------+--+
//       |              ^      |
//    q not empty       |      |
//       |              |      |
//       |              |      |
//   +---+---+        stole  q empty       +------+
//   | start |          |      |           | done |
//   +---+---+          |      |           +------+
//       |              |      |              ^
//      q empty         |      v              |
//       |            +-+---------+      didn't steal
//       +----------->|   steal   +-----------+
//                    +-----------+
static void
Ci_ParGCWorker_ProcessMarkQueueAndSteal(Ci_ParGCWorker *worker)
{
    PyObject *obj = Ci_WSDeque_Take(&worker->deque);
    Ci_ParGCWorker_MarkState state = CI_PGCW_MS_START;

    while (1) {
      switch (state) {
      case CI_PGCW_MS_START: {
          if (obj == NULL) {
              state = CI_PGCW_MS_STEAL;
          } else {
              state = CI_PGCW_MS_MARK;
          }
          break;
      }

      case CI_PGCW_MS_MARK: {
          // Process mark queue
          while (obj != NULL) {
              CI_TRACE("Visiting %p from dequeue", obj);
              Py_TYPE(obj)->tp_traverse(obj,
                                        (visitproc) Ci_queue_obj_for_marking,
                                        worker);
              obj = Ci_WSDeque_Take(&worker->deque);
          }
          state = CI_PGCW_MS_STEAL;
          break;
      }

      case CI_PGCW_MS_STEAL: {
          // Try to steal some work
          obj = Ci_ParGCWorker_MaybeSteal(worker);
          if (obj == NULL) {
              return;
          }
          state = CI_PGCW_MS_MARK;
          break;
      }

      default:
        abort();
    }
    }
}

#define CI_GC_BACKOFF_MIN 4
#define CI_GC_BACKOFF_MAX 12

// This clever implementation was stolen from Julia's parallel
// GC.
static void
Ci_gc_steal_backoff(int *i)
{
    if (*i < CI_GC_BACKOFF_MAX) {
        (*i)++;
    }
    for (int j = 0; j < (1 << *i); j++) {
        Ci_cpu_pause();
    }
}

static int
Ci_ParGCWorker_TakeStealCoordinator(Ci_ParGCWorker *worker)
{
    int success = 0;
    Ci_ParGCState *par_gc = worker->par_gc;
    MUTEX_LOCK(par_gc->steal_coord_lock);
    if (par_gc->steal_coordinator == NULL) {
        par_gc->steal_coordinator = worker;
        success = 1;
    }
    MUTEX_UNLOCK(par_gc->steal_coord_lock);
    return success;
}

static void
Ci_ParGCWorker_DropStealCoordinator(Ci_ParGCWorker *worker)
{
    Ci_ParGCState *par_gc = worker->par_gc;
    MUTEX_LOCK(par_gc->steal_coord_lock);
    assert(par_gc->steal_coordinator == worker);
    par_gc->steal_coordinator = NULL;
    MUTEX_UNLOCK(par_gc->steal_coord_lock);
}

#define CI_UNITS_PER_WORKER 1

static void
Ci_ParGCWorker_CoordinateStealing(Ci_ParGCWorker *worker)
{
    int backoff = CI_GC_BACKOFF_MIN;
    Ci_ParGCState *par_gc = worker->par_gc;
    size_t num_workers = par_gc->num_workers;
    while (1) {
        // Marking is finished if we're the only active worker
        int num_workers_marking = _Py_atomic_load(&par_gc->num_workers_marking);
        if (num_workers_marking == 1) {
            _Py_atomic_fetch_sub(&par_gc->num_workers_marking, 1);
            Ci_Sema_Post(&par_gc->steal_sema, num_workers - num_workers_marking);
            return;
        }

        // Compute available work
        size_t work_available = 0;
        for (size_t i = 0; i < num_workers; i++) {
            work_available += Ci_WSDeque_Size(&par_gc->workers[i].deque);
        }

        // Figure out how many workers need to be woken up
        if (work_available) {
            size_t num_workers_to_wake_up = work_available / CI_UNITS_PER_WORKER;
            size_t num_inactive_workers = num_workers - num_workers_marking ;
            if (num_workers_to_wake_up > num_inactive_workers) {
                num_workers_to_wake_up = num_inactive_workers;
            }
            if (num_workers_to_wake_up > 0) {
                CI_DLOG("Waking up %lu workers, %d active, %lu inactive\n", num_workers_to_wake_up, num_workers_marking, num_inactive_workers);
                // We need to increment the number of workers marking in the
                // coordinator, rather than in each worker, to avoid a race
                // condition where a worker is woken up but doesn't run before
                // the next time the coordinator checks the number of workers
                // marking. In that scenario, if the worker that was awakened
                // was the only other active worker then the coordinator would
                // incorrectly terminate marking because the numbers of workers
                // marking wouldn't have been updated.
                _Py_atomic_fetch_add(&par_gc->num_workers_marking, num_workers_to_wake_up);
                Ci_Sema_Post(&par_gc->steal_sema, num_workers_to_wake_up);
            }
            return;
        }

        Ci_gc_steal_backoff(&backoff);
    }
}

static void
Ci_ParGCWorker_MarkReachable(Ci_ParGCWorker *worker)
{
    Ci_ParGCWorker_MarkGCSlice(worker);

    do {
        Ci_ParGCWorker_ProcessMarkQueueAndSteal(worker);

        if (Ci_ParGCWorker_TakeStealCoordinator(worker)) {
            CI_DLOG("Took steal coordinator");
            Ci_ParGCWorker_CoordinateStealing(worker);
            Ci_ParGCWorker_DropStealCoordinator(worker);
            CI_DLOG("Dropped steal coordinator");
        } else {
            // Wait until the coordinator wakes us up
            CI_DLOG("Waiting for coordinator");
            _Py_atomic_fetch_sub(&worker->par_gc->num_workers_marking, 1);
            Ci_Sema_Wait(&worker->par_gc->steal_sema);
        }
    } while (_Py_atomic_load(&worker->par_gc->num_workers_marking));
}

static
void Ci_ParGCWorker_Run(Ci_ParGCWorker *worker)
{
    Ci_ParGCState *par_gc = worker->par_gc;

    _Py_atomic_fetch_add(&par_gc->num_workers_active, 1);
    CI_DLOG("Worker started");

    // Subtract outgoing references from all GC objects in the generation
    // being collected that refer to other objects in the same generation.
    worker->subtract_refs_load = 0;
    Ci_ParGCWorker_SubtractRefs(worker);

    // Wait until all other workers are finished subtracting refs, then
    // mark all reachable objects from objects that are known to be live.
    Ci_Barrier_Wait(&par_gc->mark_barrier);
    worker->mark_load = 0;
    worker->steal_attempts = 0;
    worker->steal_successes = 0;
    Ci_ParGCWorker_MarkReachable(worker);

    // Notify main thread that work is complete
    CI_DLOG("Worker done");
    Ci_Barrier_Wait(&par_gc->done_barrier);
    _Py_atomic_fetch_sub(&par_gc->num_workers_active, 1);
}

static void
Ci_ParGCWorker_Init(Ci_ParGCWorker *worker, Ci_ParGCState *par_gc, unsigned int seed)
{
    worker->gc_slice.start = NULL;
    worker->gc_slice.end = NULL;
    Ci_WSDeque_Init(&worker->deque);
    worker->subtract_refs_load = 0;
    worker->mark_load = 0;
    worker->par_gc = par_gc;
    worker->seed = seed;
    worker->thread_id = 0;
}

static void
Ci_ParGCWorker_Fini(Ci_ParGCWorker *worker)
{
    Ci_WSDeque_Fini(&worker->deque);
}

// Stolen from os_cpu_count_impl in posixmodule.c
static int
Ci_get_num_processors()
{
    int ncpu = 1;
#ifdef MS_WINDOWS
    ncpu = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#elif defined(__hpux)
    ncpu = mpctl(MPC_GETNUMSPUS, NULL, NULL);
#elif defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_ONLN)
    ncpu = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__VXWORKS__)
    ncpu = _Py_popcount32(vxCpuEnabledGet());
#elif defined(__DragonFly__) || \
      defined(__OpenBSD__)   || \
      defined(__FreeBSD__)   || \
      defined(__NetBSD__)    || \
      defined(__APPLE__)
    int mib[2];
    size_t len = sizeof(ncpu);
    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    if (sysctl(mib, 2, &ncpu, &len, NULL, 0) != 0)
        ncpu = 1;
#endif
    return ncpu;
}

static int
Ci_get_default_num_par_gc_threads()
{
    int num_threads = Ci_get_num_processors() / 2;
    return num_threads > 0 ? num_threads : 1;
}

static void
Ci_ParGCState_Destroy(Ci_ParGCState *par_gc);

static Ci_ParGCState *
Ci_ParGCState_New(size_t min_gen, size_t num_threads)
{
    if (min_gen >= NUM_GENERATIONS) {
        _PyErr_SetString(_PyThreadState_GET(), PyExc_ValueError, "invalid generation");
        return NULL;
    }
    if (num_threads == 0) {
        num_threads = Ci_get_default_num_par_gc_threads();
    }

    Ci_ParGCState *par_gc = (Ci_ParGCState *) PyMem_RawCalloc(1, sizeof(Ci_ParGCState) + sizeof(Ci_ParGCWorker) * num_threads);
    if (par_gc == NULL) {
        PyErr_SetString(PyExc_MemoryError, "out of memory");
        return NULL;
    }

    par_gc->gc_impl.collect = gc_collect_main;
    par_gc->gc_impl.finalize = (Ci_gc_finalize_t) Ci_ParGCState_Destroy;
    par_gc->min_gen = min_gen;

    Ci_Barrier_Init(&par_gc->mark_barrier, num_threads);
    _Py_atomic_store(&par_gc->num_workers_marking, 0);

    MUTEX_INIT(par_gc->steal_coord_lock);
    par_gc->steal_coordinator = NULL;
    Ci_Sema_Init(&par_gc->steal_sema);

    // All worker threads + the main thread
    Ci_Barrier_Init(&par_gc->done_barrier, num_threads + 1);
    _Py_atomic_store(&par_gc->num_workers_active, 0);

    par_gc->num_workers = num_threads;
    for (size_t i = 0; i < num_threads; i++) {
        Ci_ParGCWorker_Init(&par_gc->workers[i], par_gc, i);
    }

    CI_DLOG("Enabling parallel gc with %zu threads", num_threads);

    return par_gc;
}

static void
Ci_ParGCState_Destroy(Ci_ParGCState *par_gc)
{
    // Wait until all workers are done before destroying shared state.
    //
    // During finalization, the interpreter will perform a final collection
    // immediately before destroying GC state. Depending on the vagaries of the
    // OS scheduler, we may reach this point before some worker threads have
    // been woken up. When that occurs, they will be paused at
    //
    //     Ci_Barrier_Wait(&par_gc->done_barrier);
    //
    // and will still need access to the synchronization primitives in
    // `par_gc->done_barrier`. We must wait until they have proceeded past this
    // point before we can safely finalize par_gc.
    //
    // A simple solution would be to join each thread, but that would require
    // joining each thread at the end of every collection, slowing things
    // down. Additionally, the Python C-API does not support this. Instead,
    // each worker decrements `par_gc->num_workers_active` as the last
    // operation it performs before exiting. Since no future collections will
    // be performed once we reach this point, we can be sure that all workers
    // no longer need access to any shared state once
    // `par_gc->num_workers_active` reaches zero.
    while (_Py_atomic_load(&par_gc->num_workers_active)) {
        Ci_cpu_pause();
    }

    Ci_PyGCImpl *old_impl = par_gc->old_impl;
    if (old_impl != NULL) {
        old_impl->finalize(old_impl);
    }

    Ci_Barrier_Fini(&par_gc->mark_barrier);
    Ci_Barrier_Fini(&par_gc->done_barrier);
    MUTEX_FINI(par_gc->steal_coord_lock);
    Ci_Sema_Fini(&par_gc->steal_sema);

    for (size_t i = 0; i < par_gc->num_workers; i++) {
        Ci_ParGCWorker_Fini(&par_gc->workers[i]);
    }

    PyMem_RawFree(par_gc);
}

// Assign workers slices of the gc list `base` for processing
static void
Ci_assign_worker_slices(Ci_ParGCWorker *workers, int num_workers, PyGC_Head *base, int num_objects)
{
    assert(num_objects >= num_workers);

    for (int i = 0; i < num_workers; i++) {
        workers[i].gc_slice.start = NULL;
        workers[i].gc_slice.end = NULL;
    }
    int idx = 0;
    int seen = 0;
    int objs_per_slice = num_objects / num_workers;
    for (PyGC_Head *gc = GC_NEXT(base); gc != base; gc = GC_NEXT(gc)) {
        idx = seen / objs_per_slice;
        idx = idx < num_workers ? idx : num_workers - 1;
        if (workers[idx].gc_slice.start == NULL) {
            // Start a new slice and close the previous one
            workers[idx].gc_slice.start = gc;
            if (idx > 0) {
                workers[idx - 1].gc_slice.end = gc;
            }
        }
        seen++;
    }
    assert(idx == num_workers - 1);
    workers[idx].gc_slice.end = base;
}

static void
Ci_report_load(Ci_ParGCWorker *workers, int num_workers)
{
    CI_STAT("%-17s  %-10s  %-13s  %-11s  %-11s  %-13s", "Thread ID", "mark load", "sub_refs load", "steal succs", "steal tries", "deque resizes");
    unsigned long total_mark_load = 0;
    unsigned long total_subtract_refs_load = 0;
    unsigned long total_steal_attempts = 0;
    unsigned long total_steals = 0;
    for (int i = 0; i < num_workers; i++) {
        Ci_ParGCWorker *w = &workers[i];
        CI_STAT("T%-16lu  %-10lu  %-13lu  %-11lu  %-11lu  %-13d", w->thread_id, w->mark_load, w->subtract_refs_load, w->steal_successes, w->steal_attempts, Ci_WSDeque_GetNumResizes(&w->deque));
        total_mark_load += w->mark_load;
        total_subtract_refs_load += w->subtract_refs_load;
        total_steal_attempts += w->steal_attempts;
        total_steals += w->steal_successes;
    }
    CI_STAT("         total mark load: %lu", total_mark_load);
    CI_STAT("total subtract_refs load: %lu", total_subtract_refs_load);
    CI_STAT("     steal success ratio: %lu/%lu (%.2f%%)", total_steals, total_steal_attempts, 100.0 * total_steals / total_steal_attempts);
}

static void
Ci_move_unreachable_parallel(PyGC_Head *base, PyGC_Head *unreachable)
{
    // Visit all GC objects, moving anything with a refcount of 0 to unreachable, and fix
    // up prev pointers.
    PyGC_Head *prev = base;
    PyGC_Head *gc = GC_NEXT(base);
    while (gc != base) {
        if (gc_get_refs(gc) == 0) {
            // Splice gc out of base. The next iteration of the loop will fix up
            // the prev pointers.
            _PyGCHead_SET_NEXT(prev, GC_NEXT(gc));

            // Insert gc into unreachable.
            // We can't use gc_list_append() here because we use
            // NEXT_MASK_UNREACHABLE here.
            PyGC_Head *last = GC_PREV(unreachable);
            // NOTE: Since all objects in unreachable set has
            // NEXT_MASK_UNREACHABLE flag, we set it unconditionally.
            // But this may pollute the unreachable list head's 'next' pointer
            // too. That's semantically senseless but expedient here - the
            // damage is repaired when this function ends.
            last->_gc_next = (NEXT_MASK_UNREACHABLE | (uintptr_t)gc);
            _PyGCHead_SET_PREV(gc, last);
            gc->_gc_next = (NEXT_MASK_UNREACHABLE | (uintptr_t)unreachable);
            unreachable->_gc_prev = (uintptr_t)gc;

            gc = GC_NEXT(prev);
        } else {
            _PyGCHead_SET_PREV(gc, prev);
            gc_clear_collecting(gc);

            prev = gc;
            gc = GC_NEXT(gc);
        }

    }

    // base->_gc_prev must be last element remained in the list.
    _PyGCHead_SET_PREV(base, prev);
    // don't let the pollution of the list head's next pointer leak
    unreachable->_gc_next &= ~NEXT_MASK_UNREACHABLE;
}

/* Deduce which objects among "base" are unreachable from outside the list in
   parallel and move them to 'unreachable'.

   This uses the same basic approach as `deduce_unreachable`, but parallelizes
   it across a number of worker threads. Figuring out the unreachable set is
   split across three conceptual phases:

   1. Iterate across the generation being collected and store each object's
      refcount in the `prev` field of the doubly linked list, called its
      `gc_refcount`.
   2. For each object in the generation being collected, subtract all of its
      outgoing references from the `gc_refcount` of other objects in the same
      generation. After this, all objects with a `gc_refcount` > 0 are
      reachable from outside of the generation being collected and are
      considered live.
   3. For each live object from (2), mark any objects that are transitively
      reachable as live (by setting their `gc_refcount` to a value > 0).
   4. All objects left in the generation being collected with a `gc_refcount`
      of 0 are unreachable.

   Step two of this process is parallelized roughly as follows:

   1. The main GC thread assigns each worker thread a slice of the GC list that
      it should process.
   2. The main GC thread wakes up each worker thread and waits for them all to
      finish.
   3. Each worker thread performs step (2) from above on its slice of the GC list
      and notifies the main thread when its complete.

   The static partitioning approach has good (~linear) scaling properties when
   the number of outgoing references in each GC chunk is roughly equal, but can
   become imbalanced if a subset of the GC chunks contain objects with a
   disproportionate number of outgoing references (e.g. large lists or
   dictionaries). We can adapt the work stealing approach used below to provide
   better load balancing, should it become an issue.

   Parallelization of step three is divided between static partitioning and
   coordinated work stealing:

   1. Each worker thread processes its slice of the GC list, queuing objects
      that are reachable from live objects in the list for further processing.
   2. Each worker thread processes all of the objects in its queue, enqueuing
      newly discovered objects for further processing.
   3. Once the queue is empty, it attempts to steal work from other workers,
      returning to step (2) if it successfully steals work.
   4. When a worker fails to steal work it either becomes the steal coordinator
      or waits to be woken up by current coordinator, either because the
      coordinator thinks there is work to steal, or because marking has
      finished.

   The steal coordinator is responsible for ensuring that the number of workers
   that are attempting to steal work is proportional to the amount of work
   that is available to steal. This dramatically reduces the number of cycles
   that are wasted by workers that fail to steal work.

Contracts:

    * The "base" has to be a valid list with no mask set.

    * The "unreachable" list must be uninitialized (this function calls
      gc_list_init over 'unreachable').

IMPORTANT: This function leaves 'unreachable' with the NEXT_MASK_UNREACHABLE
flag set but it does not clear it to skip unnecessary iteration. Before the
flag is cleared (for example, by using 'clear_unreachable_mask' function or
by a call to 'move_legacy_finalizers'), the 'unreachable' list is not a normal
list and we can not use most gc_list_* functions for it. */
static void
Ci_deduce_unreachable_parallel(Ci_ParGCState *par_gc, PyGC_Head *base, PyGC_Head *unreachable)
{
    validate_list(base, collecting_clear_unreachable_clear);

    unsigned int num_objects = update_refs(base);
    if (num_objects < par_gc->num_workers) {
        CI_DLOG("Too few objects to justify parallel collection. Collecting serially.");
        deduce_unreachable(base, unreachable);
        return;
    }

    CI_DLOG("Starting parallel collection of %d objects", num_objects);

    _Py_atomic_store(&par_gc->num_workers_marking, par_gc->num_workers);
    Ci_assign_worker_slices(par_gc->workers, par_gc->num_workers, base, num_objects);
    Ci_ParGCWorker *workers = par_gc->workers;
    for (size_t i = 0; i < par_gc->num_workers; i++) {
        workers[i].thread_id = PyThread_start_new_thread((void (*)(void *)) Ci_ParGCWorker_Run, &workers[i]);
    }

    Ci_Barrier_Wait(&par_gc->done_barrier);

    gc_list_init(unreachable);
    Ci_move_unreachable_parallel(base, unreachable);
    validate_list(base, collecting_clear_unreachable_clear);
    validate_list(unreachable, collecting_set_unreachable_set);

    if (CI_LOG_LEVEL) {
        Ci_report_load(par_gc->workers, par_gc->num_workers);
    }
    CI_DLOG("Done with parallel collection");
}

static int
Ci_is_par_gc(Ci_PyGCImpl *impl)
{
    return impl->collect == gc_collect_main && impl->finalize == (Ci_gc_finalize_t) Ci_ParGCState_Destroy;
}

int
Cinder_EnableParallelGC(size_t min_gen, size_t num_threads)
{
    PyThreadState *tstate = _PyThreadState_GET();
#ifdef HAVE_WS_DEQUE
    GCState *gc_state = &tstate->interp->gc;
    Ci_PyGCImpl *impl = Ci_PyGC_GetImpl(gc_state);
    if (Ci_is_par_gc(impl)) {
        return 0;
    }

    CI_INIT_LOGGING();
    Ci_ParGCState *par_gc = Ci_ParGCState_New(min_gen, num_threads);
    if (par_gc == NULL) {
        return -1;
    }

    Ci_PyGCImpl *old_impl = Ci_PyGC_SetImpl(gc_state, (Ci_PyGCImpl *) par_gc);
    if (old_impl == NULL) {
        Ci_ParGCState_Destroy(par_gc);
        return -1;
    }

    par_gc->old_impl = old_impl;

    return 0;
#else
    _PyErr_SetString(tstate, PyExc_RuntimeError, "not supported on this platform");
    return -1;
#endif
}

PyObject *
Cinder_GetParallelGCSettings()
{
    PyThreadState *tstate = _PyThreadState_GET();
    struct _gc_runtime_state *gc_state = &tstate->interp->gc;

    Ci_PyGCImpl *impl = Ci_PyGC_GetImpl(gc_state);
    if (!Ci_is_par_gc(impl)) {
        Py_RETURN_NONE;
    }

    Ci_ParGCState *par_gc = (Ci_ParGCState *) impl;
    PyObject *settings = PyDict_New();
    if (settings == NULL) {
        return NULL;
    }

    PyObject *num_threads = PyLong_FromLong(par_gc->num_workers);
    if (num_threads == NULL) {
        Py_DECREF(settings);
        return NULL;
    }
    if (PyDict_SetItemString(settings, "num_threads", num_threads) < 0) {
        Py_DECREF(num_threads);
        Py_DECREF(settings);
        return NULL;
    }
    Py_DECREF(num_threads);

    PyObject *min_gen = PyLong_FromLong(par_gc->min_gen);
    if (min_gen == NULL) {
        Py_DECREF(settings);
        return NULL;
    }
    if (PyDict_SetItemString(settings, "min_generation", min_gen) < 0) {
        Py_DECREF(min_gen);
        Py_DECREF(settings);
        return NULL;
    }
    Py_DECREF(min_gen);

    return settings;
}

void
Cinder_DisableParallelGC()
{
    PyThreadState *tstate = _PyThreadState_GET();
    struct _gc_runtime_state *gc_state = &tstate->interp->gc;

    Ci_PyGCImpl *impl = Ci_PyGC_GetImpl(gc_state);
    if (Ci_is_par_gc(impl)) {
        Ci_ParGCState *par_gc = (Ci_ParGCState *) impl;
        Ci_PyGC_SetImpl(gc_state, par_gc->old_impl);
        par_gc->old_impl = NULL;
        impl->finalize(impl);
    }
}
