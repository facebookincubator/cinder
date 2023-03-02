// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include "Jit/deopt_patcher.h"
#include "Jit/ref.h"

namespace jit {

// Patch a DeoptPatchpoint when the given PyTypeObject is modified (as
// determined by PyType_Modified())
class TypeDeoptPatcher : public DeoptPatcher {
 public:
  TypeDeoptPatcher(BorrowedRef<PyTypeObject> type);

 protected:
  void init() override;

 private:
  BorrowedRef<PyTypeObject> type_;
};

} // namespace jit
