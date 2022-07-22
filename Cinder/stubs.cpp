#include "Python.h"
#include "arraymodule.h"
#include "classloader.h"
#include "funccredobject.h"
#include "switchboard.h"

#include "cinder/porting-support.h"

#include "Jit/log.h"


#define STUB(ret, func, args...) ret func(args) { \
    PORT_ASSERT(Hit stubbed function: func); \
  }


// classloader.c
STUB(PyObject *, _PyClassLoader_NewAwaitableWrapper, PyObject *, int, PyObject *, awaitable_cb, awaitable_presend)
STUB(Py_ssize_t, _PyClassLoader_ResolveMethod, PyObject *)
STUB(Py_ssize_t, _PyClassLoader_ResolveFieldOffset, PyObject *, int *)
STUB(int, _PyClassLoader_ResolvePrimitiveType, PyObject *)
STUB(int, _PyClassLoader_GetTypeCode, PyTypeObject *)
STUB(PyTypeObject *, _PyClassLoader_ResolveType, PyObject *, int *, int *)
STUB(int, _PyClassLoader_PrimitiveTypeToStructMemberType, int)
STUB(Py_ssize_t, _PyClassLoader_PrimitiveTypeToSize, int)
STUB(int, _PyClassLoader_AddSubclass, PyTypeObject *, PyTypeObject *)
STUB(_PyType_VTable *, _PyClassLoader_EnsureVtable, PyTypeObject *, int)
STUB(int, _PyClassLoader_ClearVtables, void)
STUB(void, _PyClassLoader_ClearGenericTypes, void)
STUB(int, _PyClassLoader_IsPatchedThunk, PyObject *)
STUB(PyObject **, _PyClassLoader_GetIndirectPtr, PyObject *, PyObject *, PyObject *)
STUB(int, _PyClassLoader_IsEnum, PyTypeObject *)
STUB(int, _PyClassLoader_IsImmutable, PyObject *)
STUB(PyObject *, _PyClassLoader_ResolveFunction, PyObject *, PyObject **)
STUB(PyObject *, _PyClassLoader_ResolveReturnType, PyObject *, int *, int *, int *, int *)
STUB(PyMethodDescrObject *, _PyClassLoader_ResolveMethodDef, PyObject *)
STUB(void, _PyClassLoader_ClearCache, void)
STUB(PyObject *, _PyClassLoader_GetReturnTypeDescr, PyFunctionObject *)
STUB(PyObject *, _PyClassLoader_GetCodeReturnTypeDescr, PyCodeObject *)
STUB(int, _PyClassLoader_IsFinalMethodOverridden, PyTypeObject *, PyObject *)
STUB(PyObject *, _PyTypedDescriptor_New, PyObject *, PyObject *, Py_ssize_t)
STUB(PyObject *, _PyTypedDescriptorWithDefaultValue_New, PyObject *, PyObject *, Py_ssize_t, PyObject *)
STUB(int, _PyClassLoader_UpdateModuleName, PyStrictModuleObject *, PyObject *, PyObject *)
STUB(int, _PyClassLoader_UpdateSlot, PyTypeObject *, PyObject *, PyObject *)
STUB(int, _PyClassLoader_InitTypeForPatching, PyTypeObject *)
STUB(_PyTypedArgsInfo *, _PyClassLoader_GetTypedArgsInfo, PyCodeObject *, int)
STUB(_PyTypedArgsInfo*, _PyClassLoader_GetTypedArgsInfoFromThunk, PyObject *, PyObject *, int )
STUB(int, _PyClassLoader_HasPrimitiveArgs, PyCodeObject*)
STUB(PyObject *, _PyClassLoader_GtdGetItem, _PyGenericTypeDef *, PyObject *)


// funccredobject.c
STUB(PyObject *, PyFunctionCredential_New, void)
STUB(void, PyFunctionCredential_Fini, void)


// Objects/moduleobject.c
STUB(int, strictmodule_is_unassigned, PyObject *, PyObject *)
STUB(PyObject *, PyStrictModule_GetOriginal, PyStrictModuleObject *, PyObject *)
STUB(int, _Py_do_strictmodule_patch, PyObject *, PyObject *, PyObject *)


// Python/switchboard.c
STUB(int, Switchboard_Init, void)
STUB(Switchboard *, Switchboard_New, void)
STUB(PyObject *, Switchboard_Subscribe, Switchboard *, PyObject *, Switchboard_Callback, PyObject *)
STUB(int, Switchboard_Notify, Switchboard *, PyObject *)
STUB(Py_ssize_t, Switchboard_GetNumSubscriptions, Switchboard *, PyObject *)
STUB(int, Switchboard_Unsubscribe, Switchboard *, PyObject *)
STUB(int, Switchboard_UnsubscribeAll, Switchboard *, PyObject *)


// Objects/typeobject.c
STUB(void, _PyType_ClearNoShadowingInstances, struct _typeobject *, PyObject *)
STUB(void, _PyType_SetNoShadowingInstances, struct _typeobject *)
STUB(PyObject *, _PyType_GetMethodCacheStats, void)
STUB(void, _PyType_ResetMethodCacheStats, void)
STUB(void, _PyType_SetReadonlyProperties, struct _typeobject *)


// Objects/object.c
STUB(PyObject **, _PyObject_GetDictPtrAtOffset, PyObject *, Py_ssize_t)


// Python/bltinmodule.c
STUB(PyObject *, builtin_next, PyObject *, PyObject *const *, Py_ssize_t)
STUB(PyObject *, _PyBuiltin_Next, PyObject *, PyObject *)


// Objects/listobject.c
STUB(PyObject *, _PyList_Repeat, PyListObject *, Py_ssize_t)
STUB(int, _PyList_APPEND, PyObject *, PyObject *)


// Objects/tupleobject.c
STUB(PyObject *, _PyTuple_Repeat, PyTupleObject *, Py_ssize_t)


// Objects/dictobject.c
STUB(PyObject *, _PyCheckedDict_New, PyTypeObject *)
STUB(PyObject *, _PyCheckedDict_NewPresized, PyTypeObject *, Py_ssize_t)
STUB(int, _PyCheckedDict_Check, PyObject *)
STUB(PyObject *, _PyCheckedList_GetItem, PyObject *, Py_ssize_t)
STUB(PyObject *, _PyCheckedList_New, PyTypeObject *, Py_ssize_t)
STUB(int, _PyCheckedList_TypeCheck, PyTypeObject *)
STUB(int, _PyDict_SetItem, PyObject *, PyObject *, PyObject *)
STUB(PyObject *, _PyDict_GetItemId, PyObject *, struct _Py_Identifier *)
STUB(int, _PyCheckedDict_TypeCheck, PyTypeObject *)


// Python/ceval.c
STUB(int, _Py_DoRaise, PyThreadState *, PyObject *, PyObject *)
STUB(PyObject *, _PyEval_SuperLookupMethodOrAttr, PyThreadState *, PyObject *, PyTypeObject *, PyObject *, PyObject *, int, int *)
STUB(PyObject *, _PyEval_GetAIter, PyObject *)
STUB(PyObject *, _PyEval_GetANext, PyObject *)
STUB(PyObject *, special_lookup, PyThreadState *, PyObject *, _Py_Identifier *)
STUB(int, check_args_iterable, PyThreadState *, PyObject *, PyObject *)
STUB(void, format_kwargs_error, PyThreadState *, PyObject *, PyObject *)
STUB(void, format_awaitable_error, PyThreadState *, PyTypeObject *, int)


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


// Include/errors.h
STUB(int, _PyErr_OCCURRED, )


// Objects/call.c
STUB(PyObject *, _PyVectorcall_Call, PyObject *, PyObject *, PyObject *, size_t)


// Objects/typeobject.c
STUB(PyObject *, _PyType_GetSwitchboard, void)


// Python/pystate.c
STUB(void, _PyThreadState_SetProfileInterpAll, int)


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

// Include/object.h
int Py_IS_IMMORTAL(PyObject *) { return false; }

// Objects/funcobject.c
STUB(PyObject *, _PyFunction_GetSwitchboard, void)
STUB(PyObject *, _PyFunction_GetBuiltins, PyFunctionObject *)
