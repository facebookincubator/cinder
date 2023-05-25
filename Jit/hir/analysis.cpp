// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/analysis.h"

#include "Jit/dataflow.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/memory_effects.h"
#include "Jit/hir/printer.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <memory>

namespace jit::hir {

const RegisterSet kEmptyRegSet;

std::ostream& operator<<(std::ostream& os, const RegisterSet& regs) {
  fmt::print(os, "RegisterSet[{}] = {{", regs.size());
  std::vector<Register*> sorted_regs{regs.begin(), regs.end()};
  std::sort(sorted_regs.begin(), sorted_regs.end(), [](auto r1, auto r2) {
    return r1->id() < r2->id();
  });
  auto sep = "";
  for (auto reg : sorted_regs) {
    fmt::print(os, "{}{}", sep, reg->name());
    sep = ", ";
  }
  return os << "}";
}

bool isPassthrough(const Instr& instr) {
  switch (instr.opcode()) {
    case Opcode::kAssign:
    case Opcode::kBitCast:
    case Opcode::kCheckErrOccurred:
    case Opcode::kCheckExc:
    case Opcode::kCheckField:
    case Opcode::kCheckFreevar:
    case Opcode::kCheckNeg:
    case Opcode::kCheckVar:
    case Opcode::kGuardIs:
    case Opcode::kGuardType:
    case Opcode::kRefineType:
    case Opcode::kUseType:
      return true;

    // Cast is pass-through except when we are casting to float, in which case
    // we may coerce an incoming int to a new float.
    case Opcode::kCast:
      return (static_cast<const Cast*>(&instr))->pytype() != &PyFloat_Type;

    case Opcode::kBinaryOp:
    case Opcode::kBuildSlice:
    case Opcode::kBuildString:
    case Opcode::kCallCFunc:
    case Opcode::kCallEx:
    case Opcode::kCallExKw:
    case Opcode::kCallMethod:
    case Opcode::kCallStatic:
    case Opcode::kCallStaticRetVoid:
    case Opcode::kCheckSequenceBounds:
    case Opcode::kCompare:
    case Opcode::kCompareBool:
    case Opcode::kCopyDictWithoutKeys:
    case Opcode::kDictMerge:
    case Opcode::kDictSubscr:
    case Opcode::kDictUpdate:
    case Opcode::kDoubleBinaryOp:
    case Opcode::kFillTypeAttrCache:
    case Opcode::kFillTypeMethodCache:
    case Opcode::kFormatValue:
    case Opcode::kGetANext:
    case Opcode::kGetAIter:
    case Opcode::kGetIter:
    case Opcode::kGetLength:
    case Opcode::kGetSecondOutput:
    case Opcode::kGetTuple:
    case Opcode::kImportFrom:
    case Opcode::kImportName:
    case Opcode::kInPlaceOp:
    case Opcode::kInitialYield:
    case Opcode::kIntBinaryOp:
    case Opcode::kPrimitiveCompare:
    case Opcode::kIntConvert:
    case Opcode::kPrimitiveUnbox:
    case Opcode::kInvokeIterNext:
    case Opcode::kInvokeMethod:
    case Opcode::kInvokeStaticFunction:
    case Opcode::kIsInstance:
    case Opcode::kIsNegativeAndErrOccurred:
    case Opcode::kIsTruthy:
    case Opcode::kListAppend:
    case Opcode::kListExtend:
    case Opcode::kLoadArg:
    case Opcode::kLoadArrayItem:
    case Opcode::kLoadAttr:
    case Opcode::kLoadAttrSpecial:
    case Opcode::kLoadAttrSuper:
    case Opcode::kLoadCellItem:
    case Opcode::kLoadConst:
    case Opcode::kLoadCurrentFunc:
    case Opcode::kLoadEvalBreaker:
    case Opcode::kLoadField:
    case Opcode::kLoadFieldAddress:
    case Opcode::kLoadFunctionIndirect:
    case Opcode::kLoadGlobal:
    case Opcode::kLoadGlobalCached:
    case Opcode::kLoadMethod:
    case Opcode::kLoadModuleMethod:
    case Opcode::kLoadMethodSuper:
    case Opcode::kLoadSplitDictItem:
    case Opcode::kLoadTupleItem:
    case Opcode::kLoadTypeAttrCacheItem:
    case Opcode::kLoadTypeMethodCacheEntryValue:
    case Opcode::kLoadTypeMethodCacheEntryType:
    case Opcode::kLoadVarObjectSize:
    case Opcode::kLongCompare:
    case Opcode::kLongBinaryOp:
    case Opcode::kMakeCell:
    case Opcode::kMakeCheckedDict:
    case Opcode::kMakeDict:
    case Opcode::kMakeCheckedList:
    case Opcode::kMakeFunction:
    case Opcode::kMakeList:
    case Opcode::kMakeSet:
    case Opcode::kMakeTuple:
    case Opcode::kMakeTupleFromList:
    case Opcode::kMatchClass:
    case Opcode::kMatchKeys:
    case Opcode::kMergeSetUnpack:
    case Opcode::kPhi:
    case Opcode::kPrimitiveBox:
    case Opcode::kPrimitiveBoxBool:
    case Opcode::kPrimitiveUnaryOp:
    case Opcode::kRepeatList:
    case Opcode::kRepeatTuple:
    case Opcode::kRunPeriodicTasks:
    case Opcode::kSetCurrentAwaiter:
    case Opcode::kSetDictItem:
    case Opcode::kSetSetItem:
    case Opcode::kSetUpdate:
    case Opcode::kStealCellItem:
    case Opcode::kStoreArrayItem:
    case Opcode::kStoreAttr:
    case Opcode::kStoreSubscr:
    case Opcode::kTpAlloc:
    case Opcode::kUnaryOp:
    case Opcode::kUnicodeCompare:
    case Opcode::kUnicodeConcat:
    case Opcode::kUnicodeRepeat:
    case Opcode::kUnpackExToTuple:
    case Opcode::kVectorCall:
    case Opcode::kVectorCallKW:
    case Opcode::kVectorCallStatic:
    case Opcode::kWaitHandleLoadCoroOrResult:
    case Opcode::kWaitHandleLoadWaiter:
    case Opcode::kYieldAndYieldFrom:
    case Opcode::kYieldFrom:
    case Opcode::kYieldFromHandleStopAsyncIteration:
    case Opcode::kYieldValue:
      return false;

    case Opcode::kBatchDecref:
    case Opcode::kBeginInlinedFunction:
    case Opcode::kBranch:
    case Opcode::kCondBranch:
    case Opcode::kCondBranchIterNotDone:
    case Opcode::kCondBranchCheckType:
    case Opcode::kDecref:
    case Opcode::kDeleteAttr:
    case Opcode::kDeleteSubscr:
    case Opcode::kDeopt:
    case Opcode::kDeoptPatchpoint:
    case Opcode::kEndInlinedFunction:
    case Opcode::kGuard:
    case Opcode::kHintType:
    case Opcode::kSnapshot:
    case Opcode::kIncref:
    case Opcode::kInitFunction:
    case Opcode::kReturn:
    case Opcode::kSetCellItem:
    case Opcode::kSetFunctionAttr:
    case Opcode::kStoreField:
    case Opcode::kUnreachable:
    case Opcode::kXDecref:
    case Opcode::kXIncref:
    case Opcode::kRaiseAwaitableError:
    case Opcode::kRaise:
    case Opcode::kRaiseStatic:
    case Opcode::kWaitHandleRelease:
      JIT_ABORT("Opcode %s has no output", instr.opname());
  }
  JIT_ABORT("Bad opcode %d", static_cast<int>(instr.opcode()));
}

Register* modelReg(Register* reg) {
  auto orig_reg = reg;
  // Even though GuardIs is a passthrough, it verifies that a runtime value is a
  // specific object, breaking the dependency on the instruction that produced
  // the runtime value
  while (isPassthrough(*reg->instr()) && !(reg->instr()->IsGuardIs())) {
    reg = reg->instr()->GetOperand(0);
    JIT_DCHECK(reg != orig_reg, "Hit cycle while looking for model reg");
  }
  return reg;
}

bool isLoadMethodBase(const Instr& instr) {
  return dynamic_cast<const LoadMethodBase*>(&instr) != nullptr;
}

bool isAnyLoadMethod(const Instr& instr) {
  if (isLoadMethodBase(instr)) {
    return true;
  }
  if (!instr.IsPhi() || instr.NumOperands() != 2) {
    return false;
  }
  const Instr* arg1 = instr.GetOperand(0)->instr();
  const Instr* arg2 = instr.GetOperand(1)->instr();
  return (arg1->IsLoadTypeMethodCacheEntryValue() &&
          arg2->IsFillTypeMethodCache()) ||
      (arg2->IsLoadTypeMethodCacheEntryValue() &&
       arg1->IsFillTypeMethodCache());
}

static bool isSingleCInt(Type t) {
  return t <= TCInt8 || t <= TCUInt8 || t <= TCInt16 || t <= TCUInt16 ||
      t <= TCInt32 || t <= TCUInt32 || t <= TCInt64 || t <= TCUInt64;
}

bool registerTypeMatches(Type op_type, OperandType expected_type) {
  switch (expected_type.kind) {
    case Constraint::kType:
      return op_type <= expected_type.type;
    case Constraint::kTupleExactOrCPtr:
      return op_type <= TTupleExact || op_type <= TCPtr;
    case Constraint::kListOrChkList:
      return op_type <= TList ||
          (op_type.hasTypeSpec() &&
           Ci_CheckedList_TypeCheck(op_type.typeSpec()));
    case Constraint::kDictOrChkDict:
      return op_type <= TDict ||
          (op_type.hasTypeSpec() &&
           Ci_CheckedDict_TypeCheck(op_type.typeSpec()));
    case Constraint::kOptObjectOrCIntOrCBool:
      return op_type <= TOptObject || op_type <= TCInt || op_type <= TCBool;
    case Constraint::kOptObjectOrCInt:
      return op_type <= TOptObject || op_type <= TCInt;
    case Constraint::kMatchAllAsCInt:
      return isSingleCInt(op_type);
    case Constraint::kMatchAllAsPrimitive:
      return isSingleCInt(op_type) || op_type <= TCBool ||
          op_type <= TCDouble || op_type <= TCPtr;
  }
  JIT_ABORT("unknown constraint");
  return false;
}

bool operandsMustMatch(OperandType op_type) {
  switch (op_type.kind) {
    case Constraint::kMatchAllAsCInt:
    case Constraint::kMatchAllAsPrimitive:
      return true;

    case Constraint::kType:
    case Constraint::kTupleExactOrCPtr:
    case Constraint::kListOrChkList:
    case Constraint::kDictOrChkDict:
    case Constraint::kOptObjectOrCInt:
    case Constraint::kOptObjectOrCIntOrCBool:
      return false;
  }
  JIT_ABORT("unknown constraint");
  return false;
}

bool funcTypeChecks(const Function& func, std::ostream& err) {
  for (auto& block : func.cfg.blocks) {
    for (const Instr& instr : block) {
      if (instr.NumOperands() > 1 &&
          operandsMustMatch(instr.GetOperandType(0))) {
        Type join = TBottom;
        for (std::size_t i = 0; i < instr.NumOperands(); i++) {
          JIT_DCHECK(
              operandsMustMatch(instr.GetOperandType(i)),
              "Inconsistent operand type constraint");
          join |= instr.GetOperand(i)->type();
        }
        OperandType expected_type = instr.GetOperandType(0);
        if (!registerTypeMatches(join, expected_type)) {
          fmt::print(
              err,
              "TYPE MISMATCH in bb {} of '{}'\nInstr '{}' expected "
              "join of operands of type {} to subclass '{}'\n",
              block.id,
              func.fullname,
              instr,
              join,
              expected_type);
          return false;
        }
      } else {
        for (std::size_t i = 0; i < instr.NumOperands(); i++) {
          Register* op = instr.GetOperand(i);
          OperandType expected_type = instr.GetOperandType(i);
          if (!registerTypeMatches(op->type(), expected_type)) {
            fmt::print(
                err,
                "TYPE MISMATCH in bb {} of '{}'\nInstr '{}' expected "
                "operand {} to be of type {} "
                "but got {} from '{}'\n",
                block.id,
                func.fullname,
                instr,
                i,
                expected_type,
                op->type(),
                *op->instr());
            return false;
          }
        }
      }
    }
  }
  return true;
}

void DataflowAnalysis::AddBasicBlock(const BasicBlock* cfg_block) {
  auto res = df_blocks_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(cfg_block),
      std::forward_as_tuple());
  auto& df_block = res.first->second;
  df_analyzer_.AddBlock(df_block);
  setUninitialized(&df_block);

  std::unordered_set<Register*> gen, kill;
  ComputeGenKill(cfg_block, gen, kill);

  for (auto reg : gen) {
    df_analyzer_.SetBlockGenBit(df_block, reg);
  }
  for (auto reg : kill) {
    df_analyzer_.SetBlockKillBit(df_block, reg);
  }
}

void DataflowAnalysis::Initialize() {
  // Add all registers -- this sets up the correct number of bits for the
  // analysis
  num_bits_ = irfunc_.env.GetRegisters().size();
  for (const auto& it : irfunc_.env.GetRegisters()) {
    df_analyzer_.AddObject(it.second.get());
  }

  // Compute the initial state for each block
  for (const auto& cfg_block : irfunc_.cfg.blocks) {
    AddBasicBlock(&cfg_block);
  }

  // Set up dataflow graph
  df_analyzer_.AddBlock(df_entry_);
  df_analyzer_.SetEntryBlock(df_entry_);

  df_analyzer_.AddBlock(df_exit_);
  df_analyzer_.SetExitBlock(df_exit_);

  for (const auto& cfg_block : irfunc_.cfg.blocks) {
    auto& df_block = df_blocks_[&cfg_block];

    if (&cfg_block == irfunc_.cfg.entry_block) {
      df_entry_.ConnectTo(df_block);
    }

    if (cfg_block.out_edges().empty()) {
      df_block.ConnectTo(df_exit_);
    } else {
      for (auto cfg_edge : cfg_block.out_edges()) {
        auto succ_cfg_block = cfg_edge->to();
        JIT_CHECK(
            df_blocks_.count(succ_cfg_block),
            "succ_cfg_block has to be in the hash table df_blocks_.");
        auto& succ_df_block = df_blocks_.at(succ_cfg_block);
        df_block.ConnectTo(succ_df_block);
      }
    }
  }
}

RegisterSet DataflowAnalysis::GetIn(const BasicBlock* cfg_block) {
  RegisterSet in;
  const auto& df_block = df_blocks_[cfg_block];
  df_analyzer_.forEachBlockIn(df_block, [&](Register* r) { in.insert(r); });
  return in;
}

RegisterSet DataflowAnalysis::GetOut(const BasicBlock* cfg_block) {
  RegisterSet out;
  const auto& df_block = df_blocks_[cfg_block];
  df_analyzer_.forEachBlockOut(df_block, [&](Register* r) { out.insert(r); });
  return out;
}

void DataflowAnalysis::dump() {
  if (!g_debug) {
    return;
  }

  std::string out = fmt::format("{} complete:\n", name());
  for (auto& block : irfunc_.cfg.blocks) {
    format_to(out, "  bb {}\n", block.id);
    auto format_set = [&](const RegisterSet& regs) {
      for (auto reg : regs) {
        format_to(out, "    {}\n", reg->name());
      }
    };
    format_to(out, "  In:\n");
    format_set(GetIn(&block));
    format_to(out, "  Out:\n");
    format_set(GetOut(&block));
    format_to(out, "\n");
  }

  JIT_DLOG("%s", out);
}

void BackwardDataflowAnalysis::Run() {
  Initialize();

  std::list<jit::optimizer::DataFlowBlock*> blocks;
  for (auto& it : df_blocks_) {
    if (&it.second != &df_entry_) {
      blocks.emplace_back(&it.second);
    }
  }

  while (!blocks.empty()) {
    auto block = blocks.front();
    blocks.pop_front();

    auto new_out = ComputeNewOut(block);
    bool changed = (new_out != block->out_);
    block->out_ = std::move(new_out);

    auto new_in = ComputeNewIn(block);
    changed |= (new_in != block->in_);
    block->in_ = std::move(new_in);

    if (changed) {
      std::copy(
          block->pred_.begin(), block->pred_.end(), std::back_inserter(blocks));
    }
  }
}

void ForwardDataflowAnalysis::Run() {
  Initialize();

  std::list<jit::optimizer::DataFlowBlock*> blocks;
  for (auto& it : df_blocks_) {
    if (&it.second != &df_exit_) {
      blocks.emplace_back(&it.second);
    }
  }

  while (!blocks.empty()) {
    auto block = blocks.front();
    blocks.pop_front();

    auto new_in = ComputeNewIn(block);
    bool changed = (new_in != block->in_);
    block->in_ = std::move(new_in);

    auto new_out = ComputeNewOut(block);
    changed |= (new_out != block->out_);
    block->out_ = std::move(new_out);

    if (changed) {
      blocks.insert(blocks.end(), block->succ_.begin(), block->succ_.end());
    }
  }
}

template <typename OutputFunc, typename UseFunc>
static void analyzeInstrLiveness(
    const Instr& instr,
    OutputFunc define_output,
    UseFunc use) {
  if (auto output = instr.GetOutput()) {
    define_output(output);
  }

  if (instr.IsPhi()) {
    // Phi uses happen at the end of the predecessor block.
    return;
  }

  instr.visitUses([&](Register* reg) {
    use(reg);
    return true;
  });

  if (instr.numEdges() > 0) {
    // Mark any Phi inputs on successors to this block as live. When we switch
    // to Branch passing arguments to blocks rather than using Phis, this will
    // happen naturally as the Branch is processed.
    for (size_t i = 0, n = instr.numEdges(); i < n; ++i) {
      auto succ = instr.successor(i);
      int phi_idx = -1;
      for (auto& succ_instr : *succ) {
        if (!succ_instr.IsPhi()) {
          break;
        }
        auto& phi = static_cast<const Phi&>(succ_instr);
        if (phi_idx == -1) {
          phi_idx = phi.blockIndex(instr.block());
        }
        use(phi.GetOperand(phi_idx));
      }
    }
  }
}

LivenessAnalysis::LastUses LivenessAnalysis::GetLastUses() {
  LastUses last_uses;

  for (auto& pair : df_blocks_) {
    auto block = pair.first;
    auto live = GetOut(block);

    for (auto it = block->rbegin(); it != block->rend(); ++it) {
      auto& instr = *it;
      analyzeInstrLiveness(
          instr,
          [&](Register* output) {
            if (live.erase(output) == 0) {
              // output isn't live after instr. It's dead and dies right after
              // definition.
              last_uses[&instr].emplace(output);
            }
          },
          [&](Register* value) {
            if (live.emplace(value).second) {
              // value isn't live after instr, so this is a last use.
              last_uses[&instr].emplace(value);
            }
          });
    }
  }

  return last_uses;
}

void LivenessAnalysis::ComputeGenKill(
    const BasicBlock* cfg_block,
    RegisterSet& gen,
    RegisterSet& kill) {
  for (auto it = cfg_block->rbegin(); it != cfg_block->rend(); ++it) {
    analyzeInstrLiveness(
        *it,
        [&](Register* output) {
          kill.insert(output);
          gen.erase(output);
        },
        [&](Register* use) { gen.insert(use); });
  }
}

jit::util::BitVector LivenessAnalysis::ComputeNewIn(
    const jit::optimizer::DataFlowBlock* block) {
  jit::util::BitVector new_in(num_bits_);
  new_in = block->gen_ | (block->out_ - block->kill_);
  return new_in;
}

jit::util::BitVector LivenessAnalysis::ComputeNewOut(
    const jit::optimizer::DataFlowBlock* block) {
  jit::util::BitVector new_out(num_bits_);
  for (auto& succ : block->succ_) {
    new_out |= succ->in_;
  }
  return new_out;
}

void LivenessAnalysis::setUninitialized(jit::optimizer::DataFlowBlock*) {
  // Do nothing.
}

bool LivenessAnalysis::IsLiveIn(const BasicBlock* cfg_block, Register* reg) {
  const auto& df_block = df_blocks_[cfg_block];
  return df_analyzer_.GetBlockInBit(df_block, reg);
}

bool LivenessAnalysis::IsLiveOut(const BasicBlock* cfg_block, Register* reg) {
  const auto& df_block = df_blocks_[cfg_block];
  return df_analyzer_.GetBlockOutBit(df_block, reg);
}

AssignmentAnalysis::AssignmentAnalysis(const Function& irfunc, bool is_definite)
    : ForwardDataflowAnalysis(irfunc), args_(), is_definite_(is_definite) {
  for (const auto& instr : *irfunc_.cfg.entry_block) {
    if (instr.IsLoadArg()) {
      args_.insert(instr.GetOutput());
    }
  }
}

bool AssignmentAnalysis::IsAssignedIn(
    const BasicBlock* cfg_block,
    Register* reg) {
  const auto& df_block = df_blocks_[cfg_block];
  return df_analyzer_.GetBlockInBit(df_block, reg);
}

bool AssignmentAnalysis::IsAssignedOut(
    const BasicBlock* cfg_block,
    Register* reg) {
  const auto& df_block = df_blocks_[cfg_block];
  return df_analyzer_.GetBlockOutBit(df_block, reg);
}

void AssignmentAnalysis::ComputeGenKill(
    const BasicBlock* block,
    RegisterSet& gen,
    RegisterSet& /* kill */) {
  gen = args_;
  for (const auto& instr : *block) {
    auto output = instr.GetOutput();
    if (output != nullptr) {
      gen.insert(output);
    }
  }
}

jit::util::BitVector AssignmentAnalysis::ComputeNewIn(
    const jit::optimizer::DataFlowBlock* block) {
  if (block->pred_.empty()) {
    jit::util::BitVector new_in(num_bits_);
    return new_in;
  }
  auto it = block->pred_.begin();
  auto pred = *it++;
  jit::util::BitVector new_in = pred->out_;
  while (it != block->pred_.end()) {
    if (is_definite_) {
      new_in &= (*it)->out_;
    } else {
      new_in |= (*it)->out_;
    }
    it++;
  }
  return new_in;
}

jit::util::BitVector AssignmentAnalysis::ComputeNewOut(
    const jit::optimizer::DataFlowBlock* block) {
  jit::util::BitVector new_out(num_bits_);
  new_out = block->gen_ | (block->in_ - block->kill_);
  return new_out;
}

void AssignmentAnalysis::setUninitialized(
    jit::optimizer::DataFlowBlock* block) {
  if (is_definite_) {
    block->out_.fill(true);
  }
}

DominatorAnalysis::DominatorAnalysis(const Function& irfunc)
    : idoms_{}, dom_sets_{} {
  // Calculate immediate dominators with the iterative two-finger algorithm.
  // When it terminates, idoms_[block-id] will contain the block-id of the
  // immediate dominator of each block.  idoms_[start] will be nullptr. This is
  // the general algorithm but it will only loop twice for loop-free graphs.
  std::vector<BasicBlock*> rpo = irfunc.cfg.GetRPOTraversal();
  // Map block ids to their index in the RPO traversal
  std::unordered_map<int, int> rpo_index{};
  for (size_t i = 0; i < rpo.size(); i++) {
    rpo_index[rpo[i]->id] = i;
  }
  auto start = rpo.begin();
  const BasicBlock* entry = *start;
  idoms_[entry->id] = entry;
  start++;
  for (bool changed = true; changed;) {
    changed = false;
    for (auto it = start; it != rpo.end(); it++) {
      const BasicBlock* block = *it;
      auto predIter = block->in_edges().begin();
      auto predEnd = block->in_edges().end();
      // pred1 = any already-processed predecessor
      auto pred1 = const_cast<const BasicBlock*>((*predIter)->from());
      while (!idoms_[pred1->id]) {
        JIT_DCHECK(
            predIter != predEnd,
            "There should be an already-processed predecessor since we're "
            "iterating in RPO");
        pred1 = (*(++predIter))->from();
      }
      // for all other already-processed predecessors pred2 of block
      for (++predIter; predIter != predEnd; ++predIter) {
        auto pred2 = const_cast<const BasicBlock*>((*predIter)->from());
        if (pred2 == pred1 || !idoms_[pred2->id]) {
          continue;
        }
        // find earliest common predecessor of pred1 and pred2
        // (lower RPO ids are earlier in flow and in dom-tree).
        do {
          while (rpo_index[pred1->id] < rpo_index[pred2->id]) {
            pred2 = idoms_[pred2->id];
          }
          while (rpo_index[pred2->id] < rpo_index[pred1->id]) {
            pred1 = idoms_[pred1->id];
          }
        } while (pred1 != pred2);
      }
      if (idoms_[block->id] != pred1) {
        idoms_[block->id] = pred1;
        changed = true;
      }
    }
  }
  idoms_[entry->id] = nullptr;
  for (auto it = rpo.rbegin(); it != rpo.rend(); ++it) {
    const BasicBlock* block = *it;
    auto& block_dom_set_ = dom_sets_[block->id];
    block_dom_set_.insert(block);
    const BasicBlock* block_dom = idoms_[block->id];
    if (block_dom != nullptr) {
      dom_sets_[block_dom->id].insert(
          block_dom_set_.begin(), block_dom_set_.end());
    }
  }
}

RegisterTypeHints::RegisterTypeHints(const Function& irfunc)
    : dom_hint_{}, doms_{irfunc} {
  for (const auto& block : irfunc.cfg.blocks) {
    for (const auto& instr : block) {
      if (instr.IsHintType()) {
        for (size_t i = 0; i < instr.NumOperands(); i++) {
          dom_hint_[instr.GetOperand(i)][block.id] = &instr;
        }
      } else if (instr.IsPhi()) {
        dom_hint_[instr.GetOutput()][block.id] = &instr;
      }
    }
  }
}

const Instr* RegisterTypeHints::dominatingTypeHint(
    Register* reg,
    const BasicBlock* block) {
  // Make sure we don't default construct the map for untracked registers
  auto it = dom_hint_.find(reg);
  if (it == dom_hint_.end()) {
    return nullptr;
  }
  std::unordered_map<int, const Instr*> hint_types = it->second;
  // Look for the first type hint that dominates the passed in block
  while (!hint_types[block->id]) {
    block = doms_.immediateDominator(block);
    if (block == nullptr) {
      return nullptr;
    }
  }
  return hint_types[block->id];
}

} // namespace jit::hir
