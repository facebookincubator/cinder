// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinderhooks.h"

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
