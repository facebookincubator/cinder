// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/optimization.h"

#include "Jit/hir/ssa.h"

namespace jit {
namespace hir {

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
// - If the instruction can be elided but does not produce an output, that case
//   is currently unsupported for now.
//
// Do not modify, unlink, or delete the existing instruction; all of those
// details are handled by existing code outside of the individual optimization
// functions.

namespace {

struct Env {
  Env(Function& f) : func{f} {}

  Function& func;
  std::vector<Instr*> new_instrs;

  template <typename T, typename... Args>
  Register* emit(Args&&... args) {
    T* instr;
    if constexpr (T::has_output) {
      instr =
          T::create(func.env.AllocateRegister(), std::forward<Args>(args)...);
    } else {
      instr = T::create(std::forward<Args>(args)...);
    }
    new_instrs.emplace_back(instr);
    if constexpr (T::has_output) {
      Register* output = instr->GetOutput();
      output->set_type(outputType(*instr));
      return output;
    }
    return nullptr;
  }
};

Register* simplifyCheck(const CheckBase* instr) {
  // These all check their input for null.
  if (instr->GetOperand(0)->isA(TObject)) {
    return instr->GetOperand(0);
  }
  return nullptr;
}

Register* simplifyIntConvert(const IntConvert* instr) {
  if (instr->GetOperand(0)->isA(instr->type())) {
    return instr->GetOperand(0);
  }
  return nullptr;
}

Register* simplifyLoadTupleItem(Env& env, const LoadTupleItem* instr) {
  Register* src = instr->GetOperand(0);
  Instr* def_instr = src->instr();
  if (!def_instr->IsLoadConst()) {
    return nullptr;
  }
  auto load_const = static_cast<const LoadConst*>(def_instr);
  Type const_ty = load_const->type();
  if (!const_ty.hasValueSpec(TTuple)) {
    return nullptr;
  }
  return env.emit<LoadConst>(
      Type::fromObject(PyTuple_GET_ITEM(const_ty.objectSpec(), instr->idx())));
}

Register* simplifyBinaryOp(Env& env, const BinaryOp* instr) {
  if (instr->op() != BinaryOpKind::kSubscript) {
    return nullptr;
  }
  Register* lhs = instr->left();
  Register* rhs = instr->right();
  if (!rhs->isA(TLongExact)) {
    return nullptr;
  }
  if (lhs->isA(TListExact) || lhs->isA(TTupleExact)) {
    // TODO(T93509109): Replace TCInt64 with a less platform-specific
    // representation of the type, which should be analagous to Py_ssize_t.
    Register* right_index = env.emit<PrimitiveUnbox>(rhs, TCInt64);
    Register* adjusted_idx =
        env.emit<CheckSequenceBounds>(lhs, right_index, *instr->frameState());
    ssize_t offset = offsetof(PyTupleObject, ob_item);
    Register* array = lhs;
    // Lists carry a nested array of ob_item whereas tuples are variable-sized
    // structs.
    if (lhs->isA(TListExact)) {
      array = env.emit<LoadField>(lhs, offsetof(PyListObject, ob_item), TCPtr);
      offset = 0;
    }
    return env.emit<LoadArrayItem>(array, adjusted_idx, lhs, offset, TObject);
  }
  // Unsupported case.
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

Register* simplifyInstr(Env& env, const Instr* instr) {
  switch (instr->opcode()) {
    case Opcode::kCheckVar:
    case Opcode::kCheckExc:
    case Opcode::kCheckField:
      return simplifyCheck(static_cast<const CheckBase*>(instr));

    case Opcode::kIntConvert:
      return simplifyIntConvert(static_cast<const IntConvert*>(instr));

    case Opcode::kLoadTupleItem:
      return simplifyLoadTupleItem(
          env, static_cast<const LoadTupleItem*>(instr));

    case Opcode::kBinaryOp:
      return simplifyBinaryOp(env, static_cast<const BinaryOp*>(instr));

    case Opcode::kPrimitiveUnbox:
      return simplifyPrimitiveUnbox(
          env, static_cast<const PrimitiveUnbox*>(instr));

    case Opcode::kIsNegativeAndErrOccurred:
      return simplifyIsNegativeAndErrOccurred(
          env, static_cast<const IsNegativeAndErrOccurred*>(instr));
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
    for (BasicBlock& block : irfunc.cfg.blocks) {
      for (auto it = block.begin(); it != block.end();) {
        Instr& instr = *it;
        ++it;
        Register* new_output = simplifyInstr(env, &instr);
        if (new_output == nullptr && env.new_instrs.empty()) {
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
          env.new_instrs.emplace_back(
              Assign::create(instr.GetOutput(), new_output));
        }
        for (Instr* new_instr : env.new_instrs) {
          new_instr->copyBytecodeOffset(instr);
          new_instr->InsertBefore(instr);
        }
        env.new_instrs.clear();
        instr.unlink();
        delete &instr;
      }
    }

    if (changed) {
      // Perform some simple cleanup between each pass.
      CopyPropagation{}.Run(irfunc);
      irfunc.cfg.RemoveTrampolineBlocks();
      reflowTypes(irfunc);
    }
  } while (changed);
}

} // namespace hir
} // namespace jit
