#include "Python.h"
#include "arraymodule.h"

#include "cinder/porting-support.h"

#include "Jit/log.h"


#define STUB(ret, func, args...) ret func(args) { \
    PORT_ASSERT(Hit stubbed function: func); \
  }


// Objects/genobject.c
// TODO(T125856469) Eager coroutine execution
STUB(PyObject *, _PyWaitHandle_New, PyObject *, PyObject *)
STUB(void, _PyWaitHandle_Release, PyObject *)
PyTypeObject PyWaitHandle_Type = {
    .ob_base = PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "wait handle NOT IMPLEMENTED",
    .tp_basicsize = sizeof(PyWaitHandleObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
};

// TODO(T125845107) Shadow frames
STUB(PyObject *, _PyCoro_NewNoFrame, PyThreadState *, PyCodeObject *)
STUB(PyObject *, _PyAsyncGen_NewNoFrame, PyCodeObject *)
STUB(PyObject *, _PyGen_NewNoFrame, PyCodeObject *)


// Include/cpython/abstract.h  TODO(T125856469) Eager coroutine execution
STUB(Py_ssize_t, PyVectorcall_FLAGS, size_t)


// Objects/call.c  TODO(T125856469) Eager coroutine execution
STUB(PyObject *, _PyVectorcall_Call, PyObject *, PyObject *, PyObject *, size_t)


// Python/arraymodule.c  TODO(T124996100) Static Python

// If we decide to move the array module into CPython core we'll need to
// figure out how we want to expose PyArray_Type to the JIT's type system.
// 75bf107c converted the module to use heap types stored in the module's state.
PyTypeObject PyArray_Type = {
    .ob_base = PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "array stub NOT IMPLEMENTED",
    .tp_basicsize = sizeof(PyStaticArrayObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
};
