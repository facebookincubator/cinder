// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Struct used inside type_new_common to pass the default slot functions for
 * a type to the JIT. These will be used to determine if the JIT can be used,
 * and for deoptimization support.
 */
typedef struct {
  ternaryfunc tp_call;
  reprfunc tp_repr;
  reprfunc tp_str;
  getattrofunc tp_getattro;
  descrgetfunc tp_descr_get;
} _PyJIT_TypeSlots;

#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
