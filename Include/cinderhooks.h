// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

/* Hooks needed by CinderX that have not been added to upstream. */

typedef void(*Ci_TypeCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_created;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_destroyed;
