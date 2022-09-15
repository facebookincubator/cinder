#include "Python.h"
#include "arraymodule.h"

#include "cinder/porting-support.h"

#include "Jit/log.h"


#define STUB(ret, func, args...) ret func(args) { \
    PORT_ASSERT(Hit stubbed function: func); \
  }


// Objects/genobject.c
STUB(PyObject *, _PyAsyncGen_NewNoFrame, PyCodeObject *)
STUB(PyObject *, _PyGen_NewNoFrame, PyCodeObject *)


// Python/arraymodule.c  TODO(T124996100) Static Python

// If we decide to move the array module into CPython core we'll need to
// figure out how we want to expose PyArray_Type to the JIT's type system.
// 75bf107c converted the module to use heap types stored in the module's state.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject PyArray_Type = {
    .ob_base = PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "array stub NOT IMPLEMENTED",
    .tp_basicsize = sizeof(PyStaticArrayObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
};
#pragma GCC diagnostic pop
