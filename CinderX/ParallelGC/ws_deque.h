#pragma once

#include "pyconfig.h"

#include <stdlib.h>


#if HAVE_STD_ATOMIC

// TODO(mpage): Add necessary support to pycore_atomic.h so that we can drop the requirement
// on C11 atomics. This will require adding support for:
//
// - The `consume` memory ordering.
// - Some form of atomic compare and swap. Ideally `compare_exchange_strong_explicit`.
//
#include <stdatomic.h>

#define atomic_load_acquire(addr) atomic_load_explicit((addr), memory_order_acquire)
#define atomic_load_consume(addr) atomic_load_explicit((addr), memory_order_consume)
#define atomic_load_relaxed(addr) atomic_load_explicit((addr), memory_order_relaxed)
#define atomic_store_relaxed(addr, val) atomic_store_explicit((addr), (val), memory_order_relaxed)

// This implements the Chase-Lev work stealing deque first described in
//
//   "Dynamic Circular Work-Stealing Deque"
//   (https://dl.acm.org/doi/10.1145/1073970.1073974)
//
// and later specified using C11 atomics in
//
//   "Correct and Efﬁcient Work-Stealing for Weak Memory Models"
//   (https://dl.acm.org/doi/10.1145/2442516.2442524)
//
typedef struct Ci_WSArray {
    // Arrays are linked into a singly linked list as they grow.
    struct Ci_WSArray *next;

    size_t size;
    atomic_uintptr_t buf[];
} Ci_WSArray;

static inline Ci_WSArray *
Ci_WSArray_New(size_t size)
{
    // size must be a power of two > 0
    assert(size > 0 && (size & (size - 1)) == 0);

    Ci_WSArray *arr = calloc(1, sizeof(Ci_WSArray) + sizeof(atomic_uintptr_t) * size);
    arr->size = size;
    return arr;
}

static inline void
Ci_WSArray_Destroy(Ci_WSArray *arr)
{
    if (arr->next != NULL) {
        Ci_WSArray_Destroy(arr->next);
        arr->next = NULL;
    }
    free(arr);
}

static inline void *
Ci_WSArray_Get(Ci_WSArray *arr, size_t idx)
{
    return (void *) atomic_load_relaxed(&(arr->buf[idx & (arr->size - 1)]));
}

static inline void
Ci_WSArray_Put(Ci_WSArray *arr, size_t idx, void *obj)
{
    atomic_store_relaxed(&(arr->buf[idx & (arr->size - 1)]), (uintptr_t) obj);
}

static inline Ci_WSArray *
Ci_WSArray_Grow(Ci_WSArray *arr, size_t top, size_t bot)
{
    size_t new_size = arr->size << 1;
    assert(new_size > arr->size);

    Ci_WSArray *new_arr = Ci_WSArray_New(new_size);
    new_arr->next = arr;

    for (size_t i = top; i < bot; i++) {
        PyObject *obj = Ci_WSArray_Get(arr, i);
        Ci_WSArray_Put(new_arr, i, obj);
    }

    return new_arr;
}

static const size_t kCiParGCInitialArrSize = 1 << 12;

// TODO(mpage): Determine this based on architecture
#define CACHELINE_SIZE 64

typedef struct {
    union {
        atomic_size_t top;
        uint8_t top_padding[CACHELINE_SIZE];
    };
    union {
        atomic_size_t bot;
        uint8_t bot_padding[CACHELINE_SIZE];
    };
    _Atomic(Ci_WSArray *) arr;
    atomic_int num_resizes;
} Ci_WSDeque;

static inline void
Ci_WSDeque_Init(Ci_WSDeque *deque)
{
    Ci_WSArray *arr = Ci_WSArray_New(kCiParGCInitialArrSize);
    atomic_store_relaxed(&deque->arr, arr);
    // This fixes a small bug in the paper. When these are initialized to 0,
    // attempting to `take` on a newly empty deque will succeed; subtracting 1
    // from `bot` will cause it to wrap, and the check for a non-empty deque,
    // `top <= bot`, will succeed. Initializing these both to 1 ensures that
    // bot will not wrap.
    atomic_store_relaxed(&deque->top, 1);
    atomic_store_relaxed(&deque->bot, 1);
    atomic_store_relaxed(&deque->num_resizes, 0);
}

static inline void
Ci_WSDeque_Fini(Ci_WSDeque *deque)
{
    Ci_WSArray_Destroy(deque->arr);
}

static inline PyObject *
Ci_WSDeque_Take(Ci_WSDeque *deque)
{
    assert(atomic_load_relaxed(&deque->bot) != 0);

    size_t bot = atomic_load_relaxed(&deque->bot) - 1;
    Ci_WSArray *arr = atomic_load_relaxed(&deque->arr);
    atomic_store_relaxed(&deque->bot, bot);
    atomic_thread_fence(memory_order_seq_cst);
    size_t top = atomic_load_relaxed(&deque->top);

    PyObject *res = NULL;
    if (top <= bot) {
        // Not empty
        res = Ci_WSArray_Get(arr, bot);
        if (top == bot) {
            // One element in the queue
            if (!atomic_compare_exchange_strong_explicit(&deque->top, &top, top + 1, memory_order_seq_cst, memory_order_relaxed)) {
                // Failed race with another thread stealing from us
                res = NULL;
            }
            atomic_store_relaxed(&deque->bot, bot + 1);
        }
    } else {
        // Empty
        atomic_store_relaxed(&deque->bot, bot + 1);
    }

    return res;
}

static inline void
Ci_WSDeque_Push(Ci_WSDeque *deque, void *obj)
{
    size_t bot = atomic_load_relaxed(&deque->bot);
    size_t top = atomic_load_acquire(&deque->top);
    Ci_WSArray *arr = atomic_load_relaxed(&deque->arr);

    assert(bot >= top);

    if (bot - top > arr->size - 1) {
        // Full, need to grow the underlying array.
        //
        // NB: This differs from the paper. The paper's implementation
        // is specified as the following pseudocode,
        //
        //     resize(q);
        //     a = load_explicit(&q->array, relaxed);
        //
        // however, no implementation is provided for `resize`. Using a relaxed
        // store here should be correct: all other threads will (eventually)
        // see the update atomically and we don't have to worry about another
        // thread growing the array concurrently as only the thread that owns
        // the deque is allowed to do so.
        Ci_WSArray *new_arr = Ci_WSArray_Grow(arr, top, bot);
        atomic_store_relaxed(&deque->arr, new_arr);
        arr = atomic_load_relaxed(&deque->arr);
        atomic_fetch_add_explicit(&deque->num_resizes, 1, memory_order_relaxed);
    }
    Ci_WSArray_Put(arr, bot, obj);
    atomic_thread_fence(memory_order_release);
    atomic_store_relaxed(&deque->bot, bot + 1);
}

static inline void *
Ci_WSDeque_Steal(Ci_WSDeque *deque)
{
    while (1) {
        size_t top = atomic_load_acquire(&deque->top);
        atomic_thread_fence(memory_order_seq_cst);
        size_t bot = atomic_load_acquire(&deque->bot);
        void *res = NULL;
        if (top < bot) {
            // Not empty
            Ci_WSArray *arr = atomic_load_consume(&deque->arr);
            res = Ci_WSArray_Get(arr, top);
            if (!atomic_compare_exchange_strong_explicit(&deque->top, &top, top + 1, memory_order_seq_cst, memory_order_relaxed)) {
                // Lost race
                continue;
            }
        }
        return res;
    }
}

static inline int
Ci_WSDeque_GetNumResizes(Ci_WSDeque *deque)
{
    return atomic_load_relaxed(&deque->num_resizes);
}

static inline size_t
Ci_WSDeque_Size(Ci_WSDeque *deque)
{
    size_t bot = atomic_load_relaxed(&deque->bot);
    size_t top = atomic_load_acquire(&deque->top);
    return bot < top ? 0 : bot - top;
}

#define HAVE_WS_DEQUE 1

#else // !HAVE_STD_ATOMIC

#include <stdio.h>

static inline
void Ci_unimpl()
{
    fprintf(stderr, "this function requires an implementation of stdatomic");
    abort();
}

typedef struct CI_WSArray {
} CI_WSArray;

static inline Ci_WSArray *
Ci_WSArray_New(size_t size) { Ci_unimpl() }

static inline void
Ci_WSArray_Destroy(Ci_WSArray *arr) { Ci_unimpl() }

static inline void *
Ci_WSArray_Get(Ci_WSArray *arr, size_t idx) { Ci_unimpl() }

static inline void
Ci_WSArray_Put(Ci_WSArray *arr, size_t idx, void *obj) { Ci_unimpl() }

static Ci_WSArray *
Ci_WSArray_Grow(Ci_WSArray *arr, size_t top, size_t bot) { Ci_unimpl() }

typedef struct {
} Ci_WSDeque;

static inline void
Ci_WSDeque_Init(Ci_WSDeque *deque) { Ci_unimpl() }

static inline void *
Ci_WSDeque_Take(Ci_WSDeque *deque) { Ci_unimpl() }

static inline void
Ci_WSDeque_Push(Ci_WSDeque *deque, void *obj) { Ci_unimpl() }

static inline PyObject *
Ci_WSDeque_Steal(Ci_WSDeque *deque) { Ci_unimpl() }

static inline int
Ci_WSDeque_GetNumResizes(Ci_WSDeque *deque) { Ci_unimpl() }

static inline size_t
Ci_WSDeque_Size(Ci_WSDeque *deque) { Ci_unimpl() }

#define HAVE_WS_DEQUE 0

#endif // #if HAVE_STD_ATOMIC
