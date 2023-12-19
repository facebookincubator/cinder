// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinder/hooks.h"

int8_t Ci_cinderx_initialized = 0;

/* JIT type profiling. */
Ci_TypeCallback Ci_hook_type_created = NULL;
Ci_TypeCallback Ci_hook_type_destroyed = NULL;
Ci_TypeCallback Ci_hook_type_name_modified = NULL;
Ci_HookType_JIT_GetProfileNewInterpThread Ci_hook_JIT_GetProfileNewInterpThread = NULL;

/* Hooks for JIT Shadow frames*/
Ci_HookType_JIT_GetFrame Ci_hook_JIT_GetFrame = NULL;

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

Ci_HookType_PyShadowFrame_HasGen Ci_hook_PyShadowFrame_HasGen = NULL;
Ci_HookType_PyShadowFrame_GetGen Ci_hook_PyShadowFrame_GetGen = NULL;

Ci_HookType_PyJIT_GenVisitRefs Ci_hook_PyJIT_GenVisitRefs = NULL;
Ci_HookType_PyJIT_GenDealloc Ci_hook_PyJIT_GenDealloc = NULL;
Ci_HookType_PyJIT_GenSend Ci_hook_PyJIT_GenSend = NULL;
Ci_HookType_PyJIT_GenYieldFromValue Ci_hook_PyJIT_GenYieldFromValue = NULL;
Ci_HookType_PyJIT_GenMaterializeFrame  Ci_hook_PyJIT_GenMaterializeFrame = NULL;

Ci_HookType_MaybeStrictModule_Dict Ci_hook_MaybeStrictModule_Dict = NULL;
Ci_HookType_StrictModuleGetDict Ci_hook_StrictModuleGetDict = NULL;
Ci_HookType_StrictModule_Check Ci_hook_StrictModule_Check = NULL;

/* Interpreter */
_PyFrameEvalFunction Ci_hook_EvalFrame = NULL;
Ci_HookType_PyJIT_GetFrame Ci_hook_PyJIT_GetFrame = NULL;
Ci_HookType_PyJIT_GetBuiltins Ci_hook_PyJIT_GetBuiltins = NULL;
Ci_HookType_PyJIT_GetGlobals Ci_hook_PyJIT_GetGlobals = NULL;
Ci_HookType_PyJIT_GetCurrentCodeFlags Ci_hook_PyJIT_GetCurrentCodeFlags = NULL;
