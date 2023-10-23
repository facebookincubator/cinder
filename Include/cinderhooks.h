// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

/* Hooks needed by CinderX that have not been added to upstream. */

/* Hooks for JIT type profiling. */

typedef void(*Ci_TypeCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_created;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_destroyed;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_name_modified;

/* Hooks for Static Python. */

typedef int(*Ci_TypeRaisingCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeRaisingCallback) Ci_hook_type_pre_setattr;

typedef int(*Ci_TypeAttrRaisingCallback)(PyTypeObject *type, PyObject *name, PyObject *value);
CiAPI_DATA(Ci_TypeAttrRaisingCallback) Ci_hook_type_setattr;
