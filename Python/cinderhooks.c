// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinderhooks.h"

Ci_TypeCallback Ci_hook_type_created = NULL;
Ci_TypeCallback Ci_hook_type_destroyed = NULL;
