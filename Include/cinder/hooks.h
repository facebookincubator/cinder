// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "cinder/ci_api.h"
#include "pystate.h" // PyThreadState

/* Hooks needed by CinderX that have not been added to upstream. */

CiAPI_DATA(_PyFrameEvalFunction) Ci_hook_EvalFrame;

#ifdef __cplusplus
} // extern "C"
#endif
