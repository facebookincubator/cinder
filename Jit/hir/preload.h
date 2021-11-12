// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include "Jit/hir/type.h"
#include "Jit/log.h"
#include "Jit/ref.h"

#include <unordered_map>
#include <utility>

namespace jit {
namespace hir {

using PyTypeOpt = std::pair<Ref<PyTypeObject>, bool>;
using OffsetAndType = std::pair<Py_ssize_t, Type>;

// Preloads all classloader type descrs referenced by a code object. We need to
// do this in advance because it can resolve lazy imports (or generally just
// trigger imports) which is Python code execution, which we can't allow
// mid-compile.
class Preloader {
 public:
  void preload(BorrowedRef<PyCodeObject> code);

  Type type(BorrowedRef<> descr) const;
  Type exactType(BorrowedRef<> descr) const;
  int primitiveTypecode(BorrowedRef<> descr) const;
  BorrowedRef<PyTypeObject> pyType(BorrowedRef<> descr) const;
  const PyTypeOpt& pyTypeOpt(BorrowedRef<> descr) const;

  const OffsetAndType& fieldOffsetAndType(BorrowedRef<> descr) const;

  // get the type from CHECK_ARGS for the given locals index, or TObject
  Type checkArgType(long local) const;

  Type returnType(void) const;

 private:
  // keyed by type descr tuple identity (they are interned in code objects)
  std::unordered_map<PyObject*, PyTypeOpt> types_;
  std::unordered_map<PyObject*, OffsetAndType> fields_;
  // keyed by locals index
  std::unordered_map<long, Type> check_arg_types_;
  Type return_type_{TObject};
};

} // namespace hir
} // namespace jit
