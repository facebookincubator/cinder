// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "Jit/type_deopt_patcher.h"

#include "Jit/runtime.h"

namespace jit {

TypeDeoptPatcher::TypeDeoptPatcher(BorrowedRef<PyTypeObject> type)
    : type_{type} {}

void TypeDeoptPatcher::init() {
  Runtime::get()->watchType(type_, this);
}

} // namespace jit
