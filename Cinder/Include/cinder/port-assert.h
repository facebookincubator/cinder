#pragma once

#include <stdlib.h>
#include <stdio.h>

#define PORT_ASSERT(msg) \
  { \
    fprintf(stderr, "%s @ %d: %s\n", __FILE__, __LINE__, #msg); \
    abort(); \
  }
