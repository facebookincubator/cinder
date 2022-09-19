#include "Python.h"

#include "cinder/porting-support.h"

#include "Jit/log.h"


#define STUB(ret, func, args...) ret func(args) { \
    PORT_ASSERT(Hit stubbed function: func); \
  }


// Objects/genobject.c
STUB(PyObject *, _PyAsyncGen_NewNoFrame, PyCodeObject *)
STUB(PyObject *, _PyGen_NewNoFrame, PyCodeObject *)
