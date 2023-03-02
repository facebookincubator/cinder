// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "Jit/type_deopt_patchers.h"

#include "structmember.h"

#include "Jit/runtime.h"
#include "Jit/util.h"

namespace jit {

TypeDeoptPatcher::TypeDeoptPatcher(BorrowedRef<PyTypeObject> type)
    : type_{type} {}

void TypeDeoptPatcher::init() {
  Runtime::get()->watchType(type_, this);
}

MemberDescrDeoptPatcher::MemberDescrDeoptPatcher(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<PyUnicodeObject> member_name,
    int member_type,
    Py_ssize_t member_offset)
    : TypeDeoptPatcher{type},
      member_name_{member_name},
      member_type_{member_type},
      member_offset_{member_offset} {}

bool MemberDescrDeoptPatcher::shouldPatch(
    BorrowedRef<PyTypeObject> new_ty) const {
  if (new_ty != type_) {
    // new_ty is not the same as type_ (it's either nullptr or a new type). If
    // new_ty has the same attribute at the same offset, we could watch it as
    // well and leave the specialized code in place, but that would increase
    // complexity and memory usage for what should be a vanishingly rare
    // situation.
    return true;
  }

  if (!PyType_HasFeature(new_ty, Py_TPFLAGS_HAVE_VERSION_TAG)) {
    // new_ty has a weird MRO that doesn't allow a version tag, so
    // PyType_Modified() will no longer fire and we have to patch.
    return true;
  }

  // Similarly to the JIT code using this patcher, we want to avoid triggering
  // user-visible side-effects, so we do the lookup using typeLookupSafe(). If
  // that succeeds and returns a PyMemberDescr that still satisfies our
  // requirements, we attempt to give the type a new version tag before
  // declaring success.
  BorrowedRef<> descr{typeLookupSafe(new_ty, member_name_)};
  if (descr == nullptr || Py_TYPE(descr) != &PyMemberDescr_Type) {
    return true;
  }
  PyMemberDef* def =
      reinterpret_cast<PyMemberDescrObject*>(descr.get())->d_member;
  if ((def->flags & READ_RESTRICTED) || def->type != member_type_ ||
      def->offset != member_offset_) {
    return true;
  }

  return !Ci_Type_AssignVersionTag(new_ty);
}

void MemberDescrDeoptPatcher::addReferences(CodeRuntime* code_rt) {
  code_rt->addReference(member_name_);
}

} // namespace jit
