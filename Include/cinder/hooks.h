// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "cinder/exports.h"
#include "pystate.h" // PyThreadState

/* Hooks needed by CinderX that have not been added to upstream. */

CiAPI_DATA(int) _PyEval_ShadowByteCodeEnabled;
CiAPI_DATA(int) _PyShadow_PolymorphicCacheEnabled;

// An integer flag set to 1 if the hooks are enabled.
CiAPI_DATA(int8_t) Ci_cinderx_initialized;

/* Hooks for JIT type profiling. */

typedef void(*Ci_TypeCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_created;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_destroyed;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_name_modified;

typedef int (*Ci_HookType_JIT_GetProfileNewInterpThread)(void);
CiAPI_DATA(Ci_HookType_JIT_GetProfileNewInterpThread)
    Ci_hook_JIT_GetProfileNewInterpThread;

/* Hooks for JIT Shadow frames*/

typedef PyFrameObject *(*Ci_HookType_JIT_GetFrame)(PyThreadState *tstate);
CiAPI_DATA(Ci_HookType_JIT_GetFrame) Ci_hook_JIT_GetFrame;

typedef PyCodeObject *(*Ci_HookType_ShadowFrame_GetCode_JIT)(
    _PyShadowFrame *shadow_frame);
CiAPI_DATA(Ci_HookType_ShadowFrame_GetCode_JIT) Ci_hook_ShadowFrame_GetCode_JIT;

typedef int (*Ci_HookType_ShadowFrame_HasGen_JIT)(_PyShadowFrame *shadow_frame);
CiAPI_DATA(Ci_HookType_ShadowFrame_HasGen_JIT) Ci_hook_ShadowFrame_HasGen_JIT;

typedef PyObject *(*Ci_HookType_ShadowFrame_GetModuleName_JIT)(
    _PyShadowFrame *shadow_frame);
CiAPI_DATA(Ci_HookType_ShadowFrame_GetModuleName_JIT)
    Ci_hook_ShadowFrame_GetModuleName_JIT;

typedef int (*Ci_HookType_ShadowFrame_WalkAndPopulate)(
    PyCodeObject **async_stack, int *async_linenos, PyCodeObject **sync_stack,
    int *sync_linenos, int array_capacity, int *async_stack_len_out,
    int *sync_stack_len_out);
CiAPI_DATA(Ci_HookType_ShadowFrame_WalkAndPopulate)
    Ci_hook_ShadowFrame_WalkAndPopulate;

/* Hooks for Static Python. */
typedef int(*Ci_TypeRaisingCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeRaisingCallback) Ci_hook_type_pre_setattr;

typedef int(*Ci_TypeAttrRaisingCallback)(PyTypeObject *type, PyObject *name, PyObject *value);
CiAPI_DATA(Ci_TypeAttrRaisingCallback) Ci_hook_type_setattr;

typedef vectorcallfunc (*Ci_HookType_PyCMethod_New)(PyMethodDef *method);
CiAPI_DATA(Ci_HookType_PyCMethod_New) Ci_hook_PyCMethod_New;

typedef vectorcallfunc (*Ci_HookType_PyDescr_NewMethod)(PyMethodDef *method);
CiAPI_DATA(Ci_HookType_PyDescr_NewMethod) Ci_hook_PyDescr_NewMethod;

typedef int (*Ci_HookType_type_dealloc)(PyTypeObject *type);
CiAPI_DATA(Ci_HookType_type_dealloc) Ci_hook_type_dealloc;

typedef int (*Ci_HookType_type_traverse)(PyTypeObject *type, visitproc visit,
                                         void *arg);
CiAPI_DATA(Ci_HookType_type_traverse) Ci_hook_type_traverse;

typedef void (*Ci_HookType_type_clear)(PyTypeObject *type);
CiAPI_DATA(Ci_HookType_type_clear) Ci_hook_type_clear;

typedef int (*Ci_HookType_add_subclass)(PyTypeObject *base, PyTypeObject *type);
CiAPI_DATA(Ci_HookType_add_subclass) Ci_hook_add_subclass;

/* Hooks for Shadow Code */
typedef int (*Ci_HookType__PyShadow_FreeAll)(void);
CiAPI_DATA(Ci_HookType__PyShadow_FreeAll) Ci_hook__PyShadow_FreeAll;

/* Wrappers to expose private functions for usage with hooks. */

CiAPI_FUNC(int) Cix_cfunction_check_kwargs(PyThreadState *tstate,
                                           PyObject *func, PyObject *kwnames);

CiAPI_FUNC(PyObject *)
    Cix_descr_get_qualname(PyDescrObject *descr, void *closure);

typedef void (*Cix_funcptr)(void);
CiAPI_FUNC(Cix_funcptr)
    Cix_cfunction_enter_call(PyThreadState *tstate, PyObject *func);
CiAPI_FUNC(Cix_funcptr)
    Cix_method_enter_call(PyThreadState *tstate, PyObject *func);

typedef void (*Ci_HookType_WalkStack)(PyThreadState *tstate,
                                      CiWalkStackCallback cb, void *data);
CiAPI_DATA(Ci_HookType_WalkStack) Ci_hook_WalkStack;

typedef void (*Ci_HookType_code_sizeof_shadowcode)(struct _PyShadowCode *shadow,
                                                   Py_ssize_t *res);
CiAPI_DATA(Ci_HookType_code_sizeof_shadowcode) Ci_hook_code_sizeof_shadowcode;

typedef int (*Ci_HookType_PyShadowFrame_HasGen)(struct _PyShadowFrame *sf);
CiAPI_DATA(Ci_HookType_PyShadowFrame_HasGen) Ci_hook_PyShadowFrame_HasGen;

typedef PyGenObject *(*Ci_HookType_PyShadowFrame_GetGen)(
    struct _PyShadowFrame *sf);
CiAPI_DATA(Ci_HookType_PyShadowFrame_GetGen) Ci_hook_PyShadowFrame_GetGen;

typedef int (*Ci_HookType_PyJIT_GenVisitRefs)(PyGenObject *gen, visitproc visit,
                                              void *arg);
CiAPI_DATA(Ci_HookType_PyJIT_GenVisitRefs) Ci_hook_PyJIT_GenVisitRefs;

typedef void (*Ci_HookType_PyJIT_GenDealloc)(PyGenObject *gen);
CiAPI_DATA(Ci_HookType_PyJIT_GenDealloc) Ci_hook_PyJIT_GenDealloc;

typedef PyObject *(*Ci_HookType_PyJIT_GenSend)(PyGenObject *gen, PyObject *arg,
                                               int exc, PyFrameObject *f,
                                               PyThreadState *tstate,
                                               int finish_yield_from);
CiAPI_DATA(Ci_HookType_PyJIT_GenSend) Ci_hook_PyJIT_GenSend;

typedef PyObject *(*Ci_HookType_PyJIT_GenYieldFromValue)(PyGenObject *gen);
CiAPI_DATA(Ci_HookType_PyJIT_GenYieldFromValue) Ci_hook_PyJIT_GenYieldFromValue;

typedef PyFrameObject *(*Ci_HookType_PyJIT_GenMaterializeFrame)(
    PyGenObject *gen);
CiAPI_DATA(Ci_HookType_PyJIT_GenMaterializeFrame)
    Ci_hook_PyJIT_GenMaterializeFrame;

typedef PyObject *(*Ci_HookType_MaybeStrictModule_Dict)(PyObject *op);
CiAPI_DATA(Ci_HookType_MaybeStrictModule_Dict) Ci_hook_MaybeStrictModule_Dict;

CiAPI_FUNC(PyObject *)
    Cix_method_get_doc(PyMethodDescrObject *descr, void *closure);

CiAPI_FUNC(PyObject *)
    Cix_method_get_text_signature(PyMethodDescrObject *descr, void *closure);

CiAPI_FUNC(PyObject *) Cix_meth_get__doc__(PyCFunctionObject *m, void *closure);

CiAPI_FUNC(PyObject *)
    Cix_meth_get__name__(PyCFunctionObject *m, void *closure);

CiAPI_FUNC(PyObject *)
    Cix_meth_get__qualname__(PyCFunctionObject *m, void *closure);

CiAPI_FUNC(PyObject *)
    Cix_meth_get__self__(PyCFunctionObject *m, void *closure);

CiAPI_FUNC(PyObject *)
    Cix_meth_get__text_signature__(PyCFunctionObject *m, void *closure);

CiAPI_DATA(_PyFrameEvalFunction) Ci_hook_EvalFrame;

typedef PyFrameObject *(*Ci_HookType_PyJIT_GetFrame)(PyThreadState *tstate);
CiAPI_DATA(Ci_HookType_PyJIT_GetFrame) Ci_hook_PyJIT_GetFrame;

typedef PyObject *(*Ci_HookType_PyJIT_GetBuiltins)(PyThreadState *tstate);
CiAPI_DATA(Ci_HookType_PyJIT_GetBuiltins) Ci_hook_PyJIT_GetBuiltins;

typedef PyObject *(*Ci_HookType_PyJIT_GetGlobals)(PyThreadState *tstate);
CiAPI_DATA(Ci_HookType_PyJIT_GetGlobals) Ci_hook_PyJIT_GetGlobals;

typedef int (*Ci_HookType_PyJIT_GetCurrentCodeFlags)(PyThreadState *tstate);
CiAPI_DATA(Ci_HookType_PyJIT_GetCurrentCodeFlags)
    Ci_hook_PyJIT_GetCurrentCodeFlags;

#ifdef __cplusplus
} // extern "C"
#endif
