// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/preload.h"

#include "Python.h"
#include "classloader.h"
#include "opcode.h"

#include "Jit/bytecode.h"
#include "Jit/ref.h"

#include <utility>

namespace jit {
namespace hir {

static Type prim_type_to_type(int prim_type) {
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
  return types_.at(descr);
}

const OffsetAndType& Preloader::fieldOffsetAndType(BorrowedRef<> descr) const {
  return fields_.at(descr);
}

Type Preloader::checkArgType(long local) const {
  auto it = check_arg_types_.find(local);
  if (it == check_arg_types_.end()) {
    return TObject;
  } else {
    return it->second;
  }
}

Type Preloader::returnType() const {
  return return_type_;
}

void Preloader::preload(BorrowedRef<PyCodeObject> code) {
  // if not statically compiled, there won't be any type descrs
  if (!(code->co_flags & CO_STATICALLY_COMPILED)) {
    return;
  }

  return_type_ = to_jit_type(
      resolve_type_descr(_PyClassLoader_GetCodeReturnTypeDescr(code)));

  jit::BytecodeInstructionBlock bc_instrs{code};
  for (auto bc_instr : bc_instrs) {
    switch (bc_instr.opcode()) {
      case CHECK_ARGS: {
        BorrowedRef<PyTupleObject> checks = reinterpret_cast<PyTupleObject*>(
            PyTuple_GET_ITEM(code->co_consts, bc_instr.oparg()));
        for (int i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
          long local = PyLong_AsLong(PyTuple_GET_ITEM(checks, i));
          check_arg_types_.emplace(
              local,
              to_jit_type(resolve_type_descr(PyTuple_GET_ITEM(checks, i + 1))));
        }
        break;
      }
      case BUILD_CHECKED_LIST:
      case BUILD_CHECKED_MAP: {
        BorrowedRef<> descr = PyTuple_GET_ITEM(
            PyTuple_GET_ITEM(code->co_consts, bc_instr.oparg()), 0);
        types_.emplace(descr, resolve_type_descr(descr));
        break;
      }
      case CAST:
      case PRIMITIVE_BOX:
      case PRIMITIVE_UNBOX:
      case REFINE_TYPE:
      case TP_ALLOC: {
        BorrowedRef<> descr =
            PyTuple_GET_ITEM(code->co_consts, bc_instr.oparg());
        types_.emplace(descr, resolve_type_descr(descr));
        break;
      }
      case LOAD_FIELD:
      case STORE_FIELD: {
        BorrowedRef<> descr =
            PyTuple_GET_ITEM(code->co_consts, bc_instr.oparg());
        fields_.emplace(descr, resolve_field_descr(descr));
        break;
      }
    }
  }
}

} // namespace hir
} // namespace jit
