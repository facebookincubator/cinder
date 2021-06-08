// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/optimization.h"

#include <fmt/format.h>
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Jit/hir/analysis.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/memory_effects.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/jit_rt.h"
#include "Jit/util.h"

#include "Python.h"
#include "code.h"
#include "pycore_pystate.h"

namespace jit {
namespace hir {

PassRegistry::PassRegistry() {
  auto addPass = [&](const PassFactory& factory) {
    factories_.emplace(factory()->name(), factory);
  };
  addPass(NullCheckElimination::Factory);
  addPass(RefcountInsertion::Factory);
  addPass(CopyPropagation::Factory);
  addPass(LoadAttrSpecialization::Factory);
  addPass(CallOptimization::Factory);
  addPass(DynamicComparisonElimination::Factory);
  addPass(PhiElimination::Factory);
  addPass(RedundantConversionElimination::Factory);
  addPass(LoadConstTupleItemOptimization::Factory);
}

std::unique_ptr<Pass> PassRegistry::MakePass(const std::string& name) {
  auto it = factories_.find(name);
  if (it != factories_.end()) {
    return it->second();
  } else {
    return nullptr;
  }
}

void NullCheckElimination::Run(Function& irfunc) {
  for (auto& block : irfunc.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;
      if (!instr.IsCheckVar() && !instr.IsCheckExc() && !instr.IsCheckField()) {
        continue;
      }

      auto input = instr.GetOperand(0);
      // TODO(bsimmers): CheckExc sometimes has a CInt32 source, where a
      // negative value means an exception is set. We don't have the ability to
      // express that in Type at the moment, so we only worry about non-null
      // object inputs for now.
      if (input->type() <= TObject) {
        auto assign = Assign::create(instr.GetOutput(), input);
        assign->copyBytecodeOffset(instr);
        instr.ReplaceWith(*assign);
        delete &instr;
      }
    }
  }

  CopyPropagation{}.Run(irfunc);
}

Instr* DynamicComparisonElimination::ReplaceCompare(
    Compare* compare,
    IsTruthy* truthy) {
  // For is/is not we can use CompareInt:
  //  $truthy = CompareInt<Eq> $x $y
  //  CondBranch<x, y> $truthy
  // For other comparisons we can use ComapreBool.
  if (compare->op() == CompareOp::kIs || compare->op() == CompareOp::kIsNot) {
    return IntCompare::create(
        (compare->op() == CompareOp::kIs) ? PrimitiveCompareOp::kEqual
                                          : PrimitiveCompareOp::kNotEqual,
        truthy->GetOutput(),
        compare->GetOperand(0),
        compare->GetOperand(1));
  }

  return CompareBool::create(
      compare->op(),
      truthy->GetOutput(),
      compare->GetOperand(0),
      compare->GetOperand(1),
      *get_frame_state(*truthy));
}

void DynamicComparisonElimination::InitBuiltins() {
  if (inited_builtins_) {
    return;
  }

  inited_builtins_ = true;

  // we want to check the exact function address, rather than relying on
  // modules which can be mutated.  First find builtins, which we have
  // to do a search for because PyEval_GetBuiltins() returns the
  // module dict.
  PyObject* mods = _PyThreadState_GET()->interp->modules_by_index;
  PyModuleDef* builtins = nullptr;
  for (Py_ssize_t i = 0; i < PyList_GET_SIZE(mods); i++) {
    PyObject* cur = PyList_GET_ITEM(mods, i);
    if (cur == Py_None) {
      continue;
    }
    PyModuleDef* def = PyModule_GetDef(cur);
    if (def != nullptr && strcmp(def->m_name, "builtins") == 0) {
      builtins = def;
      break;
    }
  }

  if (builtins == nullptr) {
    return;
  }

  for (PyMethodDef* fdef = builtins->m_methods; fdef->ml_name != NULL; fdef++) {
    if (strcmp(fdef->ml_name, "isinstance") == 0) {
      isinstance_func_ = fdef->ml_meth;
      break;
    }
  }
}

Instr* DynamicComparisonElimination::ReplaceVectorCall(
    Function& irfunc,
    CondBranch& cond_branch,
    BasicBlock& block,
    VectorCall* vectorcall,
    IsTruthy* truthy) {
  auto func = vectorcall->func();

  if (!func->type().hasValueSpec(TObject)) {
    return nullptr;
  }

  InitBuiltins();

  auto funcobj = func->type().objectSpec();
  if (Py_TYPE(funcobj) == &PyCFunction_Type &&
      PyCFunction_GET_FUNCTION(funcobj) == isinstance_func_ &&
      vectorcall->numArgs() == 2 &&
      vectorcall->GetOperand(2)->type() <= TType) {
    auto obj_op = vectorcall->GetOperand(1);
    auto type_op = vectorcall->GetOperand(2);
    int bc_off = cond_branch.bytecodeOffset();

    // We want to replace:
    //  if isinstance(x, some_type):
    // with:
    //   if x.__class__ == some_type or PyObject_IsInstance(x, some_type):
    // This inlines the common type check case, and eliminates
    // the truthy case.

    // We do this by updating the existing branch to be
    // based off the fast path, and if that fails, then
    // we insert a new basic block which handles the slow path
    // and branches to the success or failure cases.

    auto obj_type = irfunc.env.AllocateRegister();
    auto fast_eq = irfunc.env.AllocateRegister();

    auto load_type =
        LoadField::create(obj_type, obj_op, offsetof(PyObject, ob_type), TType);

    auto compare_type = IntCompare::create(
        PrimitiveCompareOp::kEqual, fast_eq, obj_type, type_op);

    load_type->copyBytecodeOffset(*vectorcall);
    load_type->InsertBefore(*truthy);

    compare_type->copyBytecodeOffset(*vectorcall);

    // Slow path, call isinstance()
    auto slow_path = block.cfg->AllocateBlock();
    auto prev_false_bb = cond_branch.false_bb();
    cond_branch.set_false_bb(slow_path);
    cond_branch.SetOperand(0, fast_eq);

    slow_path->appendWithOff<IsInstance>(
        bc_off,
        truthy->GetOutput(),
        obj_op,
        type_op,
        *get_frame_state(*truthy));

    slow_path->appendWithOff<CondBranch>(
        bc_off, truthy->GetOutput(), cond_branch.true_bb(), prev_false_bb);

    // we need to update the phis from the previous false case to now
    // be coming from the slow path block.
    prev_false_bb->fixupPhis(&block, slow_path);
    // and the phis coming in on the success case now have an extra
    // block from the slow path.
    cond_branch.true_bb()->addPhiPredecessor(&block, slow_path);
    return compare_type;
  }
  return nullptr;
}

void DynamicComparisonElimination::Run(Function& irfunc) {
  LivenessAnalysis liveness{irfunc};
  liveness.Run();
  auto last_uses = liveness.GetLastUses();

  for (auto& block : irfunc.cfg.blocks) {
    auto& instr = block.back();

    // Looking for:
    //   $some_conditional = ...
    //   $truthy = IsTruthy $compare
    //   CondBranch<x, y> $truthy
    // Which we then re-write to a form which doesn't use IsTruthy anymore.
    if (!instr.IsCondBranch()) {
      continue;
    }

    Instr* truthy = instr.GetOperand(0)->instr();
    if (!truthy->IsIsTruthy() || truthy->block() != &block) {
      continue;
    }

    Instr* truthy_target = truthy->GetOperand(0)->instr();
    if (truthy_target->block() != &block ||
        (!truthy_target->IsCompare() && !truthy_target->IsVectorCall())) {
      continue;
    }

    auto& dying_regs = map_get(last_uses, truthy, kEmptyRegSet);

    if (dying_regs.count(truthy->GetOperand(0)) == 0) {
      // Compare output lives on, we can't re-write...
      continue;
    }

    // Make sure the output of compare isn't getting used between the compare
    // and the branch other than by the truthy instruction.
    std::vector<Instr*> snapshots;
    bool can_optimize = true;
    for (auto it = std::next(block.rbegin()); it != block.rend(); ++it) {
      if (&*it == truthy_target) {
        break;
      } else if (&*it != truthy) {
        if (it->IsSnapshot()) {
          if (it->Uses(truthy_target->GetOutput())) {
            snapshots.push_back(&*it);
          }
          continue;
        } else if (!it->isReplayable()) {
          can_optimize = false;
          break;
        }

        if (it->Uses(truthy->GetOperand(0))) {
          can_optimize = false;
          break;
        }
      }
    }
    if (!can_optimize) {
      continue;
    }

    Instr* replacement = nullptr;
    if (truthy_target->IsCompare()) {
      auto compare = static_cast<Compare*>(truthy_target);

      replacement = ReplaceCompare(compare, static_cast<IsTruthy*>(truthy));
    } else if (truthy_target->IsVectorCall()) {
      auto vectorcall = static_cast<VectorCall*>(truthy_target);
      replacement = ReplaceVectorCall(
          irfunc,
          static_cast<CondBranch&>(instr),
          block,
          vectorcall,
          static_cast<IsTruthy*>(truthy));
    }

    if (replacement != nullptr) {
      replacement->copyBytecodeOffset(instr);
      truthy->ReplaceWith(*replacement);

      truthy_target->unlink();
      delete truthy_target;
      delete truthy;

      // There may be zero or more Snapshots between the Compare and the
      // IsTruthy that uses the output of the Compare (which we want to delete).
      // Since we're fusing the two operations together, the Snapshot and
      // its use of the dead intermediate value should be deleted.
      for (auto snapshot : snapshots) {
        snapshot->unlink();
        delete snapshot;
      }
    }
  }

  reflowTypes(irfunc);
}

void CallOptimization::Run(Function& irfunc) {
  std::vector<Instr*> cond_branches;

  for (auto& block : irfunc.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;

      if (instr.IsVectorCall()) {
        auto target = instr.GetOperand(0);
        if (target->type() == type_type_ && instr.NumOperands() == 2) {
          auto load_type = LoadField::create(
              instr.GetOutput(),
              instr.GetOperand(1),
              offsetof(PyObject, ob_type),
              TType);
          instr.ReplaceWith(*load_type);

          delete &instr;
        }
      }
    }
  }
}

void CopyPropagation::Run(Function& irfunc) {
  std::vector<Instr*> assigns;
  for (auto block : irfunc.cfg.GetRPOTraversal()) {
    for (auto& instr : *block) {
      instr.visitUses([](Register*& reg) {
        while (reg->instr()->IsAssign()) {
          reg = reg->instr()->GetOperand(0);
        }
        return true;
      });

      if (instr.IsAssign()) {
        assigns.emplace_back(&instr);
      }
    }
  }

  for (auto instr : assigns) {
    instr->unlink();
    delete instr;
  }
}

void PhiElimination::Run(Function& func) {
  for (bool changed = true; changed;) {
    changed = false;

    for (auto& block : func.cfg.blocks) {
      std::vector<Instr*> assigns;
      for (auto it = block.begin(); it != block.end();) {
        auto& instr = *it;
        ++it;
        if (!instr.IsPhi()) {
          for (auto assign : assigns) {
            assign->InsertBefore(instr);
          }
          break;
        }
        if (auto value = static_cast<Phi&>(instr).isTrivial()) {
          auto assign = Assign::create(instr.GetOutput(), value);
          assign->copyBytecodeOffset(instr);
          assigns.emplace_back(assign);
          instr.unlink();
          delete &instr;
          changed = true;
        }
      }
    }

    CopyPropagation{}.Run(func);
  }

  func.cfg.RemoveTrampolineBlocks();
}

void RedundantConversionElimination::Run(Function& func) {
  for (auto& block : func.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;
      if (instr.IsIntConvert()) {
        auto convert = static_cast<const IntConvert*>(&instr);
        Register* input = convert->src();
        if (input->type() <= convert->type()) {
          auto assign = Assign::create(instr.GetOutput(), input);
          assign->copyBytecodeOffset(instr);
          instr.ReplaceWith(*assign);
          delete &instr;
        }
      }
    }
  }

  CopyPropagation{}.Run(func);
}

void LoadConstTupleItemOptimization::Run(Function& func) {
  for (auto& block : func.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;

      if (!instr.IsLoadTupleItem()) {
        continue;
      }

      auto load_tuple_item = static_cast<const LoadTupleItem*>(&instr);
      Register* tuple = load_tuple_item->tuple();
      Instr* def_instr = tuple->instr();

      if (!def_instr->IsLoadConst()) {
        continue;
      }

      auto load_const = static_cast<const LoadConst*>(def_instr);

      if (!load_const->type().hasValueSpec(TTuple)) {
        continue;
      }
      auto const_tuple = load_const->type().objectSpec();

      auto new_load_const = LoadConst::create(
          load_tuple_item->GetOutput(),
          Type::fromObject(
              PyTuple_GET_ITEM(const_tuple, load_tuple_item->idx())));

      instr.ReplaceWith(*new_load_const);
      delete load_tuple_item;
    }
  }
}

PyObject* loadGlobal(
    PyObject* globals,
    PyObject* builtins,
    PyObject* names,
    int name_idx) {
  PyObject* name = PyTuple_GetItem(names, name_idx);
  PyObject* result = PyDict_GetItem(globals, name);
  if (result == nullptr && builtins != nullptr) {
    result = PyDict_GetItem(builtins, name);
  }
  return result;
}

void LoadAttrSpecialization::Run(Function& irfunc) {
  std::vector<LoadAttr*> to_specialize;
  for (auto& block : irfunc.cfg.GetRPOTraversal()) {
    for (auto& instr : *block) {
      if (!instr.IsLoadAttr()) {
        continue;
      }

      if (instr.GetOperand(0)->type() <= TType) {
        to_specialize.emplace_back(static_cast<LoadAttr*>(&instr));
      }
    }
  }

  if (to_specialize.empty()) {
    return;
  }

  for (auto load_attr : to_specialize) {
    specializeForType(irfunc.env, load_attr);
  }

  reflowTypes(irfunc);
}

BasicBlock* LoadAttrSpecialization::specializeForType(
    Environment& env,
    LoadAttr* load_attr) {
  auto block = load_attr->block();
  Register* dst = load_attr->GetOutput();
  BasicBlock* tail = block->splitAfter(*load_attr);
  Register* receiver = load_attr->receiver();
  int bc_off = load_attr->bytecodeOffset();
  int name_idx = load_attr->name_idx();
  load_attr->unlink();

  // Fast path
  int cache_id = cache_id_++;
  auto fast_path = block->cfg->AllocateBlock();
  auto cached_item = env.AllocateRegister();
  fast_path->appendWithOff<LoadTypeAttrCacheItem>(
      bc_off, cached_item, cache_id, 1);
  fast_path->appendWithOff<Branch>(bc_off, tail);

  // Slow path
  auto slow_path = block->cfg->AllocateBlock();
  auto filled_item = env.AllocateRegister();
  slow_path->appendWithOff<FillTypeAttrCache>(
      bc_off,
      filled_item,
      receiver,
      name_idx,
      cache_id,
      load_attr->takeFrameState());
  delete load_attr;
  slow_path->appendWithOff<Branch>(bc_off, tail);

  // Join fast/slow paths at the beginning of tail.
  std::unordered_map<BasicBlock*, Register*> phi_vals{
      {fast_path, cached_item}, {slow_path, filled_item}};
  auto phi = tail->push_front<Phi>(dst, phi_vals);
  phi->setBytecodeOffset(bc_off);

  // Split control flow
  auto guard = env.AllocateRegister();
  block->appendWithOff<LoadTypeAttrCacheItem>(bc_off, guard, cache_id, 0);
  auto cond1 = env.AllocateRegister();
  block->appendWithOff<Compare>(
      bc_off, CompareOp::kIs, cond1, guard, receiver, FrameState{});
  auto cond2 = env.AllocateRegister();
  // TODO(bsimmers): Get rid of this once we have a type checker for HIR.
  cond2->set_type(TCInt32);
  block->appendWithOff<IsTruthy>(bc_off, cond2, cond1, FrameState{});
  block->appendWithOff<CondBranch>(bc_off, cond2, fast_path, slow_path);

  // Remove the tail if it ends up being a trampoline
  if (tail->IsTrampoline()) {
    auto old_tail = tail;
    tail = tail->GetTerminator()->successor(0);
    fast_path->GetTerminator()->set_successor(0, tail);
    slow_path->GetTerminator()->set_successor(0, tail);
    tail->cfg->RemoveBlock(old_tail);
    delete old_tail;
  }

  return tail;
}

} // namespace hir
} // namespace jit
