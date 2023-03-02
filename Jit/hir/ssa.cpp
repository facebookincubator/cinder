// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/ssa.h"

#include "Jit/bitvector.h"
#include "Jit/hir/analysis.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/type.h"
#include "Jit/log.h"
#include "Jit/runtime.h"

#include <fmt/ostream.h>

#include <ostream>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jit::hir {

namespace {
struct CheckEnv {
  CheckEnv(const Function& func, std::ostream& err)
      : func{func}, err{err}, assign{func, true} {
    assign.Run();
  }

  const Function& func;
  std::ostream& err;
  bool ok{true};

  // Definite assignment analysis. Used to ensure all uses of a register are
  // dominated by its definition.
  AssignmentAnalysis assign;

  // Flow-insensitive map from register definitions to the source
  // block. Tracked separately from `assign` to ensure no register is defined
  // twice, even if the first definition doesn't dominate the second.
  std::unordered_map<const Register*, const BasicBlock*> defs;

  // Current set of defined registers.
  RegisterSet defined;

  // Current block and instruction.
  const BasicBlock* block{nullptr};
  const Instr* instr{nullptr};
};

// Verify the following:
// - All blocks reachable from the entry block are part of this CFG.
// - The CFG's block list contains no unreachable blocks.
// - No reachable blocks have any unreachable predecessors.
// - No blocks have > 1 edge from the same predecessor
bool checkCFG(const Function& func, std::ostream& err) {
  std::queue<const BasicBlock*> queue;
  std::unordered_set<const BasicBlock*> reachable;
  queue.push(func.cfg.entry_block);
  reachable.insert(func.cfg.entry_block);
  while (!queue.empty()) {
    auto block = queue.front();
    queue.pop();

    if (block->cfg != &func.cfg) {
      fmt::print(err, "ERROR: Reachable bb {} isn't part of CFG\n", block->id);
      return false;
    }

    for (auto edge : block->out_edges()) {
      auto succ = edge->to();
      if (reachable.emplace(succ).second) {
        queue.push(succ);
      }
    }
  }

  for (auto& block : func.cfg.blocks) {
    if (!reachable.count(&block)) {
      fmt::print(err, "ERROR: CFG contains unreachable bb {}\n", block.id);
      return false;
    }

    std::unordered_set<BasicBlock*> seen;
    for (auto edge : block.in_edges()) {
      auto pred = edge->from();
      if (!reachable.count(pred)) {
        fmt::print(
            err,
            "ERROR: bb {} has unreachable predecessor bb {}\n",
            block.id,
            pred->id);
        return false;
      }
      if (seen.count(pred)) {
        fmt::print(
            err,
            "ERROR: bb {} has > 1 edge from predecessor bb {}\n",
            block.id,
            pred->id);
        return false;
      }
      seen.insert(pred);
    }
  }

  return true;
}

void checkPhi(CheckEnv& env) {
  auto& phi = static_cast<const Phi&>(*env.instr);
  auto block = phi.block();
  std::unordered_set<const BasicBlock*> preds;
  for (auto edge : block->in_edges()) {
    preds.emplace(edge->from());
  }
  for (auto phi_block : phi.basic_blocks()) {
    if (preds.count(phi_block) == 0) {
      fmt::print(
          env.err,
          "ERROR: Instruction '{}' in bb {} references bb {}, which isn't a "
          "predecessor\n",
          phi,
          block->id,
          phi_block->id);
      env.ok = false;
    }
  }
}

void checkTerminator(CheckEnv& env) {
  auto is_last = env.instr == &env.block->back();
  if (env.instr->IsTerminator() && !is_last) {
    fmt::print(
        env.err,
        "ERROR: bb {} contains terminator '{}' in non-terminal position\n",
        env.block->id,
        *env.instr);
    env.ok = false;
  }
  if (is_last && !env.instr->IsTerminator()) {
    fmt::print(
        env.err, "ERROR: bb {} has no terminator at end\n", env.block->id);
    env.ok = false;
  }
}

void checkRegisters(CheckEnv& env) {
  if (env.instr->IsPhi()) {
    auto phi = static_cast<const Phi*>(env.instr);
    for (size_t i = 0; i < phi->NumOperands(); ++i) {
      auto operand = phi->GetOperand(i);
      if (!env.assign.IsAssignedOut(phi->basic_blocks()[i], operand)) {
        fmt::print(
            env.err,
            "ERROR: Phi input '{}' to instruction '{}' in bb {} not "
            "defined at end of bb {}\n",
            operand->name(),
            *phi,
            env.block->id,
            phi->basic_blocks()[i]->id);
        env.ok = false;
      }
    }
  } else {
    for (size_t i = 0, n = env.instr->NumOperands(); i < n; ++i) {
      auto operand = env.instr->GetOperand(i);
      if (!env.defined.count(operand)) {
        fmt::print(
            env.err,
            "ERROR: Operand '{}' of instruction '{}' not defined at use in "
            "bb {}\n",
            operand->name(),
            *env.instr,
            env.block->id);
        env.ok = false;
      }
    }
  }

  if (auto output = env.instr->GetOutput()) {
    if (output->instr() != env.instr) {
      fmt::print(
          env.err,
          "ERROR: {}'s instr is not '{}', which claims to define it\n",
          output->name(),
          *env.instr);
      env.ok = false;
    }

    auto pair = env.defs.emplace(output, env.block);
    if (!pair.second) {
      fmt::print(
          env.err,
          "ERROR: {} redefined in bb {}; previous definition was in bb {}\n",
          output->name(),
          env.block->id,
          pair.first->second->id);
      env.ok = false;
    }
    env.defined.insert(output);
  }
}
} // namespace

// Verify the following properties:
//
// - The CFG is well-formed (see checkCFG() for details).
// - Every block has exactly one terminator instruction, as its final
//   instruction. This implies that blocks cannot be empty, which is also
//   verified.
// - Phi instructions do not appear after any non-Phi instructions in their
//   block.
// - Phi instructions only reference direct predecessors.
// - No register is assigned to by more than one instruction.
// - Every register has a link to its defining instruction.
// - All uses of a register are dominated by its definition.
bool checkFunc(const Function& func, std::ostream& err) {
  if (!checkCFG(func, err)) {
    return false;
  }

  CheckEnv env{func, err};
  for (auto& block : func.cfg.blocks) {
    env.block = &block;
    env.defined = env.assign.GetIn(&block);

    if (block.empty()) {
      fmt::print(err, "ERROR: bb {} has no instructions\n", block.id);
      env.ok = false;
      continue;
    }

    bool phi_section = true;
    bool allow_prologue_loads = env.block == func.cfg.entry_block;
    for (auto& instr : block) {
      env.instr = &instr;

      if (instr.IsPhi()) {
        if (!phi_section) {
          fmt::print(
              err,
              "ERROR: '{}' in bb {} comes after non-Phi instruction\n",
              instr,
              block.id);
          env.ok = false;
          continue;
        }
        checkPhi(env);
      } else {
        phi_section = false;
      }

      if (instr.IsLoadArg() || instr.IsLoadCurrentFunc()) {
        if (!allow_prologue_loads) {
          fmt::print(
              err,
              "ERROR: '{}' in bb {} comes after non-LoadArg instruction\n",
              instr,
              block.id);
          env.ok = false;
        }
      } else {
        allow_prologue_loads = false;
      }

      checkTerminator(env);
      checkRegisters(env);
    }
  }

  return env.ok;
}

static const UnorderedMap<std::string, Type> kBuiltinFunctionTypes = {
    {"dict.copy", TDictExact},
    {"hasattr", TBool},
    {"len", TLongExact},
    {"list.copy", TListExact},
    {"list.count", TLongExact},
    {"list.index", TLongExact},
    {"str.capitalize", TUnicodeExact},
    {"str.center", TUnicodeExact},
    {"str.count", TLongExact},
    {"str.endswith", TBool},
    {"str.find", TLongExact},
    {"str.format", TUnicodeExact},
    {"str.index", TLongExact},
    {"str.isalnum", TBool},
    {"str.isalpha", TBool},
    {"str.isascii", TBool},
    {"str.isdecimal", TBool},
    {"str.isdigit", TBool},
    {"str.isidentifier", TBool},
    {"str.islower", TBool},
    {"str.isnumeric", TBool},
    {"str.isprintable", TBool},
    {"str.isspace", TBool},
    {"str.istitle", TBool},
    {"str.isupper", TBool},
    {"str.join", TUnicodeExact},
    {"str.lower", TUnicodeExact},
    {"str.lstrip", TUnicodeExact},
    {"str.partition", TTupleExact},
    {"str.replace", TUnicodeExact},
    {"str.rfind", TLongExact},
    {"str.rindex", TLongExact},
    {"str.rpartition", TTupleExact},
    {"str.rsplit", TListExact},
    {"str.split", TListExact},
    {"str.splitlines", TListExact},
    {"str.upper", TUnicodeExact},
    {"tuple.count", TLongExact},
    {"tuple.index", TLongExact},
};

Type returnType(PyMethodDef* meth) {
  // To make sure we have the right function, look up the PyMethodDef in the
  // fixed builtins. Any joker can make a new C method called "len", for
  // example.
  const Builtins& builtins = Runtime::get()->builtins();
  auto name = builtins.find(meth);
  if (!name.has_value()) {
    return TObject;
  }
  auto return_type = kBuiltinFunctionTypes.find(name.value());
  if (return_type == kBuiltinFunctionTypes.end()) {
    return TObject;
  }
  return return_type->second;
}

Type returnType(Type callable) {
  if (!callable.hasObjectSpec()) {
    return TObject;
  }
  PyObject* callable_obj = callable.objectSpec();
  if (Py_TYPE(callable_obj) == &PyCFunction_Type) {
    PyCFunctionObject* func =
        reinterpret_cast<PyCFunctionObject*>(callable_obj);
    return returnType(func->m_ml);
  }
  if (Py_TYPE(callable_obj) == &PyMethodDescr_Type) {
    PyMethodDescrObject* meth =
        reinterpret_cast<PyMethodDescrObject*>(callable_obj);
    return returnType(meth->d_method);
  }
  return TObject;
}

Type outputType(
    const Instr& instr,
    const std::function<Type(std::size_t)>& get_op_type) {
  switch (instr.opcode()) {
    case Opcode::kCallEx:
      return returnType(static_cast<const CallEx&>(instr).func()->type());
    case Opcode::kCallExKw:
      return returnType(static_cast<const CallExKw&>(instr).func()->type());
    case Opcode::kVectorCall:
    case Opcode::kVectorCallKW:
    case Opcode::kVectorCallStatic:
      return returnType(
          static_cast<const VectorCallBase&>(instr).func()->type());

    case Opcode::kCompare: {
      CompareOp op = static_cast<const Compare&>(instr).op();
      if (op == CompareOp::kIn || op == CompareOp::kNotIn) {
        return TBool;
      }
      return TObject;
    }

    case Opcode::kCallMethod:
    case Opcode::kDictSubscr:
    case Opcode::kBinaryOp:
    case Opcode::kFillTypeAttrCache:
    case Opcode::kGetAIter:
    case Opcode::kGetANext:
    case Opcode::kGetIter:
    case Opcode::kImportFrom:
    case Opcode::kImportName:
    case Opcode::kInPlaceOp:
    case Opcode::kInvokeIterNext:
    case Opcode::kInvokeMethod:
    case Opcode::kLoadAttr:
    case Opcode::kLoadAttrSpecial:
    case Opcode::kLoadAttrSuper:
    case Opcode::kLoadGlobal:
    case Opcode::kLoadMethod:
    case Opcode::kLoadMethodSuper:
    case Opcode::kLoadTupleItem:
    case Opcode::kMatchKeys:
    case Opcode::kWaitHandleLoadCoroOrResult:
    case Opcode::kYieldAndYieldFrom:
    case Opcode::kYieldFrom:
    case Opcode::kYieldFromHandleStopAsyncIteration:
    case Opcode::kYieldValue:
      return TObject;
    case Opcode::kBuildString:
      return TMortalUnicode;
    case Opcode::kGetLength:
      return TLongExact;
    case Opcode::kCopyDictWithoutKeys:
      return TDictExact;
    case Opcode::kUnaryOp: {
      auto op = static_cast<const UnaryOp&>(instr).op();
      if (op == UnaryOpKind::kNot) {
        return TBool;
      }
      return TObject;
    }
    // Many opcodes just return a possibly-null PyObject*. Some of these will
    // be further specialized based on the input types in the hopefully near
    // future.
    case Opcode::kCallCFunc:
    case Opcode::kGetLoadMethodInstance:
    case Opcode::kLoadCellItem:
    case Opcode::kLoadGlobalCached:
    case Opcode::kMatchClass:
    case Opcode::kStealCellItem:
    case Opcode::kWaitHandleLoadWaiter:
      return TOptObject;

    case Opcode::kFormatValue:
      return TUnicode;

    case Opcode::kLoadVarObjectSize:
      return TCInt64;
    case Opcode::kInvokeStaticFunction:
      return static_cast<const InvokeStaticFunction&>(instr).ret_type();
    case Opcode::kLoadArrayItem:
      return static_cast<const LoadArrayItem&>(instr).type();
    case Opcode::kLoadSplitDictItem:
      return TOptObject;
    case Opcode::kLoadField:
      return static_cast<const LoadField&>(instr).type();
    case Opcode::kLoadFieldAddress:
      return TCPtr;
    case Opcode::kCallStatic: {
      auto& call = static_cast<const CallStatic&>(instr);
      return call.ret_type();
    }
    case Opcode::kIntConvert: {
      auto& conv = static_cast<const IntConvert&>(instr);
      return conv.type();
    }
    case Opcode::kIntBinaryOp: {
      auto& binop = static_cast<const IntBinaryOp&>(instr);
      if (binop.op() == BinaryOpKind::kPower ||
          binop.op() == BinaryOpKind::kPowerUnsigned) {
        return TCDouble;
      }
      return binop.left()->type().unspecialized();
    }
    case Opcode::kDoubleBinaryOp: {
      return TCDouble;
    }
    case Opcode::kPrimitiveCompare:
      return TCBool;
    case Opcode::kPrimitiveUnaryOp:
      // TODO if we have a specialized input type we should really be
      // constant-folding
      if (static_cast<const PrimitiveUnaryOp&>(instr).op() ==
          PrimitiveUnaryOpKind::kNotInt) {
        return TCBool;
      }
      return get_op_type(0).unspecialized();

    // Some return something slightly more interesting.
    case Opcode::kBuildSlice:
      return TMortalSlice;
    case Opcode::kGetTuple:
      return TTupleExact;
    case Opcode::kInitialYield:
      return TOptNoneType;
    case Opcode::kLoadArg: {
      auto& loadarg = static_cast<const LoadArg&>(instr);
      return loadarg.type();
    }
    case Opcode::kLoadCurrentFunc:
      return TFunc;
    case Opcode::kLoadEvalBreaker:
      return TCInt32;
    case Opcode::kMakeCell:
      return TMortalCell;
    case Opcode::kMakeDict:
      return TMortalDictExact;
    case Opcode::kMakeCheckedDict: {
      auto& makechkdict = static_cast<const MakeCheckedDict&>(instr);
      return makechkdict.type();
    }
    case Opcode::kMakeCheckedList: {
      auto& makechklist = static_cast<const MakeCheckedList&>(instr);
      return makechklist.type();
    }
    case Opcode::kMakeFunction:
      return TMortalFunc;
    case Opcode::kMakeSet:
      return TMortalSetExact;
    case Opcode::kLongBinaryOp: {
      auto& binop = static_cast<const LongBinaryOp&>(instr);
      if (binop.op() == BinaryOpKind::kTrueDivide) {
        return TFloatExact;
      }
      return TLongExact;
    }
    case Opcode::kLongCompare:
    case Opcode::kUnicodeCompare:
      return TBool;
    case Opcode::kDictUpdate:
    case Opcode::kDictMerge:
    case Opcode::kRunPeriodicTasks:
      return TCInt32;

    // TODO(bsimmers): These wrap C functions that return 0 for success and -1
    // for an error, which is converted into Py_None or nullptr,
    // respectively. At some point we should get rid of this extra layer and
    // deal with the int return value directly.
    case Opcode::kListExtend:
    case Opcode::kStoreAttr:
      return TNoneType;

    case Opcode::kListAppend:
    case Opcode::kMergeSetUnpack:
    case Opcode::kSetSetItem:
    case Opcode::kSetUpdate:
    case Opcode::kSetDictItem:
    case Opcode::kStoreSubscr:
      return TCInt32;

    case Opcode::kIsNegativeAndErrOccurred:
      return TCInt64;

    // Some compute their output type from either their inputs or some other
    // source.

    // Executing LoadTypeAttrCacheItem<cache_id, 1> is only legal if
    // appropriately guarded by LoadTypeAttrCacheItem<cache_id, 0>, and the
    // former will always produce a non-null object.
    //
    // TODO(bsimmers): We should probably split this into two instructions
    // rather than changing the output type based on the item index.
    case Opcode::kLoadTypeAttrCacheItem: {
      auto item = static_cast<const LoadTypeAttrCacheItem&>(instr).item_idx();
      return item == 1 ? TObject : TOptObject;
    }
    case Opcode::kAssign:
      return get_op_type(0);
    case Opcode::kBitCast:
      return static_cast<const BitCast&>(instr).type();
    case Opcode::kLoadConst: {
      return static_cast<const LoadConst&>(instr).type();
    }
    case Opcode::kMakeList:
      return TMortalListExact;
    case Opcode::kMakeTuple:
    case Opcode::kMakeTupleFromList:
    case Opcode::kUnpackExToTuple:
      return TMortalTupleExact;
    case Opcode::kPhi: {
      auto ty = TBottom;
      for (std::size_t i = 0, n = instr.NumOperands(); i < n; ++i) {
        ty |= get_op_type(i);
      }
      return ty;
    }
    case Opcode::kCheckSequenceBounds: {
      return TCInt64;
    }

    // 1 if comparison is true, 0 if not, -1 on error
    case Opcode::kCompareBool:
    case Opcode::kIsInstance:
    // 1, 0 if the value is truthy, not truthy
    case Opcode::kIsTruthy: {
      return TCInt32;
    }

    case Opcode::kLoadFunctionIndirect: {
      return TObject;
    }

    case Opcode::kRepeatList: {
      return TListExact;
    }

    case Opcode::kRepeatTuple: {
      return TTupleExact;
    }

    case Opcode::kPrimitiveBoxBool: {
      return TBool;
    }

    case Opcode::kPrimitiveBox: {
      // This duplicates the logic in Type::asBoxed(), but it has enough
      // special cases (for exactness/optionality/nullptr) that it's not worth
      // trying to reuse it here.

      auto& pb = static_cast<const PrimitiveBox&>(instr);
      if (pb.value()->type() <= TCDouble) {
        return TFloatExact;
      }
      if (pb.value()->type() <= (TCUnsigned | TCSigned | TNullptr)) {
        // Special Nullptr case for an uninitialized variable; load zero.
        return TLongExact;
      }
      JIT_CHECK(
          false,
          "only primitive numeric types should be boxed. type verification"
          "missed an unexpected type %s",
          pb.value()->type());
    }

    case Opcode::kPrimitiveUnbox: {
      auto& unbox = static_cast<const PrimitiveUnbox&>(instr);
      return unbox.type();
    }

    // Check opcodes return a copy of their input that is statically known to
    // not be null.
    case Opcode::kCheckExc:
    case Opcode::kCheckField:
    case Opcode::kCheckFreevar:
    case Opcode::kCheckNeg:
    case Opcode::kCheckVar: {
      return get_op_type(0) - TNullptr;
    }

    case Opcode::kGuardIs: {
      auto type = Type::fromObject(static_cast<const GuardIs&>(instr).target());
      return get_op_type(0) & type;
    }

    case Opcode::kCast: {
      auto& cast = static_cast<const Cast&>(instr);
      Type to_type = Type::fromType(cast.pytype()) |
          (cast.optional() ? TNoneType : TBottom);
      return to_type;
    }

    case Opcode::kTpAlloc: {
      auto& tp_alloc = static_cast<const TpAlloc&>(instr);
      Type alloc_type = Type::fromTypeExact(tp_alloc.pytype());
      return alloc_type;
    }

    // Refine type gives us more information about the type of its input.
    case Opcode::kRefineType: {
      auto type = static_cast<const RefineType&>(instr).type();
      return get_op_type(0) & type;
    }

    case Opcode::kGuardType: {
      auto type = static_cast<const GuardType&>(instr).target();
      return get_op_type(0) & type;
    }

    case Opcode::kUnicodeConcat:
    case Opcode::kUnicodeRepeat: {
      return TUnicodeExact;
    }

      // Finally, some opcodes have no destination.
    case Opcode::kBatchDecref:
    case Opcode::kBeginInlinedFunction:
    case Opcode::kBranch:
    case Opcode::kCallStaticRetVoid:
    case Opcode::kCheckErrOccurred:
    case Opcode::kCondBranch:
    case Opcode::kCondBranchCheckType:
    case Opcode::kCondBranchIterNotDone:
    case Opcode::kDecref:
    case Opcode::kDeleteAttr:
    case Opcode::kDeleteSubscr:
    case Opcode::kDeopt:
    case Opcode::kDeoptPatchpoint:
    case Opcode::kEndInlinedFunction:
    case Opcode::kGuard:
    case Opcode::kHintType:
    case Opcode::kIncref:
    case Opcode::kInitFunction:
    case Opcode::kRaise:
    case Opcode::kRaiseAwaitableError:
    case Opcode::kRaiseStatic:
    case Opcode::kReturn:
    case Opcode::kSetCurrentAwaiter:
    case Opcode::kSetCellItem:
    case Opcode::kSetFunctionAttr:
    case Opcode::kSnapshot:
    case Opcode::kStoreArrayItem:
    case Opcode::kStoreField:
    case Opcode::kUnreachable:
    case Opcode::kUseType:
    case Opcode::kWaitHandleRelease:
    case Opcode::kXDecref:
    case Opcode::kXIncref:
      JIT_CHECK(false, "Opcode %s has no output", instr.opname());
  }
  JIT_CHECK(false, "Bad opcode %d", static_cast<int>(instr.opcode()));
}

Type outputType(const Instr& instr) {
  return outputType(
      instr, [&](std::size_t ind) { return instr.GetOperand(ind)->type(); });
}

void reflowTypes(Environment* env, BasicBlock* start) {
  // First, reset all types to Bottom so Phi inputs from back edges don't
  // contribute to the output type of the Phi until they've been processed.
  for (auto& pair : env->GetRegisters()) {
    pair.second->set_type(TBottom);
  }

  // Next, flow types forward, iterating to a fixed point.
  auto rpo_blocks = CFG::GetRPOTraversal(start);
  for (bool changed = true; changed;) {
    changed = false;
    for (auto block : rpo_blocks) {
      for (auto& instr : *block) {
        if (instr.opcode() == Opcode::kReturn) {
          Type type = static_cast<const Return&>(instr).type();
          JIT_DCHECK(
              instr.GetOperand(0)->type() <= type,
              "bad return type %s, expected %s in %s",
              instr.GetOperand(0)->type(),
              type,
              *start->cfg);
        }

        auto dst = instr.GetOutput();
        if (dst == nullptr) {
          continue;
        }

        auto new_ty = outputType(instr);
        if (new_ty == dst->type()) {
          continue;
        }

        dst->set_type(new_ty);
        changed = true;
      }
    }
  }
}

void reflowTypes(Function& func) {
  reflowTypes(&func.env, func.cfg.entry_block);
}

void SSAify::Run(Function& irfunc) {
  Run(irfunc.cfg.entry_block, &irfunc.env);
  PhiElimination{}.Run(irfunc);
}

// This implements the algorithm outlined in "Simple and Efficient Construction
// of Static Single Assignment Form"
// https://pp.info.uni-karlsruhe.de/uploads/publikationen/braun13cc.pdf
void SSAify::Run(BasicBlock* start, Environment* env) {
  env_ = env;

  auto blocks = CFG::GetRPOTraversal(start);
  auto ssa_basic_blocks = initSSABasicBlocks(blocks);
  phi_uses_.clear();

  for (auto& block : blocks) {
    auto ssablock = ssa_basic_blocks.at(block);

    for (auto& instr : *block) {
      JIT_CHECK(!instr.IsPhi(), "SSAify does not support Phis in its input");
      instr.visitUses([&](Register*& reg) {
        JIT_CHECK(
            reg != nullptr, "Instructions should not have nullptr operands.");
        reg = getDefine(ssablock, reg);
        return true;
      });

      auto out_reg = instr.GetOutput();

      if (out_reg != nullptr) {
        auto new_reg = env_->AllocateRegister();
        instr.SetOutput(new_reg);
        ssablock->local_defs[out_reg] = new_reg;
      }
    }

    for (auto& succ : ssablock->succs) {
      succ->unsealed_preds--;
      if (succ->unsealed_preds > 0) {
        continue;
      }
      fixIncompletePhis(succ);
    }
  }

  // realize phi functions
  for (auto& bb : ssa_basic_blocks) {
    auto block = bb.first;
    auto ssablock = bb.second;

    // Collect and sort to stabilize IR ordering.
    std::vector<Phi*> phis;
    for (auto& pair : ssablock->phi_nodes) {
      phis.push_back(pair.second);
    }
    std::sort(phis.begin(), phis.end(), [](const Phi* a, const Phi* b) -> bool {
      // Sort using > instead of the typical < because we're effectively
      // reversing by looping push_front below.
      return a->GetOutput()->id() > b->GetOutput()->id();
    });
    for (auto& phi : phis) {
      block->push_front(phi);
    }

    delete ssablock;
  }

  reflowTypes(env, start);
}

Register* SSAify::getDefine(SSABasicBlock* ssablock, Register* reg) {
  auto iter = ssablock->local_defs.find(reg);
  if (iter != ssablock->local_defs.end()) {
    // If defined locally, just return
    return iter->second;
  }

  if (ssablock->preds.size() == 0) {
    // If we made it back to the entry block and didn't find a definition, use
    // a Nullptr from LoadConst. Place it after the initialization of the args
    // which explicitly come first.
    if (null_reg_ == nullptr) {
      auto it = ssablock->block->begin();
      while (it != ssablock->block->end() &&
             (it->IsLoadArg() || it->IsLoadCurrentFunc())) {
        ++it;
      }
      null_reg_ = env_->AllocateRegister();
      auto loadnull = LoadConst::create(null_reg_, TNullptr);
      loadnull->copyBytecodeOffset(*it);
      loadnull->InsertBefore(*it);
    }
    ssablock->local_defs.emplace(reg, null_reg_);
    return null_reg_;
  }

  if (ssablock->unsealed_preds > 0) {
    // If we haven't visited all our predecessors, they can't provide
    // definitions for us to look up. We'll place an incomplete phi that will
    // be resolved once we've visited all predecessors.
    auto phi_output = env_->AllocateRegister();
    ssablock->incomplete_phis.emplace_back(reg, phi_output);
    ssablock->local_defs.emplace(reg, phi_output);
    return phi_output;
  }

  if (ssablock->preds.size() == 1) {
    // If we only have a single predecessor, use its value
    auto new_reg = getDefine(*ssablock->preds.begin(), reg);
    ssablock->local_defs.emplace(reg, new_reg);
    return new_reg;
  }

  // We have multiple predecessors and may need to create a phi.
  auto new_reg = env_->AllocateRegister();
  // Adding a phi may loop back to our block if there is a loop in the CFG.  We
  // update our local_defs before adding the phi to terminate the recursion
  // rather than looping infinitely.
  ssablock->local_defs.emplace(reg, new_reg);
  maybeAddPhi(ssablock, reg, new_reg);

  return ssablock->local_defs.at(reg);
}

void SSAify::maybeAddPhi(
    SSABasicBlock* ssa_block,
    Register* reg,
    Register* out) {
  std::unordered_map<BasicBlock*, Register*> pred_defs;
  for (auto& pred : ssa_block->preds) {
    auto pred_reg = getDefine(pred, reg);
    pred_defs.emplace(pred->block, pred_reg);
  }
  auto bc_off = ssa_block->block->begin()->bytecodeOffset();
  auto phi = Phi::create(out, pred_defs);
  phi->setBytecodeOffset(bc_off);
  ssa_block->phi_nodes.emplace(out, phi);
  for (auto& def_pair : pred_defs) {
    phi_uses_[def_pair.second].emplace(phi, ssa_block);
  }
}

Register* SSAify::getCommonPredValue(
    const Register* out_reg,
    const std::unordered_map<BasicBlock*, Register*>& defs) {
  Register* other_reg = nullptr;

  for (auto& def_pair : defs) {
    auto def = def_pair.second;

    if (def == out_reg) {
      continue;
    }

    if (other_reg != nullptr && def != other_reg) {
      return nullptr;
    }

    other_reg = def;
  }

  return other_reg;
}

void SSAify::fixIncompletePhis(SSABasicBlock* ssa_block) {
  for (auto& pi : ssa_block->incomplete_phis) {
    maybeAddPhi(ssa_block, pi.first, pi.second);
  }
}

std::unordered_map<BasicBlock*, SSABasicBlock*> SSAify::initSSABasicBlocks(
    std::vector<BasicBlock*>& blocks) {
  std::unordered_map<BasicBlock*, SSABasicBlock*> ssa_basic_blocks;

  auto get_or_create_ssa_block =
      [&ssa_basic_blocks](BasicBlock* block) -> SSABasicBlock* {
    auto iter = ssa_basic_blocks.find(block);
    if (iter == ssa_basic_blocks.end()) {
      auto ssablock = new SSABasicBlock(block);
      ssa_basic_blocks.emplace(block, ssablock);
      return ssablock;
    }
    return iter->second;
  };

  for (auto& block : blocks) {
    auto ssablock = get_or_create_ssa_block(block);
    for (auto& edge : block->out_edges()) {
      auto succ = edge->to();
      auto succ_ssa_block = get_or_create_ssa_block(succ);
      auto p = succ_ssa_block->preds.insert(ssablock);
      if (p.second) {
        // It's possible that we have multiple outgoing edges to the same
        // successor. Since we only care about the number of unsealed
        // predecessor *nodes*, only update if this is the first time we're
        // processing this predecessor.
        succ_ssa_block->unsealed_preds++;
        ssablock->succs.insert(succ_ssa_block);
      }
    }
  }

  return ssa_basic_blocks;
}

} // namespace jit::hir
