// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/preload.h"

#include "Python.h"
#include "classloader.h"
#include "opcode.h"

#include "Jit/bytecode.h"
#include "Jit/codegen/gen_asm.h"
#include "Jit/hir/optimization.h"
#include "Jit/ref.h"
#include "Jit/util.h"

#include <utility>

namespace jit {
namespace hir {

Type prim_type_to_type(int prim_type) {
  switch (prim_type) {
    case TYPED_BOOL:
      return TCBool;
    case TYPED_CHAR:
    case TYPED_INT8:
      return TCInt8;
    case TYPED_INT16:
      return TCInt16;
    case TYPED_INT32:
      return TCInt32;
    case TYPED_INT64:
      return TCInt64;
    case TYPED_UINT8:
      return TCUInt8;
    case TYPED_UINT16:
      return TCUInt16;
    case TYPED_UINT32:
      return TCUInt32;
    case TYPED_UINT64:
      return TCUInt64;
    case TYPED_OBJECT:
      return TOptObject;
    case TYPED_DOUBLE:
      return TCDouble;
    case TYPED_ERROR:
      return TCInt32;
    default:
      JIT_CHECK(
          false, "non-primitive or unsupported Python type: %d", prim_type);
      break;
  }
}

static Type to_jit_type_impl(const PyTypeOpt& pytype_opt, bool exact) {
  auto& [pytype, opt] = pytype_opt;
  if (_PyClassLoader_IsEnum(pytype)) {
    JIT_CHECK(!opt, "static enums cannot be optional");
    return Type::fromEnum(pytype);
  }

  int prim_type = _PyClassLoader_GetTypeCode(pytype);
  if (prim_type == TYPED_OBJECT) {
    Type type = exact ? Type::fromTypeExact(pytype) : Type::fromType(pytype);
    if (opt) {
      type |= TNoneType;
    }
    return type;
  }
  JIT_CHECK(!opt, "primitive types cannot be optional");
  return prim_type_to_type(prim_type);
}

static Type to_jit_type(const PyTypeOpt& pytype_opt) {
  return to_jit_type_impl(pytype_opt, false);
}

static Type to_jit_type_exact(const PyTypeOpt& pytype_opt) {
  return to_jit_type_impl(pytype_opt, true);
}

static PyTypeOpt resolve_type_descr(BorrowedRef<> descr) {
  int optional;
  auto type =
      Ref<PyTypeObject>::steal(_PyClassLoader_ResolveType(descr, &optional));

  return {std::move(type), optional};
}

static OffsetAndType resolve_field_descr(BorrowedRef<> descr) {
  int field_type;
  Py_ssize_t offset = _PyClassLoader_ResolveFieldOffset(descr, &field_type);

  JIT_CHECK(offset != -1, "failed to resolve field %s", repr(descr));

  return {offset, prim_type_to_type(field_type)};
}

static void fill_primitive_arg_types_func(
    BorrowedRef<PyFunctionObject> func,
    ArgToType& map) {
  auto prim_args_info =
      Ref<_PyTypedArgsInfo>::steal(_PyClassLoader_GetTypedArgsInfo(
          reinterpret_cast<PyCodeObject*>(func->func_code), 1));

  for (Py_ssize_t i = 0; i < Py_SIZE(prim_args_info.get()); i++) {
    map.emplace(
        prim_args_info->tai_args[i].tai_argnum,
        prim_type_to_type(prim_args_info->tai_args[i].tai_primitive_type));
  }
}

static void fill_primitive_arg_types_builtin(
    BorrowedRef<> callable,
    ArgToType& map) {
  _PyTypedMethodDef* def = _PyClassLoader_GetTypedMethodDef(callable);
  JIT_CHECK(def != NULL, "expected typed method def");
  for (Py_ssize_t i = 0; def->tmd_sig[i] != NULL; i++) {
    const _Py_SigElement* elem = def->tmd_sig[i];
    int code = _Py_SIG_TYPE_MASK(elem->se_argtype);
    Type typ = prim_type_to_type(code);
    if (typ <= TPrimitive) {
      map.emplace(i, typ);
    }
  }
}

static std::unique_ptr<InvokeTarget> resolve_target_descr(
    BorrowedRef<> descr,
    int opcode) {
  auto target = std::make_unique<InvokeTarget>();
  PyObject* container;
  auto callable =
      Ref<>::steal(_PyClassLoader_ResolveFunction(descr, &container));
  JIT_CHECK(callable != nullptr, "unknown invoke target %s", repr(descr));

  int coroutine, optional, classmethod;
  auto return_pytype =
      Ref<PyTypeObject>::steal(_PyClassLoader_ResolveReturnType(
          callable, &optional, &coroutine, &classmethod));

  target->container_is_immutable = _PyClassLoader_IsImmutable(container);
  if (return_pytype != NULL) {
    if (coroutine) {
      // TODO properly handle coroutine returns awaitable type
      target->return_type = TOptObject;
    } else {
      target->return_type = to_jit_type({std::move(return_pytype), optional});
    }
  }
  target->is_statically_typed = _PyClassLoader_IsStaticCallable(callable);
  PyMethodDef* def;
  _PyTypedMethodDef* tmd;
  if (PyFunction_Check(callable)) {
    target->is_function = true;
  } else if ((def = _PyClassLoader_GetMethodDef(callable)) != nullptr) {
    target->is_builtin = true;
    target->builtin_c_func = reinterpret_cast<void*>(def->ml_meth);
    if (def->ml_flags == METH_NOARGS) {
      target->builtin_expected_nargs = 1;
    } else if (def->ml_flags == METH_O) {
      target->builtin_expected_nargs = 2;
    } else if ((tmd = _PyClassLoader_GetTypedMethodDef(callable))) {
      target->builtin_returns_error_code = (tmd->tmd_ret == _Py_SIG_ERROR);
      target->builtin_returns_void = (tmd->tmd_ret == _Py_SIG_VOID);
      target->builtin_c_func = tmd->tmd_meth;
    }
  }
  target->callable = std::move(callable);

  if (opcode == INVOKE_METHOD) {
    target->slot = _PyClassLoader_ResolveMethod(descr);
    JIT_CHECK(target->slot != -1, "method lookup failed: %s", repr(descr));
  } else { // the rest of this only used by INVOKE_FUNCTION currently
    target->uses_runtime_func =
        target->is_function && usesRuntimeFunc(target->func()->func_code);
    if (!target->container_is_immutable) {
      target->indirect_ptr =
          _PyClassLoader_GetIndirectPtr(descr, target->callable, container);
      JIT_CHECK(
          target->indirect_ptr != NULL, "%s indirect_ptr is null", repr(descr));
    }
    if (target->is_statically_typed) {
      if (target->is_function) {
        fill_primitive_arg_types_func(
            target->func(), target->primitive_arg_types);
      } else {
        fill_primitive_arg_types_builtin(
            target->callable, target->primitive_arg_types);
      }
    }
  }

  return target;
}

BorrowedRef<PyFunctionObject> InvokeTarget::func() const {
  JIT_CHECK(is_function, "not a PyFunctionObject");
  return reinterpret_cast<PyFunctionObject*>(callable.get());
}

Type Preloader::type(BorrowedRef<> descr) const {
  return to_jit_type(pyTypeOpt(descr));
}

Type Preloader::exactType(BorrowedRef<> descr) const {
  return to_jit_type_exact(pyTypeOpt(descr));
}

int Preloader::primitiveTypecode(BorrowedRef<> descr) const {
  return _PyClassLoader_GetTypeCode(pyType(descr));
}

BorrowedRef<PyTypeObject> Preloader::pyType(BorrowedRef<> descr) const {
  auto& [pytype, opt] = pyTypeOpt(descr);
  JIT_CHECK(!opt, "unexpected optional type");
  return pytype;
}

const PyTypeOpt& Preloader::pyTypeOpt(BorrowedRef<> descr) const {
  return map_get(types_, descr);
}

const OffsetAndType& Preloader::fieldOffsetAndType(BorrowedRef<> descr) const {
  return map_get(fields_, descr);
}

const InvokeTarget& Preloader::invokeFunctionTarget(BorrowedRef<> descr) const {
  return *(map_get(func_targets_, descr));
}

const InvokeTarget& Preloader::invokeMethodTarget(BorrowedRef<> descr) const {
  return *(map_get(meth_targets_, descr));
}

Type Preloader::checkArgType(long local_idx) const {
  return map_get(check_arg_types_, local_idx, TObject);
}

GlobalCache Preloader::getGlobalCache(BorrowedRef<> name) const {
  return jit::codegen::NativeGeneratorFactory::runtime()->findGlobalCache(
      globals_, name);
}

BorrowedRef<> Preloader::global(int name_idx) const {
  BorrowedRef<> name = map_get(global_names_, name_idx, nullptr);
  if (name != nullptr) {
    GlobalCache cache = getGlobalCache(name);
    return *(cache.valuePtr());
  }
  return nullptr;
}

std::unique_ptr<Function> Preloader::makeFunction() const {
  // We touch refcounts of Python objects here, so must serialize
  ThreadedCompileSerialize guard;
  auto irfunc = std::make_unique<Function>();
  irfunc->fullname = fullname_;
  irfunc->setCode(code_);
  irfunc->globals.reset(globals_);
  irfunc->builtins.reset(builtins_);
  irfunc->prim_args_info.reset(prim_args_info_);
  irfunc->return_type = return_type_;
  irfunc->has_primitive_args = has_primitive_args_;
  irfunc->has_primitive_first_arg = has_primitive_first_arg_;
  for (auto& [local, pytype_opt] : check_arg_pytypes_) {
    irfunc->typed_args.emplace_back(local, pytype_opt.first, pytype_opt.second);
  }
  return irfunc;
}

BorrowedRef<> Preloader::constArg(BytecodeInstruction& bc_instr) const {
  return PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
}

void Preloader::preload() {
  if (code_->co_flags & CO_STATICALLY_COMPILED) {
    return_type_ = to_jit_type(
        resolve_type_descr(_PyClassLoader_GetCodeReturnTypeDescr(code_)));
  }

  jit::BytecodeInstructionBlock bc_instrs{code_};
  for (auto bc_instr : bc_instrs) {
    switch (bc_instr.opcode()) {
      case LOAD_GLOBAL: {
        if (_PyDict_CanWatch(builtins_) && _PyDict_CanWatch(globals_)) {
          int name_idx = bc_instr.oparg();
          BorrowedRef<> name = PyTuple_GET_ITEM(code_->co_names, name_idx);
          // We can't keep hold of a reference to this cache, it could get
          // invalidated and freed; we just do this here for the side effect to
          // make sure the cached value has been loaded and any side effects of
          // loading it have been exercised.
          getGlobalCache(name);
          global_names_.emplace(name_idx, name);
        }
        break;
      }
      case CHECK_ARGS: {
        BorrowedRef<PyTupleObject> checks =
            reinterpret_cast<PyTupleObject*>(constArg(bc_instr).get());
        for (int i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
          long local = PyLong_AsLong(PyTuple_GET_ITEM(checks, i));
          if (local < 0) {
            // A negative value for local indicates that it's a cell
            JIT_CHECK(
                code_->co_cell2arg != nullptr,
                "no cell2arg but negative local %ld",
                local);
            long arg = code_->co_cell2arg[-1 * (local + 1)];
            JIT_CHECK(
                arg != CO_CELL_NOT_AN_ARG,
                "cell not an arg for local %ld",
                local);
            local = arg;
          }
          PyTypeOpt pytype_opt =
              resolve_type_descr(PyTuple_GET_ITEM(checks, i + 1));
          JIT_CHECK(
              pytype_opt.first !=
                  reinterpret_cast<PyTypeObject*>(&PyObject_Type),
              "shouldn't generate type checks for object");
          Type type = to_jit_type(pytype_opt);
          check_arg_types_.emplace(local, type);
          check_arg_pytypes_.emplace(local, std::move(pytype_opt));
          if (type <= TPrimitive) {
            has_primitive_args_ = true;
            if (local == 0) {
              has_primitive_first_arg_ = true;
            }
          }
        }
        break;
      }
      case BUILD_CHECKED_LIST:
      case BUILD_CHECKED_MAP: {
        BorrowedRef<> descr = PyTuple_GetItem(constArg(bc_instr), 0);
        types_.emplace(descr, resolve_type_descr(descr));
        break;
      }
      case CAST:
      case PRIMITIVE_BOX:
      case PRIMITIVE_UNBOX:
      case REFINE_TYPE:
      case TP_ALLOC: {
        BorrowedRef<> descr = constArg(bc_instr);
        types_.emplace(descr, resolve_type_descr(descr));
        break;
      }
      case LOAD_FIELD:
      case STORE_FIELD: {
        BorrowedRef<> descr = constArg(bc_instr);
        fields_.emplace(descr, resolve_field_descr(descr));
        break;
      }
      case INVOKE_FUNCTION:
      case INVOKE_METHOD: {
        BorrowedRef<PyObject> descr = PyTuple_GetItem(constArg(bc_instr), 0);
        auto& map = bc_instr.opcode() == INVOKE_FUNCTION ? func_targets_
                                                         : meth_targets_;
        map.emplace(descr, resolve_target_descr(descr, bc_instr.opcode()));
        break;
      }
    }
  }

  if (has_primitive_args_) {
    prim_args_info_ = Ref<_PyTypedArgsInfo>::steal(
        _PyClassLoader_GetTypedArgsInfo(code_, true));
  }
}

} // namespace hir
} // namespace jit
