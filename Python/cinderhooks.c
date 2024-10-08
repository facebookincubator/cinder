// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Python.h"
#include "cinder/hooks.h"

/* Interpreter */
_PyFrameEvalFunction Ci_hook_EvalFrame = NULL;
