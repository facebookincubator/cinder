// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinder/hooks.h"

#include "internal/pycore_shadow_frame.h"

int _PyEval_ShadowByteCodeEnabled = 1;
int _PyShadow_PolymorphicCacheEnabled = 1;

int8_t Ci_cinderx_initialized = 0;

/* JIT type profiling. */
Ci_TypeCallback Ci_hook_type_created = NULL;
Ci_TypeCallback Ci_hook_type_destroyed = NULL;
Ci_TypeCallback Ci_hook_type_name_modified = NULL;
Ci_HookType_JIT_GetProfileNewInterpThread Ci_hook_JIT_GetProfileNewInterpThread = NULL;

/* Hooks for JIT Shadow frames*/
Ci_HookType_JIT_GetFrame Ci_hook_JIT_GetFrame = NULL;
Ci_HookType_ShadowFrame_GetCode_JIT Ci_hook_ShadowFrame_GetCode_JIT = NULL;
Ci_HookType_ShadowFrame_HasGen_JIT Ci_hook_ShadowFrame_HasGen_JIT = NULL;
Ci_HookType_ShadowFrame_GetModuleName_JIT Ci_hook_ShadowFrame_GetModuleName_JIT = NULL;
Ci_HookType_ShadowFrame_WalkAndPopulate Ci_hook_ShadowFrame_WalkAndPopulate = NULL;

/* Static Python. */
Ci_TypeRaisingCallback Ci_hook_type_pre_setattr = NULL;
Ci_TypeAttrRaisingCallback Ci_hook_type_setattr = NULL;
Ci_HookType_PyCMethod_New Ci_hook_PyCMethod_New = NULL;
Ci_HookType_PyDescr_NewMethod Ci_hook_PyDescr_NewMethod = NULL;
Ci_HookType_type_dealloc Ci_hook_type_dealloc = NULL;
Ci_HookType_type_traverse Ci_hook_type_traverse = NULL;
Ci_HookType_type_clear Ci_hook_type_clear = NULL;
Ci_HookType_add_subclass Ci_hook_add_subclass = NULL;

Ci_HookType_WalkStack Ci_hook_WalkStack = NULL;

/* Shadow Code */
Ci_HookType__PyShadow_FreeAll Ci_hook__PyShadow_FreeAll = NULL;
Ci_HookType_code_sizeof_shadowcode Ci_hook_code_sizeof_shadowcode = NULL;

/* Generators */
Ci_HookType_PyJIT_GenVisitRefs Ci_hook_PyJIT_GenVisitRefs = NULL;
Ci_HookType_PyJIT_GenDealloc Ci_hook_PyJIT_GenDealloc = NULL;
Ci_HookType_PyJIT_GenSend Ci_hook_PyJIT_GenSend = NULL;
Ci_HookType_PyJIT_GenYieldFromValue Ci_hook_PyJIT_GenYieldFromValue = NULL;
Ci_HookType_PyJIT_GenMaterializeFrame Ci_hook_PyJIT_GenMaterializeFrame = NULL;

Ci_HookType_MaybeStrictModule_Dict Ci_hook_MaybeStrictModule_Dict = NULL;

/* Interpreter */
_PyFrameEvalFunction Ci_hook_EvalFrame = NULL;
Ci_HookType_PyJIT_GetFrame Ci_hook_PyJIT_GetFrame = NULL;
Ci_HookType_PyJIT_GetBuiltins Ci_hook_PyJIT_GetBuiltins = NULL;
Ci_HookType_PyJIT_GetGlobals Ci_hook_PyJIT_GetGlobals = NULL;
Ci_HookType_PyJIT_GetCurrentCodeFlags Ci_hook_PyJIT_GetCurrentCodeFlags = NULL;

// For backward compatibility, we need this in libpython rather than the
// CinderX module.
int _PyShadowFrame_WalkAndPopulate(
        PyCodeObject** async_stack,
        int* async_linenos,
        PyCodeObject** sync_stack,
        int* sync_linenos,
        int array_capacity,
        int* async_stack_len_out,
        int* sync_stack_len_out) {
    if (Ci_hook_ShadowFrame_WalkAndPopulate == NULL) {
        fprintf(stderr, "CinderX not loaded in _PyShadowFrame_WalkAndPopulate\n");
        async_stack_len_out = 0;
        sync_stack_len_out = 0;
        return -1;
    }
    return
        Ci_hook_ShadowFrame_WalkAndPopulate(
            async_stack,
            async_linenos,
            sync_stack,
            sync_linenos,
            array_capacity,
            async_stack_len_out,
            sync_stack_len_out);
}
