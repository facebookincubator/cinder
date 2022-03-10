// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/optimization.h"

#include "Python.h"
#include "code.h"
#include "pycore_pystate.h"

#include "Jit/compiler.h"
#include "Jit/hir/analysis.h"
#include "Jit/hir/builder.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/memory_effects.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/jit_rt.h"
#include "Jit/pyjit.h"
#include "Jit/util.h"

#include <fmt/format.h>

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jit {
namespace hir {

PassRegistry::PassRegistry() {
  auto addPass = [&](const PassFactory& factory) {
    factories_.emplace(factory()->name(), factory);
  };
  addPass(RefcountInsertion::Factory);
  addPass(CopyPropagation::Factory);
  addPass(CallOptimization::Factory);
  addPass(CleanCFG::Factory);
  addPass(DynamicComparisonElimination::Factory);
  addPass(PhiElimination::Factory);
  addPass(InlineFunctionCalls::Factory);
  addPass(Simplify::Factory);
  addPass(DeadCodeElimination::Factory);
  addPass(GuardTypeRemoval::Factory);
}

std::unique_ptr<Pass> PassRegistry::MakePass(const std::string& name) {
  auto it = factories_.find(name);
  if (it != factories_.end()) {
    return it->second();
  } else {
    return nullptr;
  }
}

Instr* DynamicComparisonElimination::ReplaceCompare(
    Compare* compare,
    IsTruthy* truthy) {
  // For is/is not we can use CompareInt:
  //  $truthy = CompareInt<Eq> $x $y
  //  CondBranch<x, y> $truthy
  // For other comparisons we can use ComapreBool.
  if (compare->op() == CompareOp::kIs || compare->op() == CompareOp::kIsNot) {
    return PrimitiveCompare::create(
        truthy->GetOutput(),
        (compare->op() == CompareOp::kIs) ? PrimitiveCompareOp::kEqual
                                          : PrimitiveCompareOp::kNotEqual,
        compare->GetOperand(0),
        compare->GetOperand(1));
  }

  return CompareBool::create(
      truthy->GetOutput(),
      compare->op(),
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

    auto compare_type = PrimitiveCompare::create(
        fast_eq, PrimitiveCompareOp::kEqual, obj_type, type_op);

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

  // Optimize "if x is y" case
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

  // Optimize the more general case of "x is y" used outside "if"
  for (auto& block : irfunc.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;

      if (!instr.IsCompare()) {
        continue;
      }
      auto compare = static_cast<Compare*>(&instr);
      if (compare->op() != CompareOp::kIs &&
          compare->op() != CompareOp::kIsNot) {
        continue;
      }
      auto cbool = irfunc.env.AllocateRegister();
      auto primitive_compare = PrimitiveCompare::create(
          cbool,
          compare->op() == CompareOp::kIs ? PrimitiveCompareOp::kEqual
                                          : PrimitiveCompareOp::kNotEqual,
          compare->left(),
          compare->right());
      auto box = PrimitiveBox::create(compare->dst(), cbool, TCBool);
      primitive_compare->copyBytecodeOffset(instr);
      box->copyBytecodeOffset(instr);
      compare->ExpandInto({primitive_compare, box});
      delete compare;
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

  // TODO(emacs): Investigate running the whole CleanCFG pass here or between
  // every pass.
  CleanCFG::RemoveTrampolineBlocks(&func.cfg);
}

static bool isUseful(Instr& instr) {
  return instr.IsTerminator() || instr.IsSnapshot() ||
      dynamic_cast<const DeoptBase*>(&instr) != nullptr ||
      (!instr.IsPhi() && memoryEffects(instr).may_store != AEmpty);
}

void DeadCodeElimination::Run(Function& func) {
  Worklist<Instr*> worklist;
  for (auto& block : func.cfg.blocks) {
    for (Instr& instr : block) {
      if (isUseful(instr)) {
        worklist.push(&instr);
      }
    }
  }
  std::unordered_set<Instr*> live_set;
  while (!worklist.empty()) {
    auto live_op = worklist.front();
    worklist.pop();
    if (live_set.insert(live_op).second) {
      live_op->visitUses([&](Register*& reg) {
        if (live_set.count(reg->instr()) == 0) {
          worklist.push(reg->instr());
        }
        return true;
      });
    }
  }
  for (auto& block : func.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;
      if (live_set.count(&instr) == 0) {
        instr.unlink();
        delete &instr;
      }
    }
  }
}

using RegUses = std::unordered_map<Register*, std::unordered_set<Instr*>>;

static bool
guardNeeded(const RegUses& uses, Register* new_reg, Type relaxed_type) {
  auto it = uses.find(new_reg);
  if (it == uses.end()) {
    // No uses; the guard is dead.
    return false;
  }
  // Stores all Register->Type pairs to consider as the algorithm examines
  // whether a guard is needed across passthrough + Phi instructions
  std::queue<std::pair<Register*, Type>> worklist;
  std::unordered_map<Register*, std::unordered_set<Type>> seen_state;
  worklist.emplace(new_reg, relaxed_type);
  seen_state[new_reg].insert(relaxed_type);
  while (!worklist.empty()) {
    std::pair<Register*, Type> args = worklist.front();
    worklist.pop();
    new_reg = args.first;
    relaxed_type = args.second;
    for (const Instr* instr : map_get(uses, new_reg)) {
      for (std::size_t i = 0; i < instr->NumOperands(); i++) {
        if (instr->GetOperand(i) == new_reg) {
          if ((instr->GetOutput() != nullptr) &&
              (instr->IsPhi() || isPassthrough(*instr))) {
            Register* passthrough_output = instr->GetOutput();
            Type passthrough_type = outputType(*instr, [&](std::size_t ind) {
              if (ind == i) {
                return relaxed_type;
              }
              return instr->GetOperand(ind)->type();
            });
            if (seen_state[passthrough_output]
                    .insert(passthrough_type)
                    .second) {
              worklist.emplace(passthrough_output, passthrough_type);
            }
          }
          OperandType expected_type = instr->GetOperandType(i);
          // TODO(T106726658): We should be able to remove GuardTypes if we ever
          // add a matching constraint for non-Primitive types, and our
          // GuardType adds an unnecessary refinement. Since we cannot guard on
          // primitive types yet, this should never happen
          if (operandsMustMatch(expected_type)) {
            JIT_DLOG(
                "'%s' kept alive by primitive '%s'", *new_reg->instr(), *instr);
            return true;
          }
          if (!registerTypeMatches(relaxed_type, expected_type)) {
            JIT_DLOG("'%s' kept alive by '%s'", *new_reg->instr(), *instr);
            return true;
          }
        }
      }
    }
  }
  return false;
}

// Collect direct operand uses of all Registers in the given func, excluding
// uses in FrameState or other metadata.
static RegUses collectDirectRegUses(Function& func) {
  RegUses uses;
  for (auto& block : func.cfg.blocks) {
    for (Instr& instr : block) {
      for (size_t i = 0; i < instr.NumOperands(); ++i) {
        uses[instr.GetOperand(i)].insert(&instr);
      }
    }
  }
  return uses;
}

void GuardTypeRemoval::Run(Function& func) {
  RegUses reg_uses = collectDirectRegUses(func);
  std::vector<std::unique_ptr<Instr>> removed_guards;
  for (auto& block : func.cfg.blocks) {
    for (auto it = block.begin(); it != block.end();) {
      auto& instr = *it;
      ++it;

      if (!instr.IsGuardType()) {
        continue;
      }

      Register* guard_out = instr.GetOutput();
      Register* guard_in = instr.GetOperand(0);
      if (!guardNeeded(reg_uses, guard_out, guard_in->type())) {
        auto assign = Assign::create(guard_out, guard_in);
        assign->copyBytecodeOffset(instr);
        instr.ReplaceWith(*assign);
        removed_guards.emplace_back(&instr);
      }
    }
  }

  CopyPropagation{}.Run(func);
  reflowTypes(func);
}

static bool absorbDstBlock(BasicBlock* block) {
  auto branch = dynamic_cast<Branch*>(block->GetTerminator());
  if (!branch) {
    return false;
  }
  BasicBlock* target = branch->target();
  if (target == block) {
    return false;
  }
  if (target->in_edges().size() != 1) {
    return false;
  }
  if (target == block) {
    return false;
  }
  branch->unlink();
  while (!target->empty()) {
    Instr* instr = target->pop_front();
    JIT_CHECK(!instr->IsPhi(), "Expected no Phi but found %s", *instr);
    block->Append(instr);
  }
  // The successors to target might have Phis that still refer to target.
  // Retarget them to refer to block.
  Instr* old_term = block->GetTerminator();
  JIT_CHECK(old_term != nullptr, "block must have a terminator");
  for (std::size_t i = 0, n = old_term->numEdges(); i < n; ++i) {
    old_term->successor(i)->fixupPhis(
        /*old_pred=*/target, /*new_pred=*/block);
  }
  // Target block becomes unreachable and gets picked up by
  // RemoveUnreachableBlocks.
  delete branch;
  return true;
}

bool CleanCFG::RemoveUnreachableBlocks(CFG* cfg) {
  std::unordered_set<BasicBlock*> visited;
  std::vector<BasicBlock*> stack;
  stack.emplace_back(cfg->entry_block);
  while (!stack.empty()) {
    BasicBlock* block = stack.back();
    stack.pop_back();
    if (visited.count(block)) {
      continue;
    }
    visited.insert(block);
    auto term = block->GetTerminator();
    for (std::size_t i = 0, n = term->numEdges(); i < n; ++i) {
      BasicBlock* succ = term->successor(i);
      // This check isn't necessary for correctness but avoids unnecessary
      // pushes to the stack.
      if (!visited.count(succ)) {
        stack.emplace_back(succ);
      }
    }
  }

  std::vector<BasicBlock*> unreachable;
  for (auto it = cfg->blocks.begin(); it != cfg->blocks.end();) {
    BasicBlock* block = &*it;
    ++it;
    if (!visited.count(block)) {
      if (Instr* old_term = block->GetTerminator()) {
        for (std::size_t i = 0, n = old_term->numEdges(); i < n; ++i) {
          old_term->successor(i)->removePhiPredecessor(block);
        }
      }
      cfg->RemoveBlock(block);
      block->clear();
      unreachable.emplace_back(block);
    }
  }

  for (BasicBlock* block : unreachable) {
    delete block;
  }

  return unreachable.size() > 0;
}

// Replace cond branches where both sides of branch go to the same block with a
// direct branch
// TODO(emacs): Move to Simplify
static void simplifyRedundantCondBranches(CFG* cfg) {
  std::vector<BasicBlock*> to_simplify;
  for (auto& block : cfg->blocks) {
    if (block.empty()) {
      continue;
    }
    auto term = block.GetTerminator();
    std::size_t num_edges = term->numEdges();
    if (num_edges < 2) {
      continue;
    }
    JIT_CHECK(num_edges == 2, "only two edges are supported");
    if (term->successor(0) != term->successor(1)) {
      continue;
    }
    switch (term->opcode()) {
      case Opcode::kCondBranch:
      case Opcode::kCondBranchIterNotDone:
      case Opcode::kCondBranchCheckType:
        break;
      default:
        // Can't be sure that it's safe to replace the instruction with a branch
        JIT_CHECK(
            false, "unknown side effects of %s instruction", term->opname());
        break;
    }
    to_simplify.emplace_back(&block);
  }
  for (auto& block : to_simplify) {
    auto term = block->GetTerminator();
    term->unlink();
    auto branch = block->append<Branch>(term->successor(0));
    branch->copyBytecodeOffset(*term);
    delete term;
  }
}

bool CleanCFG::RemoveTrampolineBlocks(CFG* cfg) {
  std::vector<BasicBlock*> trampolines;
  for (auto& block : cfg->blocks) {
    if (!block.IsTrampoline()) {
      continue;
    }
    BasicBlock* succ = block.successor(0);
    // if this is the entry block and its successor has multiple
    // predecessors, don't remove it; it's necessary to maintain isolated
    // entries
    if (&block == cfg->entry_block) {
      if (succ->in_edges().size() > 1) {
        continue;
      } else {
        cfg->entry_block = succ;
      }
    }
    // Update all predecessors to jump directly to our successor
    block.retargetPreds(succ);
    // Finish splicing the trampoline out of the cfg
    block.set_successor(0, nullptr);
    trampolines.emplace_back(&block);
  }
  for (auto& block : trampolines) {
    cfg->RemoveBlock(block);
    delete block;
  }
  simplifyRedundantCondBranches(cfg);
  return trampolines.size() > 0;
}

void CleanCFG::Run(Function& irfunc) {
  bool changed = false;
  do {
    // Remove any trivial Phis; absorbDstBlock cannot handle them.
    PhiElimination{}.Run(irfunc);
    std::vector<BasicBlock*> blocks = irfunc.cfg.GetRPOTraversal();
    for (auto block : blocks) {
      // Ignore transient empty blocks.
      if (block->empty()) {
        continue;
      }
      // Keep working on the current block until no further changes are made.
      for (;; changed = true) {
        if (absorbDstBlock(block)) {
          continue;
        }
        break;
      }
    }
  } while (RemoveUnreachableBlocks(&irfunc.cfg));

  if (changed) {
    reflowTypes(irfunc);
  }
}

// Most of these checks are only temporary and do not in perpetuity prohibit
// inlining. They are here to simplify bringup of the inliner and can be
// treated as TODOs.
static bool canInline(
    VectorCall* call_instr,
    PyFunctionObject* func,
    const std::string& fullname) {
  if (func->func_defaults != nullptr) {
    JIT_DLOG("Can't inline %s because it has defaults", fullname);
    return false;
  }
  if (func->func_kwdefaults != nullptr) {
    JIT_DLOG("Can't inline %s because it has kwdefaults", fullname);
    return false;
  }
  PyCodeObject* code = reinterpret_cast<PyCodeObject*>(func->func_code);
  if (code->co_kwonlyargcount > 0) {
    JIT_DLOG("Can't inline %s because it has keyword-only args", fullname);
    return false;
  }
  if (code->co_flags & CO_VARARGS) {
    JIT_DLOG("Can't inline %s because it has varargs", fullname);
    return false;
  }
  if (code->co_flags & CO_VARKEYWORDS) {
    JIT_DLOG("Can't inline %s because it has varkwargs", fullname);
    return false;
  }
  JIT_DCHECK(code->co_argcount >= 0, "argcount must be positive");
  if (call_instr->numArgs() != static_cast<size_t>(code->co_argcount)) {
    JIT_DLOG(
        "Can't inline %s because it is called with mismatched arguments",
        fullname);
    return false;
  }
  if (code->co_flags & kCoFlagsAnyGenerator) {
    JIT_DLOG("Can't inline %s because it is a generator", fullname);
    return false;
  }
  Py_ssize_t ncellvars = PyTuple_GET_SIZE(code->co_cellvars);
  if (ncellvars > 0) {
    JIT_DLOG("Can't inline %s because it is has cellvars", fullname);
    return false;
  }
  Py_ssize_t nfreevars = PyTuple_GET_SIZE(code->co_freevars);
  if (nfreevars > 0) {
    JIT_DLOG("Can't inline %s because it is has freevars", fullname);
    return false;
  }
  if (usesRuntimeFunc(code)) {
    JIT_DLOG(
        "Can't inline %s because it needs runtime access to its "
        "PyFunctionObject",
        fullname);
    return false;
  }
  if (g_threaded_compile_context.compileRunning() && !isPreloaded(func)) {
    JIT_DLOG(
        "Can't inline %s because multithreaded compile is enabled and the "
        "function is not preloaded",
        fullname);
    return false;
  }
  return true;
}

void inlineFunctionCall(Function& caller, VectorCall* call_instr) {
  Register* target = call_instr->func();
  if (!target->type().hasValueSpec(TFunc)) {
    JIT_DLOG(
        "Cannot inline non-function type %s (%s) into %s",
        target->type(),
        *target,
        caller.fullname);
    return;
  }
  PyObject* func_obj = target->type().objectSpec();
  JIT_CHECK(PyFunction_Check(func_obj), "Expected PyFunctionObject");
  PyFunctionObject* func = reinterpret_cast<PyFunctionObject*>(func_obj);
  PyCodeObject* code = reinterpret_cast<PyCodeObject*>(func->func_code);
  JIT_CHECK(PyCode_Check(code), "Expected PyCodeObject");
  PyObject* globals = func->func_globals;
  std::string fullname = funcFullname(func);
  if (!PyDict_Check(globals)) {
    JIT_DLOG(
        "Refusing to inline %s: globals is a %.200s, not a dict",
        fullname,
        Py_TYPE(globals)->tp_name);
    return;
  }
  PyObject* builtins = PyEval_GetBuiltins();
  if (!PyDict_CheckExact(builtins)) {
    JIT_DLOG(
        "Refusing to inline %s: builtins is a %.200s, not a dict",
        fullname,
        Py_TYPE(builtins)->tp_name);
    return;
  }
  if (!canInline(call_instr, func, fullname)) {
    JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
    return;
  }

  auto caller_frame_state =
      std::make_unique<FrameState>(*call_instr->frameState());
  // Multi-threaded compilation must use an existing Preloader, whereas
  // single-threaded compilation can make Preloaders on the fly.
  InlineResult result;
  if (g_threaded_compile_context.compileRunning()) {
    HIRBuilder hir_builder(getPreloader(func));
    result = hir_builder.inlineHIR(&caller, caller_frame_state.get());
  } else {
    // This explicit temporary is necessary because HIRBuilder takes a const
    // reference and stores it and we need to make sure the target doesn't go
    // away.
    Preloader preloader(func);
    HIRBuilder hir_builder(preloader);
    result = hir_builder.inlineHIR(&caller, caller_frame_state.get());
  }
  if (result.entry == nullptr) {
    JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
    return;
  }
  auto begin_inlined_function = BeginInlinedFunction::create(
      code, globals, std::move(caller_frame_state), fullname);

  BasicBlock* head = call_instr->block();
  BasicBlock* tail = head->splitAfter(*call_instr);
  // TODO(emacs): Emit a DeoptPatchpoint here to catch the case where someone
  // swaps out function.__code__.
  // VectorCall -> {BeginInlinedFunction, Branch to callee CFG}
  auto callee_branch = Branch::create(result.entry);
  call_instr->ExpandInto({begin_inlined_function, callee_branch});
  tail->push_front(
      EndInlinedFunction::create(begin_inlined_function->inlineDepth()));

  // Transform LoadArg into Assign
  for (auto it = result.entry->begin(); it != result.entry->end();) {
    auto& instr = *it;
    ++it;

    if (instr.IsLoadArg()) {
      auto load_arg = static_cast<LoadArg*>(&instr);
      auto assign = Assign::create(
          instr.GetOutput(), call_instr->arg(load_arg->arg_idx()));
      instr.ReplaceWith(*assign);
      delete &instr;
    }
  }

  // Transform Return into Assign+Branch
  auto return_instr = result.exit->GetTerminator();
  JIT_CHECK(
      return_instr->IsReturn(),
      "terminator from inlined function should be Return");
  auto assign =
      Assign::create(call_instr->GetOutput(), return_instr->GetOperand(0));
  auto return_branch = Branch::create(tail);
  return_instr->ExpandInto({assign, return_branch});
  delete return_instr;

  delete call_instr;
}

void InlineFunctionCalls::Run(Function& irfunc) {
  if (irfunc.code == nullptr) {
    // In tests, irfunc may not have bytecode.
    return;
  }
  if (irfunc.code->co_flags & kCoFlagsAnyGenerator) {
    // TODO(T109706798): Support inlining into generators
    JIT_DLOG(
        "Refusing to inline functions into %s: function is a generator",
        irfunc.fullname);
    return;
  }
  std::vector<Instr*> to_inline;
  for (auto& block : irfunc.cfg.blocks) {
    for (auto& instr : block) {
      // TODO(emacs): Support InvokeMethod, InvokeStaticFunction,
      // VectorCallStatic
      if (!instr.IsVectorCall()) {
        continue;
      }
      to_inline.emplace_back(&instr);
    }
  }
  if (to_inline.empty()) {
    return;
  }
  for (auto instr : to_inline) {
    inlineFunctionCall(irfunc, static_cast<VectorCall*>(instr));
    // We need to reflow types after every inline to propagate new type
    // information from the callee.
    reflowTypes(irfunc);
  }
  // The inliner will make some blocks unreachable and we need to remove them
  // to make the CFG valid again. While inlining might make some blocks
  // unreachable and therefore make less work (less to inline), we cannot
  // remove unreachable blocks in the above loop. It might delete instructions
  // pointed to by `to_inline`.
  CopyPropagation{}.Run(irfunc);
  CleanCFG{}.Run(irfunc);
}

} // namespace hir
} // namespace jit
