// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/lir/function.h"

namespace jit::lir {

void eliminateDeadCode(Function* func);

} // namespace jit::lir
