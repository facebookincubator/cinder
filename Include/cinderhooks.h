// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "pystate.h" // PyThreadState

/* Hooks needed by CinderX that have not been added to upstream. */

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

/* Hooks for Static Python. */
typedef int(*Ci_TypeRaisingCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeRaisingCallback) Ci_hook_type_pre_setattr;

typedef int(*Ci_TypeAttrRaisingCallback)(PyTypeObject *type, PyObject *name, PyObject *value);
CiAPI_DATA(Ci_TypeAttrRaisingCallback) Ci_hook_type_setattr;

/* Wrappers to expose private functions for usage with hooks. */

typedef void (*Cix_funcptr)(void);
CiAPI_FUNC(Cix_funcptr)
    Cix_method_enter_call(PyThreadState *tstate, PyObject *func);
