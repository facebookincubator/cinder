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

static Type to_jit_type(const PyTypeOpt& pytype_opt) {
  auto& [pytype, opt, exact] = pytype_opt;
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

static PyTypeOpt resolve_type_descr(BorrowedRef<> descr) {
  int optional, exact;
  auto type = Ref<PyTypeObject>::steal(
      _PyClassLoader_ResolveType(descr, &optional, &exact));

  return {std::move(type), optional, exact};
}

static FieldInfo resolve_field_descr(BorrowedRef<PyTupleObject> descr) {
  int field_type;
  Py_ssize_t offset = _PyClassLoader_ResolveFieldOffset(descr, &field_type);

  JIT_CHECK(offset != -1, "failed to resolve field %s", repr(descr));

  return {
      offset,
      prim_type_to_type(field_type),
      PyTuple_GET_ITEM(descr, PyTuple_GET_SIZE(descr) - 1)};
}

static void _fill_primitive_arg_types_helper(
    BorrowedRef<_PyTypedArgsInfo> prim_args_info,
    ArgToType& map) {
  for (Py_ssize_t i = 0; i < Py_SIZE(prim_args_info.get()); i++) {
    BorrowedRef<PyTypeObject> type = prim_args_info->tai_args[i].tai_type;
    map.emplace(
        prim_args_info->tai_args[i].tai_argnum,
        _PyClassLoader_IsEnum(type)
            ? Type::fromEnum(type)
            : prim_type_to_type(
                  prim_args_info->tai_args[i].tai_primitive_type));
  }
}

static void fill_primitive_arg_types_func(
    BorrowedRef<PyFunctionObject> func,
    ArgToType& map) {
  auto prim_args_info =
      Ref<_PyTypedArgsInfo>::steal(_PyClassLoader_GetTypedArgsInfo(
          reinterpret_cast<PyCodeObject*>(func->func_code), 1));
  _fill_primitive_arg_types_helper(prim_args_info, map);
}

static void fill_primitive_arg_types_thunk(
    BorrowedRef<PyObject> thunk,
    ArgToType& map,
    PyObject* container) {
  auto prim_args_info = Ref<_PyTypedArgsInfo>::steal(
      _PyClassLoader_GetTypedArgsInfoFromThunk(thunk, container, 1));

  _fill_primitive_arg_types_helper(prim_args_info, map);
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
  if (callable == nullptr) {
    JIT_LOG("unknown invoke target %s during preloading", repr(descr));
    return nullptr;
  }

  int coroutine, optional, exact, classmethod;
  auto return_pytype =
      Ref<PyTypeObject>::steal(_PyClassLoader_ResolveReturnType(
          callable, &optional, &exact, &coroutine, &classmethod));

  target->container_is_immutable = _PyClassLoader_IsImmutable(container);
  if (return_pytype != NULL) {
    if (coroutine) {
      // TODO properly handle coroutine returns awaitable type
      target->return_type = TObject;
    } else {
      target->return_type =
          to_jit_type({std::move(return_pytype), optional, exact});
    }
  }
  target->is_statically_typed = _PyClassLoader_IsStaticCallable(callable);
  PyMethodDef* def;
  _PyTypedMethodDef* tmd;
  bool is_thunk = false;
  if (PyFunction_Check(callable)) {
    target->is_function = true;
  } else if (_PyClassLoader_IsPatchedThunk(callable)) {
    is_thunk = true;
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

  if (is_thunk) {
    fill_primitive_arg_types_thunk(
        target->callable.get(), target->primitive_arg_types, container);
  }

  return target;
}

static std::unique_ptr<NativeTarget> resolve_native_target(
    BorrowedRef<> native_descr,
    BorrowedRef<> signature) {
  auto target = std::make_unique<NativeTarget>();
  void* raw_ptr = _PyClassloader_LookupSymbol(
      PyTuple_GET_ITEM(native_descr.get(), 0),
      PyTuple_GET_ITEM(native_descr.get(), 1));

  JIT_CHECK(
      raw_ptr != nullptr, "invalid address for native function: %p", raw_ptr);

  target->callable = raw_ptr;

  Py_ssize_t siglen = PyTuple_GET_SIZE(signature.get());
  auto return_type_code = _PyClassLoader_ResolvePrimitiveType(
      PyTuple_GET_ITEM(signature.get(), siglen - 1));
  target->return_type = prim_type_to_type(return_type_code);
  JIT_DCHECK(
      target->return_type <= TCInt,
      "native function return type must be a primitive");

  // Fill in the primitive arg type map in the target (index -> Type)
  ArgToType& primitive_arg_types = target->primitive_arg_types;
  for (Py_ssize_t i = 0; i < siglen - 1; i++) {
    int arg_type_code = _PyClassLoader_ResolvePrimitiveType(
        PyTuple_GET_ITEM(signature.get(), i));
    Type typ = prim_type_to_type(arg_type_code);
    JIT_DCHECK(typ <= TCInt, "native function arg type must be a primitive");

    primitive_arg_types.emplace(i, typ);
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

int Preloader::primitiveTypecode(BorrowedRef<> descr) const {
  return _PyClassLoader_GetTypeCode(pyType(descr));
}

BorrowedRef<PyTypeObject> Preloader::pyType(BorrowedRef<> descr) const {
  auto& [pytype, opt, exact] = pyTypeOpt(descr);
  JIT_CHECK(!opt, "unexpected optional type");
  return pytype;
}

const PyTypeOpt& Preloader::pyTypeOpt(BorrowedRef<> descr) const {
  return map_get(types_, descr);
}

const FieldInfo& Preloader::fieldInfo(BorrowedRef<> descr) const {
  return map_get(fields_, descr);
}

const InvokeTarget& Preloader::invokeFunctionTarget(BorrowedRef<> descr) const {
  return *(map_get(func_targets_, descr));
}

const InvokeTarget& Preloader::invokeMethodTarget(BorrowedRef<> descr) const {
  return *(map_get(meth_targets_, descr));
}

const NativeTarget& Preloader::invokeNativeTarget(BorrowedRef<> target) const {
  return *(map_get(native_targets_, target));
}

Type Preloader::checkArgType(long local_idx) const {
  return map_get(check_arg_types_, local_idx, TObject);
}

GlobalCache Preloader::getGlobalCache(BorrowedRef<> name) const {
  JIT_DCHECK(
      canCacheGlobals(),
      "trying to get a globals cache with unwatchable builtins and/or globals");
  return jit::Runtime::get()->findGlobalCache(globals_, name);
}

bool Preloader::canCacheGlobals() const {
  return _PyDict_CanWatch(builtins_) && _PyDict_CanWatch(globals_);
}

BorrowedRef<> Preloader::global(int name_idx) const {
  BorrowedRef<> name = map_get(global_names_, name_idx, nullptr);
  if (name != nullptr && canCacheGlobals()) {
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
  irfunc->prim_args_info.reset(prim_args_info_);
  irfunc->return_type = return_type_;
  irfunc->has_primitive_args = has_primitive_args_;
  irfunc->has_primitive_first_arg = has_primitive_first_arg_;
  for (auto& [local, pytype_opt] : check_arg_pytypes_) {
    irfunc->typed_args.emplace_back(
        local,
        std::get<0>(pytype_opt),
        std::get<1>(pytype_opt),
        std::get<2>(pytype_opt),
        to_jit_type(pytype_opt));
  }
  return irfunc;
}

BorrowedRef<> Preloader::constArg(BytecodeInstruction& bc_instr) const {
  return PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
}

bool Preloader::preload() {
  if (code_->co_flags & CO_STATICALLY_COMPILED) {
    return_type_ = to_jit_type(
        resolve_type_descr(_PyClassLoader_GetCodeReturnTypeDescr(code_)));
  }

  jit::BytecodeInstructionBlock bc_instrs{code_};
  for (auto bc_instr : bc_instrs) {
    switch (bc_instr.opcode()) {
      case LOAD_GLOBAL: {
        if (canCacheGlobals()) {
          int name_idx = bc_instr.oparg();
          BorrowedRef<> name = PyTuple_GET_ITEM(code_->co_names, name_idx);
          // We can't keep hold of a reference to this cache, it could get
          // invalidated and freed; we just do this here for the side effect to
          // make sure the cached value has been loaded and any side effects of
          // loading it have been exercised.
          JIT_CHECK(name != nullptr, "name cannot be null");
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
              std::get<0>(pytype_opt) !=
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
      case LOAD_CLASS:
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
        BorrowedRef<PyTupleObject> descr(constArg(bc_instr));
        fields_.emplace(descr, resolve_field_descr(descr));
        break;
      }
      case INVOKE_FUNCTION:
      case INVOKE_METHOD: {
        BorrowedRef<PyObject> descr = PyTuple_GetItem(constArg(bc_instr), 0);
        auto& map = bc_instr.opcode() == INVOKE_FUNCTION ? func_targets_
                                                         : meth_targets_;
        auto target = resolve_target_descr(descr, bc_instr.opcode());
        if (target) {
          map.emplace(descr, resolve_target_descr(descr, bc_instr.opcode()));
          break;
        } else {
          return false;
        }
      }
      case INVOKE_NATIVE: {
        BorrowedRef<> target_descr = PyTuple_GetItem(constArg(bc_instr), 0);
        BorrowedRef<> signature = PyTuple_GetItem(constArg(bc_instr), 1);
        native_targets_.emplace(
            target_descr, resolve_native_target(target_descr, signature));
        break;
      }
    }
  }

  if (has_primitive_args_) {
    prim_args_info_ = Ref<_PyTypedArgsInfo>::steal(
        _PyClassLoader_GetTypedArgsInfo(code_, true));
  }
  return true;
}

} // namespace hir
} // namespace jit
