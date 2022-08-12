#include "Python.h"
#include "arraymodule.h"
#include "classloader.h"
#include "funccredobject.h"

#include "cinder/porting-support.h"

#include "Jit/log.h"


#define STUB(ret, func, args...) ret func(args) { \
    PORT_ASSERT(Hit stubbed function: func); \
  }


// classloader.c
STUB(PyObject *, _PyClassLoader_NewAwaitableWrapper, PyObject *, int, PyObject *, awaitable_cb, awaitable_presend)


// funccredobject.c
STUB(PyObject *, PyFunctionCredential_New, void)
STUB(void, PyFunctionCredential_Fini, void)


// Objects/typeobject.c
STUB(PyObject *, _PyType_GetMethodCacheStats, void)
STUB(void, _PyType_ResetMethodCacheStats, void)
STUB(void, _PyType_SetReadonlyProperties, struct _typeobject *)


// Objects/dictobject.c
STUB(PyObject *, _PyCheckedDict_New, PyTypeObject *)
STUB(PyObject *, _PyCheckedDict_NewPresized, PyTypeObject *, Py_ssize_t)
STUB(int, _PyCheckedDict_Check, PyObject *)
STUB(PyObject *, _PyCheckedList_GetItem, PyObject *, Py_ssize_t)
STUB(PyObject *, _PyCheckedList_New, PyTypeObject *, Py_ssize_t)
STUB(int, _PyCheckedList_TypeCheck, PyTypeObject *)
STUB(int, _PyCheckedDict_TypeCheck, PyTypeObject *)


// Python/ceval.c
STUB(PyObject *, _PyEval_SuperLookupMethodOrAttr, PyThreadState *, PyObject *, PyTypeObject *, PyObject *, PyObject *, int, int *)


// Objects/genobject.c
STUB(PyObject *, _PyWaitHandle_New, PyObject *, PyObject *)
STUB(void, _PyWaitHandle_Release, PyObject *)
STUB(PyObject *, _PyCoro_NewNoFrame, PyThreadState *, PyCodeObject *)
STUB(PyObject *, _PyAsyncGen_NewNoFrame, PyCodeObject *)
STUB(PyObject *, _PyGen_NewNoFrame, PyCodeObject *)

PyTypeObject PyWaitHandle_Type = {
    .ob_base = PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "wait handle NOT IMPLEMENTED",
    .tp_basicsize = sizeof(PyWaitHandleObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
};


// Include/genobject.h
STUB(void, _PyAwaitable_SetAwaiter, PyObject *, PyObject *)


// Include/cpython/abstract.h
STUB(Py_ssize_t, PyVectorcall_FLAGS, size_t)
STUB(PyObject *, _PyObject_Call1Arg, PyObject *, PyObject *)


// Objects/frameobject.c
STUB(PyFrameObject *, _PyFrame_NewWithBuiltins_NoTrack, PyThreadState *, PyCodeObject *, PyObject *, PyObject *, PyObject *)


// Objects/call.c
STUB(PyObject *, _PyVectorcall_Call, PyObject *, PyObject *, PyObject *, size_t)


// Python/arraymodule.c
STUB(PyObject *, _PyArray_GetItem, PyObject *, Py_ssize_t)
STUB(int, _PyArray_SetItem, PyObject *, Py_ssize_t, PyObject *)
STUB(int, _PyArray_AppendSigned, PyObject *, int64_t)
STUB(int, _PyArray_AppendUnsigned, PyObject *, uint64_t)

// If we decide to move the array module into CPython core we'll need to
// figure out how we want to expose PyArray_Type to the JIT's type system.
// 75bf107c converted the module to use heap types stored in the module's state.
PyTypeObject PyArray_Type = {
    .ob_base = PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "array stub NOT IMPLEMENTED",
    .tp_basicsize = sizeof(PyStaticArrayObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
};

// Objects/funcobject.c
STUB(PyObject *, _PyFunction_GetBuiltins, PyFunctionObject *)
