// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "type.h"

#include "Jit/hir/optimization.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/runtime.h"

#include <fmt/ostream.h>

namespace jit::hir {

// This file contains the Simplify pass, which is a collection of
// strength-reduction optimizations. An optimization should be added as a case
// in Simplify rather than a standalone pass if and only if it meets these
// criteria:
// - It operates on one instruction at a time, with no global analysis or
//   state.
// - Optimizable instructions are replaced with 0 or more new instructions that
//   define an equivalent value while doing less work.
//
// To add support for a new instruction Foo, add a function simplifyFoo(Env&
// env, const Foo* instr) (env can be left out if you don't need it) containing
// the optimization and call it from a new case in
// simplifyInstr(). simplifyFoo() should analyze the given instruction, then do
// one of the following:
// - If the instruction is not optimizable, return nullptr and do not call any
//   functions on env.
// - If the instruction is redundant and can be elided, return the existing
//   value that should replace its output (this is often one of the
//   instruction's inputs).
// - If the instruction can be replaced with a cheaper sequence of
//   instructions, emit those instructions using env.emit<T>(...). For
//   instructions that define an output, emit<T> will allocate and return an
//   appropriately-typed Register* for you, to ease chaining multiple
//   instructions. As with the previous case, return the Register* that should
//   replace the current output of the instruction.
// - If the instruction can be elided but does not produce an output, set
//   env.optimized = true and return nullptr.
//
// Do not modify, unlink, or delete the existing instruction; all of those
// details are handled by existing code outside of the individual optimization
// functions.

namespace {

struct Env {
  Env(Function& f)
      : func{f},
        type_object(
            Type::fromObject(reinterpret_cast<PyObject*>(&PyType_Type))) {}

  // The current function.
  Function& func;

  // The current block being emitted into. Might not be the block originally
  // containing the instruction being optimized, if more blocks have been
  // inserted by the simplify function.
  BasicBlock* block{nullptr};

  // Insertion cursor for new instructions. Must belong to block's Instr::List,
  // and except for brief critical sections during emit functions on Env,
  // should always point to the original, unoptimized instruction.
  Instr::List::iterator cursor;

  // Bytecode instruction of the instruction being optimized, automatically set
  // on all replacement instructions.
  BCOffset bc_off{-1};

  // Set to true by emit<T>() to indicate that the original instruction should
  // be removed.
  bool optimized{false};

  // The object that corresponds to "type".
  Type type_object{TTop};

  // Create and insert the specified instruction. If the instruction has an
  // output, a new Register* will be created and returned.
  template <typename T, typename... Args>
  Register* emit(Args&&... args) {
    if constexpr (T::has_output) {
      return emitRaw<T>(
          func.env.AllocateRegister(), std::forward<Args>(args)...);
    } else {
      return emitRaw<T>(std::forward<Args>(args)...);
    }
  }

  // Similar to emit<T>(), but does not automatically create an output
  // register.
  template <typename T, typename... Args>
  Register* emitRaw(Args&&... args) {
    optimized = true;
    T* instr = T::create(std::forward<Args>(args)...);
    instr->setBytecodeOffset(bc_off);
    block->insert(instr, cursor);

    if constexpr (T::has_output) {
      Register* output = instr->GetOutput();
      output->set_type(outputType(*instr));
      return output;
    }
    return nullptr;
  }

  // Create and return a conditional value. Expects three callables:
  // - do_branch is given two BasicBlock* and should emit a conditional branch
  //   instruction using them.
  // - do_bb1 should emit code for the first successor, returning the computed
  //   value.
  // - do_bb2 should do the same for the second successor.
  template <typename BranchFn, typename Bb1Fn, typename Bb2Fn>
  Register* emitCond(BranchFn do_branch, Bb1Fn do_bb1, Bb2Fn do_bb2) {
    BasicBlock* bb1 = func.cfg.AllocateBlock();
    BasicBlock* bb2 = func.cfg.AllocateBlock();
    do_branch(bb1, bb2);
    JIT_CHECK(
        cursor != block->begin(),
        "block should not be empty after calling do_branch()");
    BasicBlock* tail = block->splitAfter(*std::prev(cursor));

    block = bb1;
    cursor = bb1->end();
    Register* bb1_reg = do_bb1();
    emit<Branch>(tail);

    block = bb2;
    cursor = bb2->end();
    Register* bb2_reg = do_bb2();
    emit<Branch>(tail);

    block = tail;
    cursor = tail->begin();
    std::unordered_map<BasicBlock*, Register*> phi_srcs{
        {bb1, bb1_reg},
        {bb2, bb2_reg},
    };
    return emit<Phi>(phi_srcs);
  }
};

Register* simplifyCheck(const CheckBase* instr) {
  // These all check their input for null.
  if (instr->GetOperand(0)->isA(TObject)) {
    // No UseType is necessary because we never guard potentially-null values.
    return instr->GetOperand(0);
  }
  return nullptr;
}

Register* simplifyCheckSequenceBounds(
    Env& env,
    const CheckSequenceBounds* instr) {
  Register* sequence = instr->GetOperand(0);
  Register* idx = instr->GetOperand(1);
  if (sequence->isA(TTupleExact) && sequence->instr()->IsMakeTuple() &&
      idx->isA(TCInt) && idx->type().hasIntSpec()) {
    size_t length = static_cast<const MakeTuple*>(sequence->instr())->nvalues();
    intptr_t idx_value = idx->type().intSpec();
    bool adjusted = false;
    if (idx_value < 0) {
      idx_value += length;
      adjusted = true;
    }
    if (static_cast<size_t>(idx_value) < length) {
      env.emit<UseType>(sequence, sequence->type());
      env.emit<UseType>(idx, idx->type());
      if (adjusted) {
        return env.emit<LoadConst>(Type::fromCInt(idx_value, TCInt64));
      } else {
        return idx;
      }
    }
  }
  return nullptr;
}

Register* simplifyGuardType(Env& env, const GuardType* instr) {
  Register* input = instr->GetOperand(0);
  Type type = instr->target();
  if (input->isA(type)) {
    // We don't need a UseType: If an instruction cares about the type of this
    // GuardType's output, it will express that through its operand type
    // constraints. Once this GuardType is removed, those constraints will
    // apply to input's instruction rather than this GuardType, and any
    // downstream instructions will still be satisfied.
    return input;
  }
  if (type == TNoneType) {
    return env.emit<GuardIs>(Py_None, input);
  }
  return nullptr;
}

Register* simplifyRefineType(const RefineType* instr) {
  Register* input = instr->GetOperand(0);
  if (input->isA(instr->type())) {
    // No UseType for the same reason as GuardType above: RefineType itself
    // doesn't care about the input's type, only users of its output do, and
    // they're unchanged.
    return input;
  }
  return nullptr;
}

Register* simplifyCast(const Cast* instr) {
  Register* input = instr->GetOperand(0);
  Type type = instr->exact() ? Type::fromTypeExact(instr->pytype())
                             : Type::fromType(instr->pytype());
  if (instr->optional()) {
    type |= TNoneType;
  }
  if (input->isA(type)) {
    // No UseType for the same reason as GuardType above: Cast itself
    // doesn't care about the input's type, only users of its output do, and
    // they're unchanged.
    return input;
  }
  return nullptr;
}

Register* emitGetLengthInt64(Env& env, Register* obj) {
  Type ty = obj->type();
  if (ty <= TListExact || ty <= TTupleExact || ty <= TArray) {
    env.emit<UseType>(obj, ty.unspecialized());
    return env.emit<LoadField>(
        obj, "ob_size", offsetof(PyVarObject, ob_size), TCInt64);
  }
  if (ty <= TDictExact || ty <= TSetExact || ty <= TUnicodeExact) {
    std::size_t offset = 0;
    const char* name = nullptr;
    if (ty <= TDictExact) {
      offset = offsetof(PyDictObject, ma_used);
      name = "ma_used";
    } else if (ty <= TSetExact) {
      offset = offsetof(PySetObject, used);
      name = "used";
    } else if (ty <= TUnicodeExact) {
      // Note: In debug mode, the interpreter has an assert that ensures the
      // string is "ready", check PyUnicode_GET_LENGTH for strings.
      offset = offsetof(PyASCIIObject, length);
      name = "length";
    } else {
      JIT_CHECK(false, "unexpected type");
    }
    env.emit<UseType>(obj, ty.unspecialized());
    return env.emit<LoadField>(obj, name, offset, TCInt64);
  }
  return nullptr;
}

Register* simplifyGetLength(Env& env, const GetLength* instr) {
  Register* obj = instr->GetOperand(0);
  if (Register* size = emitGetLengthInt64(env, obj)) {
    return env.emit<PrimitiveBox>(size, TCInt64, *instr->frameState());
  }
  return nullptr;
}

Register* simplifyIntConvert(Env& env, const IntConvert* instr) {
  Register* src = instr->GetOperand(0);
  if (src->isA(instr->type())) {
    env.emit<UseType>(src, instr->type());
    return instr->GetOperand(0);
  }
  return nullptr;
}

Register* simplifyCompare(Env& env, const Compare* instr) {
  Register* left = instr->GetOperand(0);
  Register* right = instr->GetOperand(1);
  CompareOp op = instr->op();
  if (op == CompareOp::kIs || op == CompareOp::kIsNot) {
    Type left_t = left->type();
    Type right_t = right->type();
    if (!left_t.couldBe(right_t)) {
      env.emit<UseType>(left, left_t);
      env.emit<UseType>(right, right_t);
      return env.emit<LoadConst>(
          Type::fromObject(op == CompareOp::kIs ? Py_False : Py_True));
    }
    PyObject* left_t_obj = left_t.asObject();
    PyObject* right_t_obj = right_t.asObject();
    if (left_t_obj != nullptr && right_t_obj != nullptr) {
      env.emit<UseType>(left, left_t);
      env.emit<UseType>(right, right_t);
      bool same_obj = left_t_obj == right_t_obj;
      bool truthy = (op == CompareOp::kIs) == same_obj;
      return env.emit<LoadConst>(Type::fromObject(truthy ? Py_True : Py_False));
    }
    auto cbool = env.emit<PrimitiveCompare>(
        instr->op() == CompareOp::kIs ? PrimitiveCompareOp::kEqual
                                      : PrimitiveCompareOp::kNotEqual,
        instr->left(),
        instr->right());
    return env.emit<PrimitiveBoxBool>(cbool);
  }
  if (left->isA(TNoneType) && right->isA(TNoneType)) {
    if (op == CompareOp::kEqual || op == CompareOp::kNotEqual) {
      env.emit<UseType>(left, TNoneType);
      env.emit<UseType>(right, TNoneType);
      return env.emit<LoadConst>(
          Type::fromObject(op == CompareOp::kEqual ? Py_True : Py_False));
    }
  }
  // Emit LongCompare if both args are LongExact and the op is supported
  // between two longs.
  if (left->isA(TLongExact) && right->isA(TLongExact) &&
      !(op == CompareOp::kIn || op == CompareOp::kNotIn ||
        op == CompareOp::kExcMatch)) {
    return env.emit<LongCompare>(instr->op(), left, right);
  }
  if (left->isA(TUnicodeExact) && right->isA(TUnicodeExact) &&
      !(op == CompareOp::kIn || op == CompareOp::kNotIn ||
        op == CompareOp::kExcMatch)) {
    return env.emit<UnicodeCompare>(instr->op(), left, right);
  }
  return nullptr;
}

Register* simplifyCondBranch(Env& env, const CondBranch* instr) {
  Type op_type = instr->GetOperand(0)->type();
  if (op_type.hasIntSpec()) {
    if (op_type.intSpec() == 0) {
      return env.emit<Branch>(instr->false_bb());
    }
    return env.emit<Branch>(instr->true_bb());
  }
  return nullptr;
}

Register* simplifyCondBranchCheckType(
    Env& env,
    const CondBranchCheckType* instr) {
  Register* value = instr->GetOperand(0);
  Type actual_type = value->type();
  Type expected_type = instr->type();
  if (actual_type <= expected_type) {
    env.emit<UseType>(value, actual_type);
    return env.emit<Branch>(instr->true_bb());
  }
  if (!actual_type.couldBe(expected_type)) {
    env.emit<UseType>(value, actual_type);
    return env.emit<Branch>(instr->false_bb());
  }
  return nullptr;
}

Register* simplifyIsTruthy(Env& env, const IsTruthy* instr) {
  Type ty = instr->GetOperand(0)->type();
  PyObject* obj = ty.asObject();
  if (obj != nullptr) {
    // Should only consider immutable Objects
    static const std::unordered_set<PyTypeObject*> kTrustedTypes{
        &PyBool_Type,
        &PyFloat_Type,
        &PyLong_Type,
        &PyFrozenSet_Type,
        &PySlice_Type,
        &PyTuple_Type,
        &PyUnicode_Type,
        &_PyNone_Type,
    };
    if (kTrustedTypes.count(Py_TYPE(obj))) {
      int res = PyObject_IsTrue(obj);
      JIT_CHECK(res >= 0, "PyObject_IsTrue failed on trusted type");
      // Since we no longer use instr->GetOperand(0), we need to make sure that
      // we don't lose any associated type checks
      env.emit<UseType>(instr->GetOperand(0), ty);
      Type output_type = instr->GetOutput()->type();
      return env.emit<LoadConst>(Type::fromCInt(res, output_type));
    }
  }
  if (ty <= TBool) {
    Register* left = instr->GetOperand(0);
    env.emit<UseType>(left, TBool);
    Register* right = env.emit<LoadConst>(Type::fromObject(Py_True));
    Register* result =
        env.emit<PrimitiveCompare>(PrimitiveCompareOp::kEqual, left, right);
    return env.emit<IntConvert>(result, TCInt32);
  }
  if (Register* size = emitGetLengthInt64(env, instr->GetOperand(0))) {
    return env.emit<IntConvert>(size, TCInt32);
  }
  if (ty <= TLongExact) {
    Register* left = instr->GetOperand(0);
    env.emit<UseType>(left, ty);
    // Zero is canonical as a "small int" in CPython.
    ThreadedCompileSerialize guard;
    auto zero = Ref<>::steal(PyLong_FromLong(0));
    Register* right = env.emit<LoadConst>(
        Type::fromObject(env.func.env.addReference(std::move(zero))));
    Register* result =
        env.emit<PrimitiveCompare>(PrimitiveCompareOp::kNotEqual, left, right);
    return env.emit<IntConvert>(result, TCInt32);
  }
  return nullptr;
}

Register* simplifyLoadTupleItem(Env& env, const LoadTupleItem* instr) {
  Register* src = instr->GetOperand(0);
  Type src_ty = src->type();
  if (!src_ty.hasValueSpec(TTuple)) {
    return nullptr;
  }
  env.emit<UseType>(src, src_ty);
  return env.emit<LoadConst>(
      Type::fromObject(PyTuple_GET_ITEM(src_ty.objectSpec(), instr->idx())));
}

Register* simplifyBinaryOp(Env& env, const BinaryOp* instr) {
  Register* lhs = instr->left();
  Register* rhs = instr->right();
  if (instr->op() == BinaryOpKind::kSubscript) {
    if (lhs->isA(TDictExact)) {
      return env.emit<DictSubscr>(lhs, rhs, *instr->frameState());
    }
    if (!rhs->isA(TLongExact)) {
      return nullptr;
    }
    Type lhs_type = lhs->type();
    Type rhs_type = rhs->type();
    if (lhs_type <= TTupleExact && lhs_type.hasObjectSpec() &&
        rhs_type.hasObjectSpec()) {
      int overflow;
      Py_ssize_t index =
          PyLong_AsLongAndOverflow(rhs_type.objectSpec(), &overflow);
      if (!overflow) {
        PyObject* lhs_obj = lhs_type.objectSpec();
        if (index >= 0 && index < PyTuple_GET_SIZE(lhs_obj)) {
          PyObject* item = PyTuple_GET_ITEM(lhs_obj, index);
          env.emit<UseType>(lhs, lhs_type);
          env.emit<UseType>(rhs, rhs_type);
          return env.emit<LoadConst>(
              Type::fromObject(env.func.env.addReference(item)));
        }
        // Fallthrough
      }
      // Fallthrough
    }
    if (lhs->isA(TListExact) || lhs->isA(TTupleExact)) {
      // TODO(T93509109): Replace TCInt64 with a less platform-specific
      // representation of the type, which should be analagous to Py_ssize_t.
      env.emit<UseType>(lhs, lhs->isA(TListExact) ? TListExact : TTupleExact);
      env.emit<UseType>(rhs, TLongExact);
      Register* right_index = env.emit<PrimitiveUnbox>(rhs, TCInt64);
      Register* adjusted_idx =
          env.emit<CheckSequenceBounds>(lhs, right_index, *instr->frameState());
      ssize_t offset = offsetof(PyTupleObject, ob_item);
      Register* array = lhs;
      // Lists carry a nested array of ob_item whereas tuples are variable-sized
      // structs.
      if (lhs->isA(TListExact)) {
        array = env.emit<LoadField>(
            lhs, "ob_item", offsetof(PyListObject, ob_item), TCPtr);
        offset = 0;
      }
      return env.emit<LoadArrayItem>(array, adjusted_idx, lhs, offset, TObject);
    }
  }
  if (lhs->isA(TLongExact) && rhs->isA(TLongExact)) {
    // All binary ops on TLong's return mutable so can be freely simplified with
    // no explicit check.
    if (instr->op() == BinaryOpKind::kMatrixMultiply ||
        instr->op() == BinaryOpKind::kSubscript) {
      // These will generate an error at runtime.
      return nullptr;
    }
    env.emit<UseType>(lhs, TLongExact);
    env.emit<UseType>(rhs, TLongExact);
    return env.emit<LongBinaryOp>(instr->op(), lhs, rhs, *instr->frameState());
  }
  if ((lhs->isA(TUnicodeExact) && rhs->isA(TLongExact)) &&
      (instr->op() == BinaryOpKind::kMultiply)) {
    Register* unboxed_rhs = env.emit<PrimitiveUnbox>(rhs, TCInt64);
    env.emit<IsNegativeAndErrOccurred>(unboxed_rhs, *instr->frameState());
    return env.emit<UnicodeRepeat>(lhs, unboxed_rhs, *instr->frameState());
  }
  if ((lhs->isA(TUnicodeExact) && rhs->isA(TUnicodeExact)) &&
      (instr->op() == BinaryOpKind::kAdd)) {
    return env.emit<UnicodeConcat>(lhs, rhs, *instr->frameState());
  }

  // Unsupported case.
  return nullptr;
}

Register* simplifyLongBinaryOp(Env& env, const LongBinaryOp* instr) {
  Type left_type = instr->left()->type();
  Type right_type = instr->right()->type();
  if (left_type.hasObjectSpec() && right_type.hasObjectSpec()) {
    ThreadedCompileSerialize guard;
    Ref<> result;
    if (instr->op() == BinaryOpKind::kPower) {
      result = Ref<>::steal(PyLong_Type.tp_as_number->nb_power(
          left_type.objectSpec(), right_type.objectSpec(), Py_None));
    } else {
      binaryfunc helper = instr->slotMethod();
      result = Ref<>::steal(
          (*helper)(left_type.objectSpec(), right_type.objectSpec()));
    }
    if (result == nullptr) {
      PyErr_Clear();
      return nullptr;
    }
    env.emit<UseType>(instr->left(), left_type);
    env.emit<UseType>(instr->right(), right_type);
    return env.emit<LoadConst>(
        Type::fromObject(env.func.env.addReference(std::move(result))));
  }
  return nullptr;
}

Register* simplifyUnaryOp(Env& env, const UnaryOp* instr) {
  Register* operand = instr->operand();

  if (instr->op() == UnaryOpKind::kNot && operand->isA(TBool)) {
    env.emit<UseType>(operand, TBool);
    Register* unboxed = env.emit<PrimitiveUnbox>(operand, TCBool);
    Register* negated =
        env.emit<PrimitiveUnaryOp>(PrimitiveUnaryOpKind::kNotInt, unboxed);
    return env.emit<PrimitiveBoxBool>(negated);
  }

  return nullptr;
}

Register* simplifyPrimitiveCompare(const PrimitiveCompare* instr) {
  if (instr->op() == PrimitiveCompareOp::kEqual) {
    Register* lhs = instr->GetOperand(0);
    Type rhs_type = instr->GetOperand(1)->type();
    if (lhs->instr()->IsPrimitiveBoxBool() && rhs_type.asObject() == Py_True) {
      // If we're comparing a boxed bool to Py_True, return the original,
      // unboxed CBool.
      return lhs->instr()->GetOperand(0);
    }
  }
  return nullptr;
}

Register* simplifyPrimitiveUnbox(Env& env, const PrimitiveUnbox* instr) {
  Register* unboxed_value = instr->GetOperand(0);
  Type unbox_output_type = instr->GetOutput()->type();
  // Ensure that we are dealing with either a integer or a double.
  Type unboxed_value_type = unboxed_value->type();
  if (!(unboxed_value_type.hasObjectSpec())) {
    return nullptr;
  }
  PyObject* value = unboxed_value_type.objectSpec();
  if (unbox_output_type <= (TCSigned | TCUnsigned)) {
    if (!PyLong_Check(value)) {
      return nullptr;
    }
    int overflow = 0;
    long number =
        PyLong_AsLongAndOverflow(unboxed_value_type.objectSpec(), &overflow);
    if (overflow != 0) {
      return nullptr;
    }
    if (unbox_output_type <= TCSigned) {
      if (!Type::CIntFitsType(number, unbox_output_type)) {
        return nullptr;
      }
      return env.emit<LoadConst>(Type::fromCInt(number, unbox_output_type));
    } else {
      if (!Type::CUIntFitsType(number, unbox_output_type)) {
        return nullptr;
      }
      return env.emit<LoadConst>(Type::fromCUInt(number, unbox_output_type));
    }
  } else if (unbox_output_type <= TCDouble) {
    if (!PyFloat_Check(value)) {
      return nullptr;
    }
    double number = PyFloat_AS_DOUBLE(unboxed_value_type.objectSpec());
    return env.emit<LoadConst>(Type::fromCDouble(number));
  }
  return nullptr;
}

Register* simplifyLoadAttr(Env& env, const LoadAttr* load_attr) {
  Register* receiver = load_attr->GetOperand(0);
  if (!receiver->isA(TType)) {
    return nullptr;
  }

  const int cache_id = env.func.env.allocateLoadTypeAttrCache();
  env.emit<UseType>(receiver, TType);
  Register* guard = env.emit<LoadTypeAttrCacheItem>(cache_id, 0);
  Register* type_matches =
      env.emit<PrimitiveCompare>(PrimitiveCompareOp::kEqual, guard, receiver);
  return env.emitCond(
      [&](BasicBlock* fast_path, BasicBlock* slow_path) {
        env.emit<CondBranch>(type_matches, fast_path, slow_path);
      },
      [&] { // Fast path
        return env.emit<LoadTypeAttrCacheItem>(cache_id, 1);
      },
      [&] { // Slow path
        int name_idx = load_attr->name_idx();
        return env.emit<FillTypeAttrCache>(
            receiver, name_idx, cache_id, *load_attr->frameState());
      });
}

// If we're loading ob_fval from a known float into a double, this can be
// simplified into a LoadConst.
Register* simplifyLoadField(Env& env, const LoadField* instr) {
  Register* loadee = instr->GetOperand(0);
  Type load_output_type = instr->GetOutput()->type();
  // Ensure that we are dealing with either a integer or a double.
  Type loadee_type = loadee->type();
  if (!loadee_type.hasObjectSpec()) {
    return nullptr;
  }
  PyObject* value = loadee_type.objectSpec();
  if (PyFloat_Check(value) && load_output_type <= TCDouble &&
      instr->offset() == offsetof(PyFloatObject, ob_fval)) {
    double number = PyFloat_AS_DOUBLE(loadee_type.objectSpec());
    env.emit<UseType>(loadee, loadee_type);
    return env.emit<LoadConst>(Type::fromCDouble(number));
  }
  return nullptr;
}

Register* simplifyIsNegativeAndErrOccurred(
    Env& env,
    const IsNegativeAndErrOccurred* instr) {
  if (!instr->GetOperand(0)->instr()->IsLoadConst()) {
    return nullptr;
  }
  // Other optimizations might reduce the strength of global loads, etc. to load
  // consts. If this is the case, we know that there can't be an active
  // exception. In this case, the IsNegativeAndErrOccurred instruction has a
  // known result. Instead of deleting it, we replace it with load of false -
  // the idea is that if there are other downstream consumers of it, they will
  // still have access to the result. Otherwise, DCE will take care of this.
  Type output_type = instr->GetOutput()->type();
  return env.emit<LoadConst>(Type::fromCInt(0, output_type));
}

static bool isBuiltin(PyMethodDef* meth, const char* name) {
  // To make sure we have the right function, look up the PyMethodDef in the
  // fixed builtins. Any joker can make a new C method called "len", for
  // example.
  const Builtins& builtins = Runtime::get()->builtins();
  return builtins.find(meth) == name;
}

static bool isBuiltin(Register* callable, const char* name) {
  Type callable_type = callable->type();
  if (!callable_type.hasObjectSpec()) {
    return false;
  }
  PyObject* callable_obj = callable_type.objectSpec();
  if (Py_TYPE(callable_obj) == &PyCFunction_Type) {
    PyCFunctionObject* func =
        reinterpret_cast<PyCFunctionObject*>(callable_obj);
    return isBuiltin(func->m_ml, name);
  }
  if (Py_TYPE(callable_obj) == &PyMethodDescr_Type) {
    PyMethodDescrObject* meth =
        reinterpret_cast<PyMethodDescrObject*>(callable_obj);
    return isBuiltin(meth->d_method, name);
  }
  return false;
}

Register* simplifyVectorCall(Env& env, const VectorCall* instr) {
  Register* target = instr->GetOperand(0);
  Type target_type = target->type();
  if (target_type == env.type_object && instr->NumOperands() == 2) {
    env.emit<UseType>(target, env.type_object);
    return env.emit<LoadField>(
        instr->GetOperand(1), "ob_type", offsetof(PyObject, ob_type), TType);
  }
  if (isBuiltin(target, "len") && instr->numArgs() == 1) {
    env.emit<UseType>(target, target->type());
    return env.emit<GetLength>(instr->arg(0), *instr->frameState());
  }
  return nullptr;
}

Register* simplifyVectorCallStatic(Env& env, const VectorCallStatic* instr) {
  Register* func = instr->func();
  if (isBuiltin(func, "list.append") && instr->numArgs() == 2) {
    env.emit<UseType>(func, func->type());
    env.emit<ListAppend>(instr->arg(0), instr->arg(1), *instr->frameState());
    return env.emit<LoadConst>(TNoneType);
  }
  return nullptr;
}

Register* simplifyInstr(Env& env, const Instr* instr) {
  switch (instr->opcode()) {
    case Opcode::kCheckVar:
    case Opcode::kCheckExc:
    case Opcode::kCheckField:
      return simplifyCheck(static_cast<const CheckBase*>(instr));
    case Opcode::kCheckSequenceBounds:
      return simplifyCheckSequenceBounds(
          env, static_cast<const CheckSequenceBounds*>(instr));
    case Opcode::kGuardType:
      return simplifyGuardType(env, static_cast<const GuardType*>(instr));
    case Opcode::kRefineType:
      return simplifyRefineType(static_cast<const RefineType*>(instr));
    case Opcode::kCast:
      return simplifyCast(static_cast<const Cast*>(instr));

    case Opcode::kCompare:
      return simplifyCompare(env, static_cast<const Compare*>(instr));

    case Opcode::kCondBranch:
      return simplifyCondBranch(env, static_cast<const CondBranch*>(instr));
    case Opcode::kCondBranchCheckType:
      return simplifyCondBranchCheckType(
          env, static_cast<const CondBranchCheckType*>(instr));

    case Opcode::kGetLength:
      return simplifyGetLength(env, static_cast<const GetLength*>(instr));

    case Opcode::kIntConvert:
      return simplifyIntConvert(env, static_cast<const IntConvert*>(instr));

    case Opcode::kIsTruthy:
      return simplifyIsTruthy(env, static_cast<const IsTruthy*>(instr));

    case Opcode::kLoadAttr:
      return simplifyLoadAttr(env, static_cast<const LoadAttr*>(instr));
    case Opcode::kLoadField:
      return simplifyLoadField(env, static_cast<const LoadField*>(instr));
    case Opcode::kLoadTupleItem:
      return simplifyLoadTupleItem(
          env, static_cast<const LoadTupleItem*>(instr));

    case Opcode::kBinaryOp:
      return simplifyBinaryOp(env, static_cast<const BinaryOp*>(instr));
    case Opcode::kLongBinaryOp:
      return simplifyLongBinaryOp(env, static_cast<const LongBinaryOp*>(instr));
    case Opcode::kUnaryOp:
      return simplifyUnaryOp(env, static_cast<const UnaryOp*>(instr));

    case Opcode::kPrimitiveCompare:
      return simplifyPrimitiveCompare(
          static_cast<const PrimitiveCompare*>(instr));
    case Opcode::kPrimitiveUnbox:
      return simplifyPrimitiveUnbox(
          env, static_cast<const PrimitiveUnbox*>(instr));

    case Opcode::kIsNegativeAndErrOccurred:
      return simplifyIsNegativeAndErrOccurred(
          env, static_cast<const IsNegativeAndErrOccurred*>(instr));

    case Opcode::kVectorCall:
      return simplifyVectorCall(env, static_cast<const VectorCall*>(instr));
    case Opcode::kVectorCallStatic:
      return simplifyVectorCallStatic(
          env, static_cast<const VectorCallStatic*>(instr));
    default:
      return nullptr;
  }
}

} // namespace

void Simplify::Run(Function& irfunc) {
  Env env{irfunc};
  bool changed;
  do {
    changed = false;
    for (auto cfg_it = irfunc.cfg.blocks.begin();
         cfg_it != irfunc.cfg.blocks.end();) {
      BasicBlock& block = *cfg_it;
      ++cfg_it;
      env.block = &block;

      for (auto blk_it = block.begin(); blk_it != block.end();) {
        Instr& instr = *blk_it;
        ++blk_it;

        env.optimized = false;
        env.cursor = block.iterator_to(instr);
        env.bc_off = instr.bytecodeOffset();
        Register* new_output = simplifyInstr(env, &instr);
        JIT_CHECK(
            env.cursor == env.block->iterator_to(instr),
            "Simplify functions are expected to leave env.cursor pointing to "
            "the original instruction, with new instructions inserted before "
            "it.");
        if (new_output == nullptr && !env.optimized) {
          continue;
        }

        changed = true;
        JIT_CHECK(
            (new_output == nullptr) == (instr.GetOutput() == nullptr),
            "Simplify function should return a new output if and only if the "
            "existing instruction has an output");
        if (new_output != nullptr) {
          JIT_CHECK(
              new_output->type() <= instr.GetOutput()->type(),
              "New output type %s isn't compatible with old output type %s",
              new_output->type(),
              instr.GetOutput()->type());
          env.emitRaw<Assign>(instr.GetOutput(), new_output);
        }

        if (instr.IsCondBranch() || instr.IsCondBranchIterNotDone() ||
            instr.IsCondBranchCheckType()) {
          JIT_CHECK(env.cursor != env.block->begin(), "Unexpected empty block");
          Instr& prev_instr = *std::prev(env.cursor);
          JIT_CHECK(
              prev_instr.IsBranch(),
              "The only supported simplification for CondBranch* is to a "
              "Branch, got unexpected '%s'",
              prev_instr);

          // If we've optimized a CondBranchBase into a Branch, we also need to
          // remove any Phi references to the current block from the block that
          // we no longer visit.
          auto cond = static_cast<CondBranchBase*>(&instr);
          BasicBlock* new_dst = prev_instr.successor(0);
          BasicBlock* old_branch_block =
              cond->false_bb() == new_dst ? cond->true_bb() : cond->false_bb();
          old_branch_block->removePhiPredecessor(cond->block());
        }

        instr.unlink();
        delete &instr;

        if (env.block != &block) {
          // If we're now in a different block, `block' should only contain the
          // newly-emitted instructions, with no more old instructions to
          // process. Continue to the next block in the list; any newly-created
          // blocks were added to the end of the list and will be processed
          // later.
          break;
        }
      }
    }

    if (changed) {
      // Perform some simple cleanup between each pass.
      CopyPropagation{}.Run(irfunc);
      reflowTypes(irfunc);
      CleanCFG{}.Run(irfunc);
    }
  } while (changed);
}

} // namespace jit::hir
