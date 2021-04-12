#include "Jit/code_allocator.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "Jit/log.h"

#define DEFAULT_PAGE_SIZE 4096
#define FUNCTION_ALIGNMENT 16

typedef struct {
  uint8_t* start; /* The start of the slab of memory we got from the OS */
  uint8_t* limit; /* The end of the allocated portion of the slab */
  uint8_t* end; /* The end of the slab */
} slab_t;

struct code_allocator {
  slab_t slab;
};

static size_t page_size;

static size_t get_page_size() {
  if (page_size == 0) {
    long result = sysconf(_SC_PAGESIZE);
    if (result == -1) {
      perror("failed getting pagesize, using default");
      page_size = DEFAULT_PAGE_SIZE;
    } else {
      page_size = (size_t)result;
    }
  }
  return page_size;
}

static size_t round_up(size_t size, size_t multiple) {
  size_t remainder = size % multiple;
  if (remainder > 0) {
    size += (multiple - remainder);
  }
  return size;
}

CodeAllocator* CodeAllocator_New(size_t size) {
  CodeAllocator* allocator =
      static_cast<CodeAllocator*>(malloc(sizeof(CodeAllocator)));
  if (allocator == NULL) {
    return NULL;
  }

  size = round_up(size, get_page_size());

  /* TODO(mpage): Lock this down so that memory is W ^ X */
  /* TODO(mpage): Huge pages */
  void* buf = mmap(
      NULL,
      size,
      PROT_READ | PROT_WRITE | PROT_EXEC,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0);
  if (buf == (void*)-1) {
    perror("mmap");
    free(allocator);
    return NULL;
  }

  allocator->slab.start = (uint8_t*)buf;
  allocator->slab.limit = allocator->slab.start;
  allocator->slab.end = allocator->slab.start + size;

  return allocator;
}

void* CodeAllocator_Allocate(CodeAllocator* allocator, size_t size) {
  size_t roundup_size = round_up(size, FUNCTION_ALIGNMENT);
  size_t extra_size = round_up(sizeof(size_t), FUNCTION_ALIGNMENT);
  size_t available = (size_t)(allocator->slab.end - allocator->slab.limit);
  if (available < roundup_size + extra_size) {
    errno = ENOMEM;
    return NULL;
  }

  uint8_t* result = allocator->slab.limit + extra_size;
  allocator->slab.limit += (roundup_size + extra_size);
  (*(size_t*)(result - sizeof(size_t))) = size;

  return result;
}

int CodeAllocator_Free(CodeAllocator* allocator) {
  int result = munmap(
      allocator->slab.start, allocator->slab.end - allocator->slab.start);
  if (result == -1) {
    perror("munmap");
    return result;
  }
  free(allocator);
  return 0;
}
