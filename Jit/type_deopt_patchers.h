// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include "Jit/deopt_patcher.h"
#include "Jit/ref.h"
#include "Jit/runtime.h"

#include <variant>

namespace jit {

class TypeDeoptPatcher : public DeoptPatcher {
 public:
  TypeDeoptPatcher(BorrowedRef<PyTypeObject> type);

  virtual bool maybePatch(BorrowedRef<PyTypeObject> new_ty) = 0;

 protected:
  void init() override;

  BorrowedRef<PyTypeObject> type_;
};

// Patch a DeoptPatchpoint when the given PyTypeObject no longer has the given
// PyObject* at the specified name.
class TypeAttrDeoptPatcher : public TypeDeoptPatcher {
 public:
  TypeAttrDeoptPatcher(
      BorrowedRef<PyTypeObject> type,
      BorrowedRef<PyUnicodeObject> attr_name,
      BorrowedRef<> target_object);

  bool maybePatch(BorrowedRef<PyTypeObject> new_ty) override;

 private:
  Ref<PyUnicodeObject> attr_name_;
  Ref<> target_object_;
};

class SplitDictDeoptPatcher : public TypeDeoptPatcher {
 public:
  SplitDictDeoptPatcher(
      BorrowedRef<PyTypeObject> type,
      BorrowedRef<PyUnicodeObject> attr_name,
      PyDictKeysObject* keys);

  bool maybePatch(BorrowedRef<PyTypeObject> new_ty) override;

 private:
  Ref<PyUnicodeObject> attr_name_;

  // We don't need to hold a strong reference to keys_ like we do for
  // attr_name_ because calls to PyTypeModified() happen before the old keys
  // object is decrefed.
  PyDictKeysObject* keys_;
};

} // namespace jit
