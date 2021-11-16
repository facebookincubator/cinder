// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include "Jit/hir/type.h"
#include "Jit/log.h"
#include "Jit/ref.h"

#include <map>
#include <unordered_map>
#include <utility>

namespace jit {
namespace hir {

Type prim_type_to_type(int prim_type);

using PyTypeOpt = std::pair<Ref<PyTypeObject>, bool>;
using OffsetAndType = std::pair<Py_ssize_t, Type>;
using ArgToType = std::map<long, Type>;

// The target of an INVOKE_FUNCTION or INVOKE_METHOD
struct InvokeTarget {
  BorrowedRef<PyFunctionObject> func(void) const;

  // Vector-callable Python object
  Ref<> callable;
  // python-level return type (None for void/error-code builtins)
  Type return_type{TOptObject};
  // map argnum to primitive type code for primitive args only
  ArgToType primitive_arg_types;
  // container is immutable (target is not patchable)
  bool container_is_immutable;
  // patching indirection, nullptr if container_is_immutable
  PyObject** indirect_ptr;
  // vtable slot number (INVOKE_METHOD only)
  Py_ssize_t slot{-1};
  // is a CO_STATICALLY_COMPILED Python function or METH_TYPED builtin
  bool is_statically_typed;
  // is PyFunctionObject
  bool is_function;
  // is PyMethodDescrObject or PyCFunction (has a PyMethodDef)
  bool is_builtin;
  // needs the function object available at runtime (e.g. for freevars)
  bool uses_runtime_func;
  // underlying C function implementation for builtins
  void* builtin_c_func;
  // expected nargs for builtin; if matched, can x64 invoke even if untyped
  long builtin_expected_nargs{-1};
  // is a METH_TYPED builtin that returns void
  bool builtin_returns_void;
  // is a METH_TYPED builtin that returns integer error code
  bool builtin_returns_error_code;
};

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

  const InvokeTarget& invokeFunctionTarget(BorrowedRef<> descr) const;
  const InvokeTarget& invokeMethodTarget(BorrowedRef<> descr) const;

  // get the type from CHECK_ARGS for the given locals index, or TObject
  Type checkArgType(long local) const;

  Type returnType(void) const;

 private:
  // keyed by type descr tuple identity (they are interned in code objects)
  std::unordered_map<PyObject*, PyTypeOpt> types_;
  std::unordered_map<PyObject*, OffsetAndType> fields_;
  std::unordered_map<PyObject*, std::unique_ptr<InvokeTarget>> func_targets_;
  std::unordered_map<PyObject*, std::unique_ptr<InvokeTarget>> meth_targets_;
  // keyed by locals index
  std::unordered_map<long, Type> check_arg_types_;
  Type return_type_{TObject};
};

} // namespace hir
} // namespace jit
