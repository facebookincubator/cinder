// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/bitvector.h"
#include "Jit/deopt.h"
#include "Jit/hir/analysis.h"
#include "Jit/hir/memory_effects.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/log.h"

#include <fmt/ostream.h>

#include <algorithm>
#include <queue>
#include <set>
#include <vector>

#define TRACE(...) JIT_LOGIF(g_debug_refcount, __VA_ARGS__)

// This file implements our reference count insertion pass. If this is your
// first time here, I recommend reading refcount_insertion.md first.

namespace jit::hir {

namespace {

// Borrow support, represented as a bit vector. The least significant
// AliasClass::kNumBits hold an AliasClass, and the rest of the bits each
// represent one Register. Only Phi inputs can be used as borrow support, and
// bits are assigned to Registers in Env's constructor.
//
// BorrowSupport starts out empty and must be initialized with a call to
// init(num_support_bits) before use.
class BorrowSupport {
  static_assert(
      AliasClass::kNumBits <= 64,
      "AliasClass bits must fit in BitVector chunk");

 public:
  void clear() {
    bits_.SetBitWidth(0);
  }

  void init(size_t num_support_bits) {
    bits_.SetBitWidth(num_support_bits);
    bits_.fill(0);
  }

  bool empty() const {
    return bits_.IsEmpty();
  }

  bool intersects(const BorrowSupport& other) const {
    return !(bits_ & other.bits_).IsEmpty();
  }
  bool intersects(AliasClass acls) const {
    return (bits_.GetBitChunk(0) & acls.bits()) != 0;
  }
  bool intersects(size_t bit) const {
    return bits_.GetBit(bit);
  }

  bool operator==(const BorrowSupport& other) const {
    return bits_ == other.bits_;
  }

  bool operator!=(const BorrowSupport& other) const {
    return !operator==(other);
  }

  const util::BitVector& bits() const {
    return bits_;
  }

  void add(const BorrowSupport& other) {
    bits_ |= other.bits_;
  }
  void add(AliasClass acls) {
    bits_.SetBitChunk(0, bits_.GetBitChunk(0) | acls.bits());
  }

  void add(size_t bit) {
    bits_.SetBit(bit, 1);
  }

  void remove(AliasClass acls) {
    bits_.SetBitChunk(0, bits_.GetBitChunk(0) & ~acls.bits());
  }

  void remove(size_t bit) {
    bits_.SetBit(bit, 0);
  }

 private:
  util::BitVector bits_;
};

// The state of a live value, including arbitrarily many copies of the original
// Register (from instructions like Assign and CheckExc).
struct RegState {
  RegState(Register* model) : model_{model} {
    addCopy(model);
  }

  bool operator==(const RegState& other) const {
    return model_ == other.model_ && copies_ == other.copies_ &&
        kind_ == other.kind_ && support_ == other.support_;
  }
  bool operator!=(const RegState& other) const {
    return !operator==(other);
  }

  // The model Register, or the original version that may or may not have been
  // copied.
  Register* model() const {
    return model_;
  }

  // The most recently defined copy of the model, which may still be the model
  // itself.
  Register* current() const {
    JIT_DCHECK(!copies_.empty(), "%s has no live copies", model_->name());
    return copies_.back();
  }

  void addCopy(Register* copy) {
    copies_.emplace_back(copy);
  }

  // Remove the given Register from the list of live copies, returning true iff
  // there are now no more live copies.
  bool killCopy(Register* copy) {
    // The linear search and erase here assumes that having more than a couple
    // copies of a value is rare.
    auto it = std::find(copies_.begin(), copies_.end(), copy);
    JIT_DCHECK(
        it != copies_.end(),
        "%s isn't a live copy of %s",
        copy->name(),
        model_->name());
    copies_.erase(it);
    return copies_.empty();
  }

  int numCopies() const {
    return copies_.size();
  }

  Register* copy(size_t i) const {
    JIT_DCHECK(i < copies_.size(), "Invalid index %d", i);
    return copies_[i];
  }

  // Merge `from` into `this`.
  void merge(const RegState& from) {
    if (kind() == from.kind()) {
      // The two kinds are the same, so keep that in the merged result. For
      // two borrowed references, merge their support.
      if (isBorrowed()) {
        support().add(from.support());
      }
    } else if (isUncounted()) {
      // Merging Uncounted with anything else takes the other state.
      *this = from;
    } else if (from.isUncounted()) {
      // As with the previous case, use what's already in this.
    } else {
      // The two states are different and neither is uncounted, so one is
      // borrowed and one is owned. The merged result is owned.
      setOwned();
    }
  }

  RefKind kind() const {
    return kind_;
  }

  bool isUncounted() const {
    return kind_ == RefKind::kUncounted;
  }
  bool isBorrowed() const {
    return kind_ == RefKind::kBorrowed;
  }
  bool isOwned() const {
    return kind_ == RefKind::kOwned;
  }

  void setUncounted() {
    kind_ = RefKind::kUncounted;
    support_.clear();
  }

  void setBorrowed(size_t num_support_bits) {
    kind_ = RefKind::kBorrowed;
    support_.init(num_support_bits);
  }

  void setOwned() {
    kind_ = RefKind::kOwned;
    support_.clear();
  }

  BorrowSupport& support() {
    JIT_DCHECK(isBorrowed(), "Value isn't borrowed");
    return support_;
  }

  const BorrowSupport& support() const {
    return const_cast<RegState*>(this)->support();
  }

 private:
  Register* model_;
  std::vector<Register*> copies_;
  RefKind kind_{RefKind::kUncounted};
  BorrowSupport support_;
};

// A map from model values to their RegState, implemented as a thin wrapper
// around std:unordered_map<> that calls modelReg() on keys by default.
//
// All live values are tracked, even if they aren't a reference counted type,
// in order to correctly populate deopt info.
class StateMap {
  using map_t = std::unordered_map<Register*, RegState>;

 public:
  auto size() const {
    return map_.size();
  }
  auto empty() const {
    return map_.empty();
  }

  auto countModel(Register* model) const {
    JIT_DCHECK(model == modelReg(model), "countModel given non-model reg");
    return map_.count(model);
  }

  RegState& getModel(Register* model) {
    JIT_DCHECK(model == modelReg(model), "getModel given non-model reg");
    return map_get(map_, model);
  }
  const RegState& getModel(Register* model) const {
    JIT_DCHECK(model == modelReg(model), "getModel given non-model reg");
    return map_get(map_, model);
  }

  auto find(Register* reg) {
    return map_.find(modelReg(reg));
  }
  auto find(Register* reg) const {
    return map_.find(modelReg(reg));
  }

  auto findModel(Register* model) {
    JIT_DCHECK(model == modelReg(model), "findModel given non-model reg");
    return map_.find(model);
  }
  auto findModel(Register* model) const {
    JIT_DCHECK(model == modelReg(model), "findModel given non-model reg");
    return map_.find(model);
  }

  template <typename... Args>
  auto emplace(Args&&... args) {
    return map_.emplace(std::forward<Args>(args)...);
  }

  auto begin() {
    return map_.begin();
  }
  auto begin() const {
    return map_.begin();
  }

  auto end() {
    return map_.end();
  }
  auto end() const {
    return map_.end();
  }

  auto eraseModel(Register* model) {
    JIT_DCHECK(model == modelReg(model), "eraseModel given non-model reg");
    return map_.erase(model);
  }
  auto erase(map_t::iterator it) {
    return map_.erase(it);
  }

  bool operator==(const StateMap& other) const {
    return map_ == other.map_;
  }
  bool operator!=(const StateMap& other) const {
    return !operator==(other);
  }

 private:
  map_t map_;
};

// In- and out-states for a BasicBlock, populated during the analysis phase.
struct BlockState {
  // For blocks with <= 1 predecessor: an empty map.
  //
  // For blocks with >1 predecessor: values that are live after any Phis at
  // block entry, including the Phi outputs.
  StateMap in;

  // Values that are live before the final control flow instruction
  // (CondBranch, CondBranchCheckType, etc.) or after the terminator (Return,
  // Deopt, etc.).
  StateMap out;
};

// For every Register that is an input to one or more Phis, map from predecessor
// blocks to the Phi outputs that value contributes to.
using PhiUseMap = std::unordered_map<
    Register*,
    std::unordered_map<BasicBlock*, std::vector<Register*>>>;

struct RegisterLess {
  bool operator()(const Register* a, const Register* b) const {
    return a->id() < b->id();
  }
};

struct RegStateLess {
  bool operator()(const RegState* a, const RegState* b) const {
    return RegisterLess{}(a->model(), b->model());
  }
};

// Global state used by the analysis.
struct Env {
  Env(Function& func) : func{func}, liveness{func} {
    liveness.Run();
    last_uses = liveness.GetLastUses();

    // Visit each Phi to collect some metadata:
    // - Assign a borrow support bit to any Register that is a Phi input or
    //   output.
    // - Build up a map of values used by Phis, and the blocks they come from.
    std::string bit_names;
    auto add_support_bit = [&](Register* model) {
      if (reg_to_bit.emplace(model, num_support_bits).second) {
        if (g_debug_refcount) {
          format_to(bit_names, "  {} => {}\n", num_support_bits, *model);
        }
        num_support_bits++;
      }
    };

    for (auto& block : func.cfg.blocks) {
      block.forEachPhi([&](Phi& phi) {
        auto output = phi.GetOutput();
        add_support_bit(output);
        for (int i = 0, n = phi.NumOperands(); i < n; ++i) {
          auto model = modelReg(phi.GetOperand(i));
          add_support_bit(model);
          phi_uses[model][phi.basic_blocks()[i]].emplace_back(output);
        }
      });
    }

    TRACE("Support bits:\n%s", bit_names);
  }

  // State that is initialized during setup and is immutable during the pass
  // itself:

  Function& func;

  // Liveness information, including which Registers die at each Instr.
  LivenessAnalysis liveness;
  LivenessAnalysis::LastUses last_uses;

  // The number of bits in an initialized BorrowSupport, and the Register ->
  // bit assignments.
  size_t num_support_bits{AliasClass::kNumBits};
  std::unordered_map<Register*, int> reg_to_bit;

  // Information about Phi nodes, keyed by their input Registers.
  PhiUseMap phi_uses;

  // State that is initialized during the analysis phase and is unchanged
  // during the mutation phase:

  // In- and out-states for all blocks.
  std::unordered_map<BasicBlock*, BlockState> blocks;

  // Some functions are used by both the analysis and mutation phases and
  // perform nearly identically between the two, so this is used as a flag to
  // control the few behavioral differences that exist.
  bool mutate{false};

  // Transient state that is updated as instructions are processed:

  // Unused Phi outputs are collected here, and dropped in bulk after the last
  // Phi of the block.
  std::vector<Register*> deferred_deaths;

  // The state of all live Registers.
  StateMap live_regs;

  // All borrow support currently supporting live, borrowed Registers.
  BorrowSupport borrow_support;

  // All live registers that are currently supported by non-empty borrow
  // support.
  std::set<RegState*, RegStateLess> borrowed_regs;
};

struct PredState {
  PredState(BasicBlock* block, const StateMap* state)
      : block{block}, state{state} {}

  BasicBlock* block;
  const StateMap* state;
};

// Return a list of out-states for all visited predecessors of the given block,
// sorted by block id.
std::vector<PredState> collectPredStates(Env& env, BasicBlock* block) {
  std::vector<PredState> preds;
  for (auto edge : block->in_edges()) {
    auto pred = edge->from();
    auto it = env.blocks.find(pred);
    if (it == env.blocks.end()) {
      continue;
    }
    preds.emplace_back(pred, &it->second.out);
  }
  std::sort(preds.begin(), preds.end(), [](auto& p1, auto& p2) {
    return p1.block->id < p2.block->id;
  });
  return preds;
}

// Return true iff the given Register is definitely not a reference-counted
// value.
bool isUncounted(const Register* reg) {
  return !reg->type().couldBe(TMortalObject);
}

// Insert an Incref of `reg` before `cursor`.
void insertIncref(Env& env, Register* reg, Instr& cursor) {
  JIT_DCHECK(env.mutate, "Attempt to insert incref with mutate == false");
  JIT_DCHECK(!isUncounted(reg), "Attempt to incref an uncounted value");
  Instr* incref;
  if (reg->type() <= TObject) {
    incref = Incref::create(reg);
  } else {
    incref = XIncref::create(reg);
  }
  incref->copyBytecodeOffset(cursor);
  incref->InsertBefore(cursor);
  TRACE(
      "Inserted '%s' before '%s' in bb %d",
      *incref,
      cursor,
      cursor.block()->id);
}

// Insert a Decref or XDecref of `reg`, depending on its type, before `cursor`.
void insertDecref(Env& env, Register* reg, Instr& cursor) {
  JIT_DCHECK(env.mutate, "Attempt to insert decref with mutate == false");
  JIT_DCHECK(!isUncounted(reg), "Attempt to decref an uncounted value");
  Instr* decref;
  if (reg->type() <= TObject) {
    decref = Decref::create(reg);
  } else {
    decref = XDecref::create(reg);
  }
  decref->copyBytecodeOffset(cursor);
  decref->InsertBefore(cursor);
  TRACE(
      "Inserted '%s' before '%s' in bb %d",
      *decref,
      cursor,
      cursor.block()->id);
}

// If the given RegState is borrowed with non-empty support, track it in env.
void registerBorrowSupport(Env& env, RegState& rstate) {
  if (!rstate.isBorrowed() || rstate.support().empty()) {
    return;
  }
  env.borrow_support.add(rstate.support());
  env.borrowed_regs.emplace(&rstate);
}

// Invalidate the borrow support represented by either a bit index or an
// AliasClass, updating live value state and inserting Increfs to promote
// values to owned as appropriate.
template <typename Support>
void invalidateBorrowSupport(Env& env, Instr& cursor, Support support) {
  if (!env.borrow_support.intersects(support)) {
    return;
  }

  for (auto it = env.borrowed_regs.begin(); it != env.borrowed_regs.end();) {
    auto rstate = *it;
    JIT_DCHECK(
        rstate->isBorrowed(),
        "Non-borrowed state in borrowed_regs: %s",
        *rstate);
    if (rstate->support().intersects(support)) {
      rstate->setOwned();
      if (env.mutate) {
        insertIncref(env, rstate->current(), cursor);
      }
      it = env.borrowed_regs.erase(it);
    } else {
      ++it;
    }
  }

  env.borrow_support.remove(support);
}

// Kill a Register that has died after its last use. If that Register was the
// last live copy of its model, untrack it, and if we owned a reference to it,
// insert a Decref.
void killRegisterImpl(
    Env& env,
    RegState& rstate,
    Register* copy,
    Instr& cursor) {
  TRACE("Killing %s from %s", *copy, rstate);
  if (!rstate.killCopy(copy)) {
    // There are copies of this value still live.
    return;
  }

  Register* model = rstate.model();
  if (rstate.isOwned()) {
    // Before killing our owned reference, check for anyone borrowing from us.
    auto bit_it = env.reg_to_bit.find(model);
    if (bit_it != env.reg_to_bit.end()) {
      invalidateBorrowSupport(env, cursor, bit_it->second);
    }

    // Invalidate all managed-memory-backed borrow support, for two reasons:
    // 1. The Decref we're going to insert here can run arbitrary code in the
    //    destructor.
    // 2. The value we're losing a reference to could be a container supporting
    //    a borrowed value.
    // It's possible to do better in the future on both of these points, with
    // more complexity.
    invalidateBorrowSupport(env, cursor, AManagedHeapAny);
    if (env.mutate) {
      insertDecref(env, copy, cursor);
    }
  }

  env.borrowed_regs.erase(&rstate);
  env.live_regs.eraseModel(model);
}

// Kill a list of registers that have died, in an order that is predictable and
// avoids unecessary promotions from borrowed to owned.
void killRegisters(
    Env& env,
    const std::vector<Register*>& regs,
    Instr& cursor) {
  struct RegCopyState {
    RegCopyState(Register* copy, RegState* rstate)
        : copy(copy), rstate(rstate) {}

    Register* copy;
    RegState* rstate;
  };
  std::vector<RegCopyState> rstates;
  rstates.reserve(regs.size());
  for (Register* reg : regs) {
    rstates.emplace_back(reg, &map_get(env.live_regs, reg));
  }
  auto rstate_lt = [](RegCopyState& a, RegCopyState& b) {
    bool a_borrowed = a.rstate->isBorrowed();
    bool b_borrowed = b.rstate->isBorrowed();
    // Put borrowed registers before all others, and sort by register number
    // within each group.
    return (a_borrowed && !b_borrowed) ||
        (a_borrowed == b_borrowed && RegStateLess{}(a.rstate, b.rstate));
  };
  std::sort(rstates.begin(), rstates.end(), rstate_lt);
  for (RegCopyState& rcs : rstates) {
    killRegisterImpl(env, *rcs.rstate, rcs.copy, cursor);
  }
}

// Copy the given state into env, and re-initialize borrow support tracking
// from the new live values.
void useInState(Env& env, const StateMap& state) {
  env.live_regs = state;

  env.borrow_support.init(env.num_support_bits);
  env.borrowed_regs.clear();
  for (auto& pair : env.live_regs) {
    registerBorrowSupport(env, pair.second);
  }
}

// For a block with 0 or 1 predecessors, compute and activate its in-state. For
// the entry block, this is an empty map. For 1-predecessor blocks, it's a copy
// of the predecessor's out-state with adjustments for a CondBranch* in the
// predecessor and/or registers that died across the edge.
void useSimpleInState(Env& env, BasicBlock* block) {
  if (block->in_edges().empty()) {
    useInState(env, StateMap{});
    return;
  }

  JIT_DCHECK(
      block->in_edges().size() == 1,
      "Only blocks with <= 1 predecessors are supported");
  JIT_DCHECK(
      !block->front().IsPhi(),
      "Phis in a single-predecessor block are unsupported");

  BasicBlock* pred = (*block->in_edges().begin())->from();
  useInState(env, map_get(env.blocks, pred).out);

  // First, adjust for a conditional branch, if any, in the predecessor.
  Instr* term = pred->GetTerminator();
  if (term->IsCondBranch() || term->IsCondBranchIterNotDone()) {
    auto cond = static_cast<CondBranchBase*>(term);
    // The operand of the CondBranch is uncounted coming out of the false edge:
    // for CondBranch it's nullptr, and for CondBranchIterNotDone it's an
    // immortal sentinel.
    if (block == cond->false_bb()) {
      Register* reg = cond->GetOperand(0);
      map_get(env.live_regs, reg).setUncounted();
    }
  } else if (term->IsCondBranchCheckType()) {
    // Ci_PyWaitHandleObject is an uncounted singleton, so we adjust its
    // reference state here to avoid refcounting it.
    auto cond = static_cast<CondBranchCheckType*>(term);
    if (cond->type() == TWaitHandle) {
      if (block == cond->true_bb()) {
        Register* reg = cond->GetOperand(0);
        map_get(env.live_regs, reg).setUncounted();
      }
    }
  }

  // Second, kill any registers that die across the edge.
  RegisterSet live_in = env.liveness.GetIn(block);
  std::vector<Register*> dying_values;
  for (auto& pair : env.live_regs) {
    RegState& rstate = pair.second;
    for (int i = rstate.numCopies() - 1; i >= 0; --i) {
      Register* reg = rstate.copy(i);
      if (!live_in.count(reg)) {
        dying_values.emplace_back(reg);
      }
    }
  }

  killRegisters(env, dying_values, block->front());
}

// The first time we see a block with multiple predecessors, populate its
// in-state with all live-in registers and Phi outputs, with their copy lists
// appropriately initialized.
void initializeInState(
    BasicBlock* block,
    StateMap& in_state,
    const RegisterSet& live_in,
    const StateMap& pred_state) {
  for (auto current : live_in) {
    auto model = modelReg(current);
    auto in_pair = in_state.emplace(model, model);
    if (!in_pair.second) {
      // We already processed this value with a copy we saw earlier.
      continue;
    }

    auto& rstate = in_pair.first->second;
    // Clear the list of copies since we're initializing it manually.
    rstate.killCopy(model);

    // Using an arbitrary predecessor to get definition order, insert any
    // copies that are still live into this block.
    auto& pred_rstate = pred_state.getModel(model);
    for (int i = 0, n = pred_rstate.numCopies(); i < n; ++i) {
      auto copy = pred_rstate.copy(i);
      if (live_in.count(copy)) {
        rstate.addCopy(copy);
      }
    }
  }

  block->forEachPhi([&](Phi& phi) {
    auto inserted = in_state.emplace(phi.GetOutput(), phi.GetOutput()).second;
    JIT_DCHECK(inserted, "Register shouldn't exist in map yet");
  });
}

// Return true iff the given register is live into the given block, in the
// given in-state. Phi outputs are not live into the block they're defined in,
// even though they appear in the in-state.
bool isLiveIn(BasicBlock* block, Register* reg, const StateMap& in_state) {
  if (reg->instr()->IsPhi() && reg->instr()->block() == block) {
    return false;
  }
  return in_state.countModel(reg);
}

struct PhiInput {
  PhiInput(BasicBlock* block, const RegState* rstate)
      : block{block}, rstate{rstate} {}

  BasicBlock* block;
  const RegState* rstate;
};

// Return a list of predecessor blocks paired with the RegState for the value
// they provide to the given Phi. This relies on the output of
// collectPredStates() being sorted in the same order as Phi::basic_blocks, by
// block id.
std::vector<PhiInput> collectPhiInputs(
    const std::vector<PredState>& preds,
    const Phi& phi) {
  std::vector<PhiInput> inputs;
  auto preds_it = preds.begin();
  for (std::size_t phi_idx = 0; preds_it != preds.end(); ++phi_idx) {
    auto& pred = *preds_it;
    if (phi.basic_blocks().at(phi_idx) != pred.block) {
      // This predecessor hasn't been processed yet.
      continue;
    }
    auto input = phi.GetOperand(phi_idx);
    inputs.emplace_back(pred.block, &map_get(*pred.state, input));
    ++preds_it;
  }
  JIT_DCHECK(!inputs.empty(), "Processing block with no visited predecessors");
  return inputs;
}

// Information about Phi instructions: a set of owned Phi inputs that aren't
// separately live into the block, and a map of which Phi outputs those dead
// inputs could forward their owned reference to.
//
// Used to modify the support of values borrowed from the dead inputs, so we
// only borrow references from live values.
struct PhiSupport {
  PhiSupport(size_t support_bits) {
    dead.init(support_bits);
  }

  BorrowSupport dead;
  std::unordered_map<size_t, BorrowSupport> forwards;
};

// For each Phi in the given block, inspect the state of all incoming values
// and decide on a merged state for the Phi's output.
PhiSupport processPhis(
    Env& env,
    BasicBlock* block,
    const std::vector<PredState>& preds,
    StateMap& in_state) {
  PhiSupport support_info(env.num_support_bits);

  for (auto& instr : *block) {
    if (!instr.IsPhi()) {
      break;
    }

    auto& phi = static_cast<Phi&>(instr);
    auto output = phi.GetOutput();
    auto& rstate = in_state.getModel(output);

    // No more analysis is needed if the value isn't refcounted, or if it's
    // already owned.
    if (isUncounted(output) || rstate.isOwned()) {
      continue;
    }

    auto inputs = collectPhiInputs(preds, phi);

    // Dead phi inputs with an owned reference force the phi output to be
    // owned. We also keep track of which Phi outputs these owned references
    // are forwarded into, so borrow support that depends on the now-dead
    // registers can be updated.
    bool promote_output = false;
    for (PhiInput& input : inputs) {
      Register* model = input.rstate->model();
      if (!isLiveIn(block, model, in_state) && input.rstate->isOwned()) {
        promote_output = true;

        size_t model_bit = map_get(env.reg_to_bit, model);
        support_info.dead.add(model_bit);
        auto forward_pair =
            support_info.forwards.emplace(model_bit, BorrowSupport{});
        if (forward_pair.second) {
          forward_pair.first->second.init(env.num_support_bits);
        }
        forward_pair.first->second.add(map_get(env.reg_to_bit, output));
        TRACE("Forwarding support from dead %s to %s", *model, *output);
      }
    }
    if (promote_output) {
      rstate.setOwned();
      continue;
    }

    // Otherwise, the phi's output is borrowed from its owned inputs and the
    // borrow support of its borrowed inputs.
    rstate.setBorrowed(env.num_support_bits);
    for (auto& input : inputs) {
      if (input.rstate->isOwned()) {
        rstate.support().add(map_get(env.reg_to_bit, input.rstate->model()));
      } else if (input.rstate->isBorrowed()) {
        // TODO(bsimmers): If this input gets promoted to owned because of a
        // loop, the borrow support we add here will be redundant and could
        // result in worse results. We should revisit this at some point, but
        // it's never incorrect to add more borrow support and fixing this gets
        // messy.
        rstate.support().add(input.rstate->support());
      }
    }
  }

  return support_info;
}

// Update the in-state for the given block, leaving the result in both
// env.live_regs and env.blocks[block].in.
void updateInState(Env& env, BasicBlock* block) {
  if (block->in_edges().size() <= 1) {
    useSimpleInState(env, block);
    return;
  }

  auto preds = collectPredStates(env, block);
  auto live_in = env.liveness.GetIn(block);
  auto block_pair = env.blocks.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(block),
      std::forward_as_tuple());
  auto& in_state = block_pair.first->second.in;

  if (block_pair.second) {
    initializeInState(block, in_state, live_in, *preds[0].state);
  }

  PhiSupport phi_support = processPhis(env, block, preds, in_state);

  for (auto& pair : in_state) {
    auto& rstate = pair.second;
    auto model = rstate.model();
    if (isUncounted(rstate.current()) || rstate.isOwned()) {
      continue;
    }

    if (!(model->instr()->IsPhi() && model->instr()->block() == block)) {
      for (auto& pred : preds) {
        rstate.merge(pred.state->getModel(model));
        if (rstate.isOwned()) {
          break;
        }
      }
    }

    // If the value is borrowed from one or more now-dead Phi inputs, change it
    // to borrow from the corresponding Phi output(s) instead.
    if (rstate.isBorrowed() && rstate.support().intersects(phi_support.dead)) {
      for (auto pair : phi_support.forwards) {
        if (rstate.support().intersects(pair.first)) {
          rstate.support().remove(pair.first);
          rstate.support().add(pair.second);
        }
      }
    }
  }

  useInState(env, in_state);
}

// If the given instruction can deopt, fill in its live registers.
void fillDeoptLiveRegs(const StateMap& live_regs, Instr& instr) {
  auto deopt = instr.asDeoptBase();
  if (deopt == nullptr) {
    return;
  }

  for (auto& pair : live_regs) {
    auto& rstate = pair.second;
    auto ref_kind = rstate.kind();
    for (int i = 0, n = rstate.numCopies(); i < n; ++i) {
      Register* reg = rstate.copy(i);
      deopt->emplaceLiveReg(reg, ref_kind, deoptValueKind(reg->type()));
      if (ref_kind == RefKind::kOwned) {
        // Treat anything other than the first copy as borrowed, to avoid
        // over-decrefing it. We can probably do better in the future by
        // ensuring that we only ever have one copy of each value in the
        // FrameState/live regs, but that's a more disruptive change.
        ref_kind = RefKind::kBorrowed;
      }
    }
  }
}

// Process any operands stolen by the given instruction.
void stealInputs(
    Env& env,
    Instr& instr,
    const util::BitVector& stolen_inputs,
    const RegisterSet& dying_regs) {
  if (stolen_inputs.GetPopCount() == 0) {
    return;
  }

  for (int i = 0, n = instr.NumOperands(); i < n; ++i) {
    if (!stolen_inputs.GetBit(i)) {
      continue;
    }

    auto reg = instr.GetOperand(i);
    auto& rstate = map_get(env.live_regs, reg);
    if (rstate.isOwned() && dying_regs.count(reg)) {
      // This instruction is the last use of reg and we own a reference to it,
      // so forward the reference to the instruction. Mark the value as
      // borrowed to avoid forwarding this reference more than once in this
      // loop, and it will be killed later in processInstr().
      rstate.setBorrowed(env.num_support_bits);
      continue;
    }
    if (env.mutate && !rstate.isUncounted()) {
      insertIncref(env, reg, instr);
    }
  }
}

// Track the output of the given instruction.
void processOutput(Env& env, const Instr& instr, const MemoryEffects& effects) {
  auto output = instr.GetOutput();
  if (output == nullptr) {
    return;
  }

  // Even though GuardIs is a passthrough, it verifies that a runtime value is a
  // specific object, breaking the dependency on the instruction that produced
  // the runtime value
  if (isPassthrough(instr) && !instr.IsGuardIs()) {
    auto& rstate = map_get(env.live_regs, output);
    rstate.addCopy(output);
    if (isUncounted(output)) {
      rstate.setUncounted();
    }
    return;
  }

  auto pair = env.live_regs.emplace(output, output);
  JIT_DCHECK(pair.second, "Register %s already defined", output->name());
  auto& rstate = pair.first->second;
  if (isUncounted(output)) {
    // Do nothing. rstate is already Uncounted by default.
  } else if (effects.borrows_output) {
    rstate.setBorrowed(env.num_support_bits);
    rstate.support().add(effects.borrow_support);
    registerBorrowSupport(env, rstate);
  } else {
    rstate.setOwned();
  }
}

// Process the given instruction: handle its memory effects, stolen inputs,
// output, and any registers that die after it.
void processInstr(Env& env, Instr& instr) {
  JIT_DCHECK(
      !instr.IsIncref() && !instr.IsDecref() && !instr.IsXDecref() &&
          !instr.IsSnapshot(),
      "Unsupported instruction %s",
      instr.opname());

  if (instr.numEdges() > 0) {
    // Branches are handled outside the main loop.
    return;
  }

  auto last_uses_it = env.last_uses.find(&instr);
  auto& dying_regs =
      last_uses_it == env.last_uses.end() ? kEmptyRegSet : last_uses_it->second;

  TRACE("Processing '%s' with state:\n%s", instr, env.live_regs);
  if (!dying_regs.empty()) {
    TRACE("dying_regs: %s", dying_regs);
  }

  if (instr.IsPhi()) {
    // If a Phi output is unused, it will die immediately after the Phi that
    // defines it. It's illegal to insert a Decref between Phis, so we collect
    // any such Registers to Decref together after the last Phi in the block.
    if (!dying_regs.empty()) {
      JIT_DCHECK(dying_regs.size() == 1, "Multiple regs dying after Phi");
      auto output = instr.GetOutput();
      JIT_DCHECK(
          *dying_regs.begin() == output, "Unexpected value dying after Phi");
      env.deferred_deaths.emplace_back(output);
    }

    auto& next = *std::next(instr.block()->iterator_to(instr));
    if (!next.IsPhi() && !env.deferred_deaths.empty()) {
      killRegisters(env, env.deferred_deaths, next);
      env.deferred_deaths.clear();
    }
    return;
  }

  auto effects = memoryEffects(instr);
  invalidateBorrowSupport(env, instr, effects.may_store);
  stealInputs(env, instr, effects.stolen_inputs, dying_regs);

  if (instr.IsReturn()) {
    JIT_DCHECK(env.live_regs.size() == 1, "Unexpected live value(s) at Return");
    JIT_DCHECK(
        !map_get(env.live_regs, instr.GetOperand(0)).isOwned(),
        "Return operand should not be owned at exit");
    return;
  }

  if (env.mutate) {
    fillDeoptLiveRegs(env.live_regs, instr);
  }

  if (instr.IsTerminator()) {
    return;
  }

  processOutput(env, instr, effects);

  auto& next_instr = *std::next(instr.block()->iterator_to(instr));
  killRegisters(env, {dying_regs.begin(), dying_regs.end()}, next_instr);
}

// When leaving a block with one successor, insert any Increfs necessary to
// transition to the target state.
void exitBlock(Env& env, const Edge* out_edge) {
  auto block = out_edge->from();
  auto succ = out_edge->to();
  if (succ->in_edges().size() == 1) {
    // No reconciliation is needed on 1:1 edges.
    return;
  }
  auto const& from_regs = env.live_regs;
  auto const& to_regs = map_get(env.blocks, succ).in;
  TRACE("Reconciling to in-state for bb %d:\n%s", succ->id, to_regs);

  // Count the number of increfs we need for each value.
  std::vector<std::pair<Register*, int>> reg_increfs;
  for (auto& pair : from_regs) {
    auto model = pair.first;
    auto& from_rstate = pair.second;
    if (from_rstate.isUncounted()) {
      // Like in enterBlock(), sending an uncounted value to any other state
      // never needs an adjustment.
      continue;
    }

    bool to_owned = [&] {
      if (!isLiveIn(succ, model, to_regs)) {
        return false;
      }
      return to_regs.getModel(model).isOwned();
    }();

    // Start by calculating the number of increfs needed to reconcile the out
    // state to the in state. This may begin as -1 if the out state is Owned
    // and the in state isn't, in which case the outgoing owned reference will
    // be transferred to a Phi dest.
    int increfs = to_owned - from_rstate.isOwned();

    // Add an incref for every time this value is passed to an owned Phi output.
    auto phi_it = env.phi_uses.find(model);
    if (phi_it != env.phi_uses.end()) {
      auto block_it = phi_it->second.find(block);
      if (block_it != phi_it->second.end()) {
        for (auto phi_output : block_it->second) {
          increfs += to_regs.getModel(phi_output).isOwned();
        }
      }
    }

    if (increfs > 0) {
      reg_increfs.emplace_back(from_rstate.current(), increfs);
    } else {
      JIT_DCHECK(increfs == 0, "Invalid state transition");
    }
  }

  std::sort(reg_increfs.begin(), reg_increfs.end(), [](auto& a, auto& b) {
    return RegisterLess{}(a.first, b.first);
  });
  auto& cursor = block->back();
  for (auto& pair : reg_increfs) {
    for (int i = 0; i < pair.second; ++i) {
      // If we see long strings of Increfs being inserted in real code by this,
      // we should either figure out if we can optimize code like that better,
      // or create a variant of Incref that adds more than 1 to the refcount.
      insertIncref(env, pair.first, cursor);
    }
  }
}

// Bind guards to their dominating FrameState, and remove all Snapshot
// instructions since they're no longer needed. We run this before refcount
// insertion to ensure that Snapshots don't keep values alive longer than
// necessary.
void bindGuards(Function& irfunc) {
  std::vector<Instr*> snapshots;
  for (auto& block : irfunc.cfg.blocks) {
    FrameState* fs = nullptr;
    for (auto& instr : block) {
      if (instr.IsSnapshot()) {
        auto& snapshot = static_cast<const Snapshot&>(instr);
        fs = snapshot.frameState();
        snapshots.emplace_back(&instr);
      } else if (
          instr.IsGuard() || instr.IsGuardIs() || instr.IsGuardType() ||
          instr.IsDeopt() || instr.IsDeoptPatchpoint()) {
        JIT_DCHECK(
            fs != nullptr,
            "No dominating snapshot for '%s' in function:\n%s",
            instr,
            irfunc);
        auto& guard = static_cast<DeoptBase&>(instr);
        guard.setFrameState(*fs);
      } else if (!instr.isReplayable()) {
        fs = nullptr;
      }
    }
  }
  for (auto& snapshot : snapshots) {
    snapshot->unlink();
    delete snapshot;
  }
  DeadCodeElimination{}.Run(irfunc);
}

std::ostream& operator<<(std::ostream& os, const RegState& rstate) {
  os << "RegState{[";
  auto sep = "";
  for (int i = 0, n = rstate.numCopies(); i < n; ++i) {
    fmt::print(os, "{}{}", sep, rstate.copy(i)->name());
    sep = ", ";
  }
  fmt::print(os, "], {}", rstate.kind());
  if (rstate.isBorrowed() && !rstate.support().empty()) {
    fmt::print(os, " {}", rstate.support().bits());
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const StateMap& regs) {
  if (regs.empty()) {
    return os << "StateMap[0] = {}";
  }

  std::vector<const RegState*> states;
  for (auto& pair : regs) {
    states.emplace_back(&pair.second);
  }
  std::sort(states.begin(), states.end(), RegStateLess{});
  fmt::print(os, "StateMap[{}] = {{\n", states.size());
  for (auto state : states) {
    fmt::print(os, "  {} -> {}\n", state->model()->name(), *state);
  }
  return os << "}";
}

void optimizeLongDecrefRuns(Function& irfunc) {
  constexpr int kMinimumNumberOfDecrefsToOptimize = 4;

  auto get_number_of_decrefs = [](auto block, auto cur_iter) {
    int result = 0;
    while (cur_iter != block->end()) {
      if (!cur_iter->IsDecref()) {
        break;
      }
      result++;
      ++cur_iter;
    }
    return result;
  };

  for (auto& block : irfunc.cfg.GetRPOTraversal()) {
    auto cur_iter = block->begin();

    while (cur_iter != block->end()) {
      if (!cur_iter->IsDecref()) {
        ++cur_iter;
        continue;
      }

      int num = get_number_of_decrefs(block, cur_iter);
      if (num < kMinimumNumberOfDecrefsToOptimize) {
        std::advance(cur_iter, num);
        continue;
      }

      auto batch_decref = BatchDecref::create(num);
      batch_decref->InsertBefore(*cur_iter);

      constexpr size_t kDecrefOperandIndex = 0;
      for (int i = 0; i < num; i++) {
        JIT_CHECK(
            cur_iter->IsDecref(),
            "An unexpected non-decref instruction in a decref run.");

        batch_decref->SetOperand(i, cur_iter->GetOperand(kDecrefOperandIndex));
        auto old_instr = cur_iter++;
        old_instr->unlink();
        delete &(*old_instr);
      }
    }
  }
}

} // namespace

void RefcountInsertion::Run(Function& func) {
  PhiElimination{}.Run(func);
  bindGuards(func);
  func.cfg.splitCriticalEdges();

  TRACE(
      "Starting refcount insertion for '%s':\n%s",
      func.fullname,
      HIRPrinter(true).ToString(func));
  Env env{func};

  auto rpo_blocks = func.cfg.GetRPOTraversal();
  Worklist<BasicBlock*> worklist;
  for (auto block : rpo_blocks) {
    worklist.push(block);
  }

  while (!worklist.empty()) {
    auto block = worklist.front();
    worklist.pop();

    updateInState(env, block);

    TRACE("\nAnalyzing bb %d with in-state:\n%s", block->id, env.live_regs);
    for (auto& instr : *block) {
      processInstr(env, instr);
    }

    TRACE("Finished bb %d with out-state:\n%s", block->id, env.live_regs);
    auto& block_state = env.blocks[block];
    if (env.live_regs != block_state.out) {
      block_state.out = std::move(env.live_regs);
      for (auto edge : block->out_edges()) {
        worklist.push(edge->to());
      }
    }
  }

  TRACE("\nStarting mutation phase");
  env.mutate = true;
  for (auto block : rpo_blocks) {
    // Remember first_instr here to skip any (Inc|Dec)Refs inserted by
    // useSimpleInState().
    auto& first_instr = block->front();
    if (block->in_edges().size() <= 1) {
      useSimpleInState(env, block);
    } else {
      useInState(env, map_get(env.blocks, block).in);
    }

    TRACE("\nEntering bb %d with state:\n%s", block->id, env.live_regs);

    for (auto it = block->iterator_to(first_instr); it != block->end();) {
      auto& instr = *it;
      // Increment it before calling processInstr() to skip any Decrefs
      // inserted after instr.
      ++it;
      processInstr(env, instr);
    }

    TRACE("Leaving bb %d with state:\n%s", block->id, env.live_regs);
    if (block->out_edges().size() == 1) {
      exitBlock(env, *block->out_edges().begin());
    }
  }

  // Clean up any trampoline blocks that weren't necessary.
  // TODO(emacs): Investigate running the whole CleanCFG pass here or between
  // every pass.
  CleanCFG::RemoveTrampolineBlocks(&func.cfg);

  // Optimize long decref runs
  optimizeLongDecrefRuns(func);
}

} // namespace jit::hir
