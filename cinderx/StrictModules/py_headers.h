// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "Python.h"
#include "cinderx/StrictModules/pycore_dependencies.h"

// remove conflicting macros from python-ast.h
#ifdef Compare
#undef Compare
#endif
#ifdef Set
#undef Set
#endif
#ifdef arg
#undef arg
#endif
#ifdef FunctionType
#undef FunctionType
#endif
