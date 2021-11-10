// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once
#include "Python.h"

#include "ast.h"
#include "symtable.h"

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
