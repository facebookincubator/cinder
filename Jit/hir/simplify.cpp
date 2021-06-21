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
  if (!lhs->isA(TListExact) || !rhs->isA(TLongExact)) {
    return nullptr;
  }

  // TODO(T93509109): Replace TCInt64 with a less platform-specific
  // representation of the type, which should be analagous to Py_ssize_t.
  Register* right_index = env.emit<PrimitiveUnbox>(rhs, TCInt64);
  Register* adjusted_idx =
      env.emit<CheckSequenceBounds>(lhs, right_index, *instr->frameState());
  Register* ob_item =
      env.emit<LoadField>(lhs, offsetof(PyListObject, ob_item), TCPtr);
  return env.emit<LoadArrayItem>(ob_item, adjusted_idx, lhs, TObject);
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
