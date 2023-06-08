// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "Jit/type_deopt_patchers.h"

#include "structmember.h"

#include "Jit/runtime.h"
#include "Jit/util.h"

namespace jit {

TypeDeoptPatcher::TypeDeoptPatcher(BorrowedRef<PyTypeObject> type)
    : type_{type} {}

bool TypeDeoptPatcher::maybePatch(BorrowedRef<PyTypeObject>) {
  patch();
  return true;
}

void TypeDeoptPatcher::init() {
  Runtime::get()->watchType(type_, this);
}

namespace {
template <typename Body>
bool shouldPatchForAttr(
    BorrowedRef<PyTypeObject> old_ty,
    BorrowedRef<PyTypeObject> new_ty,
    BorrowedRef<PyUnicodeObject> attr_name,
    Body body) {
  if (new_ty != old_ty) {
    // new_ty is not the same as old_ty (it's either nullptr or a new type). If
    // new_ty has the same attribute with the same properties, we could watch
    // it as well and leave the specialized code in place, but that would
    // increase complexity and memory usage for what should be a vanishingly
    // rare situation.
    return true;
  }

  // Similarly to the JIT code using this patcher, we want to avoid triggering
  // user-visible side-effects, so we do the lookup using typeLookupSafe(). If
  // that succeeds and returns an object that still satisfies our requirements,
  // we attempt to give the type a new version tag before declaring success.
  BorrowedRef<> attr{typeLookupSafe(new_ty, attr_name)};

  if (body(attr)) {
    return true;
  }

  return !Ci_Type_AssignVersionTag(new_ty);
}
} // namespace

TypeAttrDeoptPatcher::TypeAttrDeoptPatcher(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<PyUnicodeObject> attr_name,
    BorrowedRef<> target_object)
    : TypeDeoptPatcher{type} {
  ThreadedCompileSerialize guard;
  attr_name_.reset(attr_name);
  target_object_.reset(target_object);
}

bool TypeAttrDeoptPatcher::maybePatch(BorrowedRef<PyTypeObject> new_ty) {
  bool should_patch =
      shouldPatchForAttr(type_, new_ty, attr_name_, [&](BorrowedRef<> attr) {
        return attr != target_object_;
      });
  if (should_patch) {
    patch();
    attr_name_.reset();
    target_object_.reset();
  }
  return should_patch;
}

SplitDictDeoptPatcher::SplitDictDeoptPatcher(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<PyUnicodeObject> attr_name,
    PyDictKeysObject* keys)
    : TypeDeoptPatcher{type}, keys_{keys} {
  ThreadedCompileSerialize guard;
  attr_name_.reset(attr_name);
}

bool SplitDictDeoptPatcher::maybePatch(BorrowedRef<PyTypeObject> new_ty) {
  bool should_patch =
      shouldPatchForAttr(type_, new_ty, attr_name_, [&](BorrowedRef<> attr) {
        if (attr != nullptr) {
          // This is more conservative than strictly necessary: the split dict
          // lookup would still be OK if attr isn't a data descriptor, but we'd
          // have to watch attr's type to safely rely on that fact.
          return true;
        }

        if (!PyType_HasFeature(new_ty, Py_TPFLAGS_HEAPTYPE)) {
          return true;
        }

        BorrowedRef<PyHeapTypeObject> ht(new_ty);
        return ht->ht_cached_keys != keys_;
      });
  if (should_patch) {
    patch();
    attr_name_.reset();
  }
  return should_patch;
}

} // namespace jit
