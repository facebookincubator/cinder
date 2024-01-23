// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "cinderx/StaticPython/classloader.h"

#include "cinderx/Jit/global_cache.h"
#include "cinderx/Jit/hir/hir.h"
#include "cinderx/Jit/hir/type.h"
#include "cinderx/Jit/log.h"
#include "cinderx/Jit/ref.h"

#include <map>
#include <unordered_map>
#include <utility>

namespace jit::hir {

Type prim_type_to_type(int prim_type);

using PyTypeOpt = std::tuple<Ref<PyTypeObject>, bool, bool>;
using ArgToType = std::map<long, Type>;

struct FieldInfo {
  Py_ssize_t offset;
  Type type;
  BorrowedRef<PyUnicodeObject> name;
};

// The target of an INVOKE_FUNCTION or INVOKE_METHOD
struct InvokeTarget {
  BorrowedRef<PyFunctionObject> func() const;

  // Vector-callable Python object
  Ref<> callable;
  // python-level return type (None for void/error-code builtins)
  Type return_type{TObject};
  // map argnum to primitive type code for primitive args only
  ArgToType primitive_arg_types;
  // container is immutable (target is not patchable)
  bool container_is_immutable{false};
  // patching indirection, nullptr if container_is_immutable
  PyObject** indirect_ptr{nullptr};
  // vtable slot number (INVOKE_METHOD only)
  Py_ssize_t slot{-1};
  // is a CO_STATICALLY_COMPILED Python function or METH_TYPED builtin
  bool is_statically_typed{false};
  // is PyFunctionObject
  bool is_function{false};
  // is PyMethodDescrObject or PyCFunction (has a PyMethodDef)
  bool is_builtin{false};
  // needs the function object available at runtime (e.g. for freevars)
  bool uses_runtime_func{false};
  // underlying C function implementation for builtins
  void* builtin_c_func{nullptr};
  // expected nargs for builtin; if matched, can x64 invoke even if untyped
  long builtin_expected_nargs{-1};
  // is a METH_TYPED builtin that returns void
  bool builtin_returns_void{false};
  // is a METH_TYPED builtin that returns integer error code
  bool builtin_returns_error_code{false};
};

// The target of an INVOKE_NATIVE
struct NativeTarget {
  // the address of target
  void* callable;
  // return type (must be a primitive int for native calls)
  Type return_type{TObject};
  // map argnum to primitive type code for primitive args only
  ArgToType primitive_arg_types;
};

// Preloads all globals and classloader type descrs referenced by a code object.
// We need to do this in advance because it can resolve lazy imports (or
// generally just trigger imports) which is Python code execution, which we
// can't allow mid-compile.
class Preloader {
 public:
  Preloader(Preloader&&) = default;
  Preloader() = default;
  static std::unique_ptr<Preloader> makePreloader(
      BorrowedRef<PyFunctionObject> func) {
    auto preloader = std::unique_ptr<Preloader>(new Preloader(
        func->func_code,
        func->func_builtins,
        func->func_globals,
        funcFullname(func)));
    if (!preloader->preload()) {
      JIT_DCHECK(PyErr_Occurred(), "Expected Python exception to be set");
      return nullptr;
    }
    return preloader;
  }

  static std::unique_ptr<Preloader> makePreloader(
      BorrowedRef<PyCodeObject> code,
      BorrowedRef<PyDictObject> builtins,
      BorrowedRef<PyDictObject> globals,
      const std::string& fullname) {
    auto preloader = std::unique_ptr<Preloader>(
        new Preloader(code, builtins, globals, fullname));
    if (!preloader->preload()) {
      JIT_DCHECK(PyErr_Occurred(), "Expected Python exception to be set");
      return nullptr;
    }
    return preloader;
  }

  Type type(BorrowedRef<> descr) const;
  int primitiveTypecode(BorrowedRef<> descr) const;
  BorrowedRef<PyTypeObject> pyType(BorrowedRef<> descr) const;
  const PyTypeOpt& pyTypeOpt(BorrowedRef<> descr) const;

  const FieldInfo& fieldInfo(BorrowedRef<> descr) const;

  const InvokeTarget& invokeFunctionTarget(BorrowedRef<> descr) const;
  const InvokeTarget& invokeMethodTarget(BorrowedRef<> descr) const;
  const NativeTarget& invokeNativeTarget(BorrowedRef<> target) const;

  // get the type from argument check info for the given locals index, or
  // TObject
  Type checkArgType(long local_idx) const;

  // get value for global at given name index
  BorrowedRef<> global(int name_idx) const;

  std::unique_ptr<Function> makeFunction() const;

  BorrowedRef<PyCodeObject> code() const {
    return code_;
  }

  BorrowedRef<PyDictObject> globals() const {
    return globals_;
  }

  BorrowedRef<PyDictObject> builtins() const {
    return builtins_;
  }

  const std::string& fullname() const {
    return fullname_;
  }

  Type returnType() const {
    return return_type_;
  }

  int numArgs() const {
    if (code_ == nullptr) {
      // code_ might be null if we parsed from textual ir
      return 0;
    }
    return code_->co_argcount + code_->co_kwonlyargcount +
        bool(code_->co_flags & CO_VARARGS) +
        bool(code_->co_flags & CO_VARKEYWORDS);
  }

  std::unique_ptr<InvokeTarget> resolve_target_descr(
      BorrowedRef<> descr,
      int opcode);

 private:
  BorrowedRef<> constArg(BytecodeInstruction& bc_instr) const;
  GlobalCache getGlobalCache(BorrowedRef<> name) const;
  bool canCacheGlobals() const;
  bool preload();

  explicit Preloader(
      BorrowedRef<PyCodeObject> code,
      BorrowedRef<PyDictObject> builtins,
      BorrowedRef<PyDictObject> globals,
      const std::string& fullname)
      : code_(Ref<>::create(code)),
        builtins_(Ref<>::create(builtins)),
        globals_(Ref<>::create(globals)),
        fullname_(fullname) {
    JIT_CHECK(PyCode_Check(code_), "Expected PyCodeObject");
  };

  Ref<PyCodeObject> code_;
  Ref<PyDictObject> builtins_;
  Ref<PyDictObject> globals_;
  const std::string fullname_;

  // keyed by type descr tuple identity (they are interned in code objects)
  std::unordered_map<PyObject*, PyTypeOpt> types_;
  std::unordered_map<PyObject*, FieldInfo> fields_;
  std::unordered_map<PyObject*, std::unique_ptr<InvokeTarget>> func_targets_;
  std::unordered_map<PyObject*, std::unique_ptr<InvokeTarget>> meth_targets_;
  std::unordered_map<PyObject*, std::unique_ptr<NativeTarget>> native_targets_;
  // keyed by locals index
  std::unordered_map<long, Type> check_arg_types_;
  std::map<long, PyTypeOpt> check_arg_pytypes_;
  // keyed by name index, names borrowed from code object
  std::unordered_map<int, BorrowedRef<>> global_names_;
  Type return_type_{TObject};
  bool has_primitive_args_{false};
  bool has_primitive_first_arg_{false};
  // for primitive args only, null unless has_primitive_args_
  Ref<_PyTypedArgsInfo> prim_args_info_;
};

} // namespace jit::hir
