#ifndef JIT_CODE_ALLOCATOR_H
#define JIT_CODE_ALLOCATOR_H

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This provides a very simple allocator for executable memory that is
 * intended to be used by the master jit.
 *
 * Allocation proceeds roughly as follows:
 *
 * 1. At startup, a large, contiguous block of executable memory is allocated.
 *    This will remain live until interpreter shutdown.
 * 2. Memory is allocated from the block using a bump pointer on demand.
 * 3. When the master is about to fork (or we run out of memory), the allocator
 *    is frozen, and no more allocations can be performed.
 * 4. The block of memory is released back to the OS right before the
 * interpreter shuts down.
 *
 */

typedef struct code_allocator CodeAllocator;

/*
 * Initialize a new allocator that manages size bytes of memory.
 *
 * size is rounded up to nearest multiple of the system page size.
 *
 * Returns NULL on error.
 */
CodeAllocator* CodeAllocator_New(size_t size);

/*
 * Allocate size bytes of executable memory.
 *
 * size is rounded up to the nearest multiple of 16 to ensure jitted
 * functions are always 16 byte aligned.
 *
 * Returns a pointer to the first byte of executable memory on success.
 *
 * Returns NULL on error and sets errno to one of the following values:
 *
 *     ENOMEM - Not enough space left to allocate
 *
 */
void* CodeAllocator_Allocate(CodeAllocator* allocator, size_t size);

/*
 * Destroy an allocator and release the memory back to the OS.
 *
 * Returns -1 on error.
 */
int CodeAllocator_Free(CodeAllocator* allocator);

#ifdef __cplusplus
}
#endif
#endif /* JIT_CODE_ALLOCATOR_H */
