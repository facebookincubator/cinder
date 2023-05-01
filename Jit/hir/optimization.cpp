// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/optimization.h"

#include "Python.h"
#include "code.h"
#include "internal/pycore_interp.h"

#include "Jit/compiler.h"
#include "Jit/containers.h"
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

namespace jit::hir {

class AllPasses : public Pass {
 public:
  AllPasses() : Pass("@AllPasses") {}

  void Run(Function& irfunc) override {
    Compiler::runPasses(irfunc, PassConfig::kEnableHIRInliner);
  }

  static std::unique_ptr<AllPasses> Factory() {
    return std::make_unique<AllPasses>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AllPasses);
};

PassRegistry::PassRegistry() {
  auto addPass = [&](const PassFactory& factory) {
    factories_.emplace(factory()->name(), factory);
  };
  addPass(RefcountInsertion::Factory);
  addPass(CopyPropagation::Factory);
  addPass(CleanCFG::Factory);
  addPass(DynamicComparisonElimination::Factory);
  addPass(PhiElimination::Factory);
  addPass(InlineFunctionCalls::Factory);
  addPass(Simplify::Factory);
  addPass(DeadCodeElimination::Factory);
  addPass(GuardTypeRemoval::Factory);
  addPass(BeginInlinedFunctionElimination::Factory);
  addPass(BuiltinLoadMethodElimination::Factory);
  // AllPasses is only used for testing.
  addPass(AllPasses::Factory);
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

  const Builtins& builtins = Runtime::get()->builtins();
  auto funcobj = func->type().objectSpec();
  if (Py_TYPE(funcobj) == &PyCFunction_Type &&
      builtins.find(((PyCFunctionObject*)funcobj)->m_ml) == "isinstance" &&
      vectorcall->numArgs() == 2 &&
      vectorcall->GetOperand(2)->type() <= TType) {
    auto obj_op = vectorcall->GetOperand(1);
    auto type_op = vectorcall->GetOperand(2);
    BCOffset bc_off = cond_branch.bytecodeOffset();

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

    auto load_type = LoadField::create(
        obj_type, obj_op, "ob_type", offsetof(PyObject, ob_type), TType);

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

  reflowTypes(irfunc);
}

static Register* chaseAssignOperand(Register* value) {
  while (value->instr()->IsAssign()) {
    value = value->instr()->GetOperand(0);
  }
  return value;
}

void CopyPropagation::Run(Function& irfunc) {
  std::vector<Instr*> assigns;
  for (auto block : irfunc.cfg.GetRPOTraversal()) {
    for (auto& instr : *block) {
      instr.visitUses([](Register*& reg) {
        reg = chaseAssignOperand(reg);
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
      std::vector<Instr*> assigns_or_loads;
      for (auto it = block.begin(); it != block.end();) {
        auto& instr = *it;
        ++it;
        if (!instr.IsPhi()) {
          for (auto assign : assigns_or_loads) {
            assign->InsertBefore(instr);
          }
          break;
        }
        if (auto value = static_cast<Phi&>(instr).isTrivial()) {
          // If a trivial Phi references itself then it can never be
          // initialized, and we can use a LoadConst<Bottom> to signify that.
          Register* model_value = chaseAssignOperand(value);
          Instr* new_instr;
          if (model_value == instr.GetOutput()) {
            new_instr = LoadConst::create(instr.GetOutput(), TBottom);
          } else {
            new_instr = Assign::create(instr.GetOutput(), value);
          }
          new_instr->copyBytecodeOffset(instr);
          assigns_or_loads.emplace_back(new_instr);
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
      (instr.asDeoptBase() != nullptr && !instr.IsPrimitiveBox()) ||
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
    auto new_reg_uses = uses.find(new_reg);
    if (new_reg_uses == uses.end()) {
      continue;
    }
    for (const Instr* instr : new_reg_uses->second) {
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
  if (block->GetTerminator()->opcode() != Opcode::kBranch) {
    return false;
  }
  auto branch = dynamic_cast<Branch*>(block->GetTerminator());
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

bool CleanCFG::RemoveUnreachableInstructions(CFG* cfg) {
  bool modified = false;
  std::vector<BasicBlock*> blocks = cfg->GetPostOrderTraversal();
  DominatorAnalysis dom(*cfg->func);
  RegUses reg_uses = collectDirectRegUses(*cfg->func);

  for (BasicBlock* block : blocks) {
    auto it = block->begin();
    while (it != block->end()) {
      Instr& instr = *it;
      ++it;
      if ((instr.GetOutput() == nullptr || !instr.GetOutput()->isA(TBottom)) &&
          !instr.IsUnreachable()) {
        continue;
      }
      // 1) Any instruction dominated by a definition of a Bottom value is
      // unreachable, so we delete any such instructions and replace them
      // with a special marker instruction (Unreachable)
      // 2) Any instruction post dominated by Unreachable must deopt if it can
      // deopt, else it is unreachable itself.

      modified = true;
      // Find the last instruction between [block.begin, current instruction]
      // that can deopt. Place the Unreachable marker right after that
      // instruction. If we can't find any instruction that can deopt, the
      // Unreachable marker is placed at the beginning of the block.
      do {
        auto prev_it = std::prev(it);
        Instr& prev_instr = *prev_it;
        if (prev_instr.asDeoptBase() != nullptr) {
          break;
        }
        it = prev_it;
      } while (it != block->begin());

      if (it != block->begin() && std::prev(it)->IsGuardType()) {
        // Everything after this GuardType is unreachable, but only as long as
        // the GuardType fails at runtime. Indicate that the guard is required
        // for correctness with a UseType. This prevents GuardTypeElimination
        // from removing it.
        Instr& guard_type = *std::prev(it);
        block->insert(
            UseType::create(
                guard_type.GetOutput(), guard_type.GetOutput()->type()),
            it);
      }

      block->insert(Unreachable::create(), it);
      // Clean up dangling phi references
      if (Instr* old_term = block->GetTerminator()) {
        for (std::size_t i = 0, n = old_term->numEdges(); i < n; ++i) {
          old_term->successor(i)->removePhiPredecessor(block);
        }
      }
      // Remove all instructions after the Unreachable
      while (it != block->end()) {
        Instr& instr = *it;
        ++it;
        instr.unlink();
        delete &instr;
      }
    }
    if (block->begin()->IsUnreachable()) {
      std::vector<Instr*> interesting_branches;
      // If one edge of a conditional branch leads to an Unreachable, it can be
      // replaced with a Branch to the other target. If a Branch leads to an
      // Unreachable, it is replaced with an Unreachable.
      for (const Edge* edge : block->in_edges()) {
        BasicBlock* predecessor = edge->from();
        interesting_branches.emplace_back(predecessor->GetTerminator());
      }
      for (Instr* branch : interesting_branches) {
        if (branch->IsBranch()) {
          branch->ReplaceWith(*Unreachable::create());
        } else if (auto cond_branch = dynamic_cast<CondBranchBase*>(branch)) {
          BasicBlock* target;
          if (cond_branch->false_bb() == block) {
            target = cond_branch->true_bb();
          } else {
            JIT_CHECK(
                cond_branch->true_bb() == block,
                "true branch must be unreachable");
            target = cond_branch->false_bb();
          }

          if (branch->IsCondBranchCheckType()) {
            // Before replacing a CondBranchCheckType with a Branch to the
            // reachable block, insert a RefineType to preserve the type
            // information implied by following that path.
            auto check_type_branch = static_cast<CondBranchCheckType*>(branch);
            Register* refined_value = cfg->func->env.AllocateRegister();
            Type check_type = check_type_branch->type();
            if (target == cond_branch->false_bb()) {
              check_type = TTop - check_type_branch->type();
            }

            Register* operand = check_type_branch->GetOperand(0);
            RefineType::create(refined_value, check_type, operand)
                ->InsertBefore(*cond_branch);
            auto uses = reg_uses.find(operand);
            if (uses == reg_uses.end()) {
              break;
            }
            std::unordered_set<Instr*>& instrs_using_reg = uses->second;
            const std::unordered_set<const BasicBlock*>& dom_set =
                dom.getBlocksDominatedBy(target);
            for (Instr* instr : instrs_using_reg) {
              if (dom_set.count(instr->block())) {
                instr->ReplaceUsesOf(operand, refined_value);
              }
            }
          }
          cond_branch->ReplaceWith(*Branch::create(target));
        } else {
          JIT_CHECK(false, "Unexpected branch instruction %s", *branch);
        }
        delete branch;
      }
    }
  }
  if (modified) {
    RemoveUnreachableBlocks(cfg);
    reflowTypes(*cfg->func);
  }
  return modified;
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
    auto branch = block->appendWithOff<Branch>(
        term->bytecodeOffset(), term->successor(0));
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
    RemoveUnreachableInstructions(&irfunc.cfg);
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

struct AbstractCall {
  AbstractCall(PyFunctionObject* func, size_t nargs, DeoptBase* instr)
      : func(func), nargs(nargs), instr(instr) {}

  AbstractCall(Register* target, size_t nargs, DeoptBase* instr)
      : target(target),
        func(reinterpret_cast<PyFunctionObject*>(target->type().objectSpec())),
        nargs(nargs),
        instr(instr) {}

  Register* arg(std::size_t i) const {
    if (instr->opcode() == Opcode::kInvokeStaticFunction) {
      auto f = dynamic_cast<InvokeStaticFunction*>(instr);
      return f->arg(i + 1);
    }
    if (auto f = dynamic_cast<VectorCallBase*>(instr)) {
      return f->arg(i);
    }
    JIT_CHECK(false, "unsupported call type %s", instr->opname());
  }

  Register* target{nullptr};
  BorrowedRef<PyFunctionObject> func{nullptr};
  size_t nargs{0};
  DeoptBase* instr{nullptr};
};

static void dlogAndCollectFailureStats(
    Function::InlineFailureStats& inline_failure_stats,
    InlineFailureType failure_type,
    const std::string& function) {
  inline_failure_stats[failure_type].insert(function);
  JIT_DLOG(
      "Can't inline {} because {}",
      function,
      getInlineFailureMessage(failure_type));
}

static void dlogAndCollectFailureStats(
    Function::InlineFailureStats& inline_failure_stats,
    InlineFailureType failure_type,
    const std::string& function,
    const char* tp_name) {
  inline_failure_stats[failure_type].insert(function);
  JIT_DLOG(
      "Can't inline {} because {} but a {:.200s}",
      function,
      getInlineFailureMessage(failure_type),
      tp_name);
}

// Most of these checks are only temporary and do not in perpetuity prohibit
// inlining. They are here to simplify bringup of the inliner and can be
// treated as TODOs.
static bool canInline(
    AbstractCall* call_instr,
    PyFunctionObject* func,
    const std::string& fullname,
    Function::InlineFailureStats& inline_failure_stats) {
  if (func->func_kwdefaults != nullptr) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kHasKwdefaults, fullname);
    return false;
  }
  PyCodeObject* code = reinterpret_cast<PyCodeObject*>(func->func_code);
  if (code->co_kwonlyargcount > 0) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kHasKwOnlyArgs, fullname);
    return false;
  }
  if (code->co_flags & CO_VARARGS) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kHasVarargs, fullname);

    return false;
  }
  if (code->co_flags & CO_VARKEYWORDS) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kHasVarkwargs, fullname);

    return false;
  }
  JIT_DCHECK(code->co_argcount >= 0, "argcount must be positive");
  if (call_instr->nargs != static_cast<size_t>(code->co_argcount)) {
    dlogAndCollectFailureStats(
        inline_failure_stats,
        InlineFailureType::kCalledWithMismatchedArgs,
        fullname);

    return false;
  }
  if (code->co_flags & kCoFlagsAnyGenerator) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kIsGenerator, fullname);

    return false;
  }
  Py_ssize_t ncellvars = PyTuple_GET_SIZE(code->co_cellvars);
  if (ncellvars > 0) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kHasCellvars, fullname);

    return false;
  }
  Py_ssize_t nfreevars = PyTuple_GET_SIZE(code->co_freevars);
  if (nfreevars > 0) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kHasFreevars, fullname);

    return false;
  }
  if (usesRuntimeFunc(code)) {
    dlogAndCollectFailureStats(
        inline_failure_stats, InlineFailureType::kNeedsRuntimeAccess, fullname);
    return false;
  }
  if (g_threaded_compile_context.compileRunning() && !isPreloaded(func)) {
    dlogAndCollectFailureStats(
        inline_failure_stats,
        InlineFailureType::kMultithreadedCompileNeedsPreload,
        fullname);
    return false;
  }
  return true;
}

// As canInline() for checks which require a preloader.
static bool canInlineWithPreloader(
    AbstractCall* call_instr,
    const std::string& fullname,
    const Preloader& preloader,
    Function::InlineFailureStats& inline_failure_stats) {
  auto has_primitive_args = [&]() {
    for (int i = 0; i < preloader.numArgs(); i++) {
      if (preloader.checkArgType(i) <= TPrimitive) {
        return true;
      }
    }
    return false;
  };
  if ((call_instr->instr->IsVectorCall() ||
       call_instr->instr->IsVectorCallStatic()) &&
      (preloader.code()->co_flags & CO_STATICALLY_COMPILED) &&
      (preloader.returnType() <= TPrimitive || has_primitive_args())) {
    // TODO(T122371281) remove this constraint
    dlogAndCollectFailureStats(
        inline_failure_stats,
        InlineFailureType::kIsVectorCallWithPrimitives,
        fullname);
    return false;
  }
  return true;
}

void inlineFunctionCall(Function& caller, AbstractCall* call_instr) {
  PyFunctionObject* func = call_instr->func;
  PyCodeObject* code = reinterpret_cast<PyCodeObject*>(func->func_code);
  JIT_CHECK(PyCode_Check(code), "Expected PyCodeObject");
  PyObject* globals = func->func_globals;
  std::string fullname = funcFullname(func);
  Function::InlineFailureStats& inline_failure_stats =
      caller.inline_function_stats.failure_stats;
  if (!PyDict_Check(globals)) {
    dlogAndCollectFailureStats(
        inline_failure_stats,
        InlineFailureType::kGlobalsNotDict,
        fullname,
        Py_TYPE(globals)->tp_name);
    return;
  }
  if (!PyDict_CheckExact(func->func_builtins)) {
    dlogAndCollectFailureStats(
        inline_failure_stats,
        InlineFailureType::kBuiltinsNotDict,
        fullname,
        Py_TYPE(func->func_builtins)->tp_name);
    return;
  }
  if (!canInline(call_instr, func, fullname, inline_failure_stats)) {
    JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
    return;
  }

  auto caller_frame_state =
      std::make_unique<FrameState>(*call_instr->instr->frameState());
  // Multi-threaded compilation must use an existing Preloader, whereas
  // single-threaded compilation can make Preloaders on the fly.
  InlineResult result;
  if (g_threaded_compile_context.compileRunning()) {
    const Preloader& preloader{getPreloader(func)};
    if (!canInlineWithPreloader(
            call_instr, fullname, preloader, inline_failure_stats)) {
      JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
      return;
    }
    HIRBuilder hir_builder(preloader);
    result = hir_builder.inlineHIR(&caller, caller_frame_state.get());
  } else {
    // This explicit temporary is necessary because HIRBuilder takes a const
    // reference and stores it and we need to make sure the target doesn't go
    // away.
    auto preloader = Preloader::getPreloader(func);
    if (!preloader) {
      JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
      return;
    }
    if (!canInlineWithPreloader(
            call_instr, fullname, *preloader, inline_failure_stats)) {
      JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
      return;
    }
    HIRBuilder hir_builder(*preloader);
    result = hir_builder.inlineHIR(&caller, caller_frame_state.get());
  }
  if (result.entry == nullptr) {
    JIT_DLOG("Cannot inline %s into %s", fullname, caller.fullname);
    return;
  }

  BasicBlock* head = call_instr->instr->block();
  BasicBlock* tail = head->splitAfter(*call_instr->instr);
  auto begin_inlined_function = BeginInlinedFunction::create(
      code,
      func->func_builtins,
      globals,
      std::move(caller_frame_state),
      fullname);
  auto callee_branch = Branch::create(result.entry);
  if (call_instr->target != nullptr) {
    // Not a static call. Check that __code__ has not been swapped out since
    // the function was inlined.
    // VectorCall -> {LoadField, GuardIs, BeginInlinedFunction, Branch to
    // callee CFG}
    // TODO(emacs): Emit a DeoptPatchpoint here to catch the case where someone
    // swaps out function.__code__.
    Register* code_obj = caller.env.AllocateRegister();
    auto load_code = LoadField::create(
        code_obj,
        call_instr->target,
        "func_code",
        offsetof(PyFunctionObject, func_code),
        TObject);
    Register* guarded_code = caller.env.AllocateRegister();
    auto guard_code = GuardIs::create(
        guarded_code, reinterpret_cast<PyObject*>(code), code_obj);
    call_instr->instr->ExpandInto(
        {load_code, guard_code, begin_inlined_function, callee_branch});
  } else {
    call_instr->instr->ExpandInto({begin_inlined_function, callee_branch});
  }
  tail->push_front(EndInlinedFunction::create(begin_inlined_function));

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
  auto assign = Assign::create(
      call_instr->instr->GetOutput(), return_instr->GetOperand(0));
  auto return_branch = Branch::create(tail);
  return_instr->ExpandInto({assign, return_branch});
  delete return_instr;

  delete call_instr->instr;
  caller.inline_function_stats.num_inlined_functions++;
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
  std::vector<AbstractCall> to_inline;
  for (auto& block : irfunc.cfg.blocks) {
    for (auto& instr : block) {
      // TODO(emacs): Support InvokeMethod
      if (instr.IsVectorCall() || instr.IsVectorCallStatic()) {
        auto call = static_cast<VectorCallBase*>(&instr);
        Register* target = call->func();
        if (!target->type().hasValueSpec(TFunc)) {
          JIT_DLOG(
              "Cannot inline non-function type %s (%s) into %s",
              target->type(),
              *target,
              irfunc.fullname);
          continue;
        }
        to_inline.emplace_back(AbstractCall(target, call->numArgs(), call));
      } else if (instr.IsInvokeStaticFunction()) {
        auto call = static_cast<InvokeStaticFunction*>(&instr);
        to_inline.emplace_back(
            AbstractCall(call->func(), call->NumArgs() - 1, call));
      }
    }
  }
  if (to_inline.empty()) {
    return;
  }
  for (auto& instr : to_inline) {
    inlineFunctionCall(irfunc, &instr);
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

static void tryEliminateBeginEnd(EndInlinedFunction* end) {
  BeginInlinedFunction* begin = end->matchingBegin();
  if (begin->block() != end->block()) {
    // TODO(emacs): Support elimination across basic blocks
    return;
  }
  auto it = begin->block()->iterator_to(*begin);
  it++;
  std::vector<Instr*> to_delete{begin, end};
  for (; &*it != end; it++) {
    // Snapshots reference the FrameState owned by BeginInlinedFunction and, if
    // not removed, will contain bad pointers.
    if (it->IsSnapshot()) {
      to_delete.push_back(&*it);
      continue;
    }
    // Instructions that either deopt or otherwise materialize a PyFrameObject
    // need the shadow frames to exist. Everything that materializes a
    // PyFrameObject should also be marked as deopting.
    if (it->asDeoptBase()) {
      return;
    }
  }
  for (Instr* instr : to_delete) {
    instr->unlink();
    delete instr;
  }
}

void BeginInlinedFunctionElimination::Run(Function& irfunc) {
  std::vector<EndInlinedFunction*> ends;
  for (auto& block : irfunc.cfg.blocks) {
    for (auto& instr : block) {
      if (!instr.IsEndInlinedFunction()) {
        continue;
      }
      ends.push_back(static_cast<EndInlinedFunction*>(&instr));
    }
  }
  for (EndInlinedFunction* end : ends) {
    tryEliminateBeginEnd(end);
  }
}

struct MethodInvoke {
  LoadMethod* load_method{nullptr};
  GetSecondOutput* get_instance{nullptr};
  CallMethod* call_method{nullptr};
};

// Returns true if LoadMethod/CallMethod/GetSecondOutput were removed.
// Returns false if they could not be removed.
static bool tryEliminateLoadMethod(Function& irfunc, MethodInvoke& invoke) {
  ThreadedCompileSerialize guard;
  PyCodeObject* code = invoke.load_method->frameState()->code;
  PyObject* names = code->co_names;
  PyObject* name = PyTuple_GetItem(names, invoke.load_method->name_idx());
  JIT_DCHECK(name != nullptr, "name must not be null");
  Register* receiver = invoke.load_method->receiver();
  Type receiver_type = receiver->type();
  // This is a list of common builtin types whose methods cannot be overwritten
  // from managed code and for which looking up the methods is guaranteed to
  // not do anything "weird" that needs to happen at runtime, like make a
  // network request.
  // Note that due to the different staticmethod/classmethod/other descriptors,
  // loading and invoking methods off an instance (e.g. {}.fromkeys(...)) is
  // resolved and called differently than from the type (e.g.
  // dict.fromkeys(...)). The code below handles the instance case only.
  if (!(receiver_type <= TArray || receiver_type <= TBool ||
        receiver_type <= TBytesExact || receiver_type <= TCode ||
        receiver_type <= TDictExact || receiver_type <= TFloatExact ||
        receiver_type <= TListExact || receiver_type <= TLongExact ||
        receiver_type <= TNoneType || receiver_type <= TSetExact ||
        receiver_type <= TTupleExact || receiver_type <= TUnicodeExact)) {
    return false;
  }
  PyTypeObject* type = receiver_type.runtimePyType();
  if (type == nullptr) {
    // This might happen for a variety of reasons, such as encountering a
    // method load on a maybe-defined value where the definition occurs in a
    // block of code that isn't seen by the compiler (e.g. in an except block).
    JIT_DCHECK(
        receiver_type == TBottom,
        "type %s expected to have PyTypeObject*",
        receiver_type);
    return false;
  }
  auto method_obj = Ref<>::create(_PyType_Lookup(type, name));
  if (method_obj == nullptr) {
    // No such method. Let the LoadMethod fail at runtime. _PyType_Lookup does
    // not raise an exception.
    return false;
  }
  if (Py_TYPE(method_obj) == &PyStaticMethod_Type) {
    // This is slightly tricky and nobody uses this except for
    // bytearray/bytes/str.maketrans. Not worth optimizing.
    return false;
  }
  Register* method_reg = invoke.load_method->dst();
  auto load_const = LoadConst::create(
      method_reg, Type::fromObject(irfunc.env.addReference(method_obj.get())));
  auto call_static = VectorCallStatic::create(
      invoke.call_method->NumOperands(),
      invoke.call_method->dst(),
      invoke.call_method->isAwaited(),
      *invoke.call_method->frameState());
  call_static->SetOperand(0, method_reg);
  if (Py_TYPE(method_obj) == &PyClassMethodDescr_Type) {
    // Pass the type as the first argument (e.g. dict.fromkeys).
    Register* type_reg = irfunc.env.AllocateRegister();
    auto load_type = LoadConst::create(
        type_reg, Type::fromObject(reinterpret_cast<PyObject*>(type)));
    load_type->setBytecodeOffset(invoke.load_method->bytecodeOffset());
    load_type->InsertBefore(*invoke.call_method);
    call_static->SetOperand(1, type_reg);
  } else {
    JIT_DCHECK(
        Py_TYPE(method_obj) == &PyMethodDescr_Type ||
            Py_TYPE(method_obj) == &PyWrapperDescr_Type,
        "unexpected type");
    // Pass the instance as the first argument (e.g. str.join, str.__mod__).
    call_static->SetOperand(1, receiver);
  }
  for (std::size_t i = 2; i < invoke.call_method->NumOperands(); i++) {
    call_static->SetOperand(i, invoke.call_method->GetOperand(i));
  }
  auto use_type = UseType::create(receiver, receiver_type.unspecialized());
  invoke.load_method->ExpandInto({use_type, load_const});
  invoke.get_instance->ReplaceWith(
      *Assign::create(invoke.get_instance->dst(), receiver));
  invoke.call_method->ReplaceWith(*call_static);
  delete invoke.load_method;
  delete invoke.get_instance;
  delete invoke.call_method;
  return true;
}

void BuiltinLoadMethodElimination::Run(Function& irfunc) {
  bool changed = true;
  while (changed) {
    changed = false;
    UnorderedMap<LoadMethod*, MethodInvoke> invokes;
    for (auto& block : irfunc.cfg.blocks) {
      for (auto& instr : block) {
        if (!instr.IsCallMethod()) {
          continue;
        }
        auto cm = static_cast<CallMethod*>(&instr);
        auto func_instr = cm->func()->instr();
        if (func_instr->IsLoadMethodSuper()) {
          continue;
        }

        if (isAnyLoadMethod(*func_instr) && !func_instr->IsLoadMethod()) {
          // {FillTypeMethodCache | LoadTypeMethodCacheEntryValue} and
          // CallMethod represent loading and invoking methods off a type (e.g.
          // dict.fromkeys(...)) which do not need to follow
          // LoadMethod/CallMethod pairing invariant and do not benefit from
          // tryEliminateLoadMethod which only handles eliminating of method
          // calls on the instance
          continue;
        }

        JIT_DCHECK(
            func_instr->IsLoadMethod(),
            "LoadMethod/CallMethod should be paired but got %s/CallMethod",
            func_instr->opname());
        auto lm = static_cast<LoadMethod*>(func_instr);

        JIT_DCHECK(
            cm->self()->instr()->IsGetSecondOutput(),
            "GetSecondOutput/CallMethod should be paired but got "
            "%s/CallMethod",
            cm->self()->instr()->opname());
        auto glmi = static_cast<GetSecondOutput*>(cm->self()->instr());
        auto result = invokes.insert({lm, MethodInvoke{lm, glmi, cm}});
        if (!result.second) {
          // This pass currently only handles 1:1 LoadMethod/CallMethod
          // combinations. If there are multiple CallMethod for a given
          // LoadMethod, bail out.
          // TODO(T138839090): support multiple CallMethod
          invokes.erase(result.first);
        }
      }
    }
    for (auto [lm, invoke] : invokes) {
      changed |= tryEliminateLoadMethod(irfunc, invoke);
    }
    reflowTypes(irfunc);
  }
}

} // namespace jit::hir
