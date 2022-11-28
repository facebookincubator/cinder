// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/bitvector.h"
#include "Jit/codegen/copy_graph.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/containers.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/operand.h"
#include "Jit/log.h"
#include "Jit/util.h"

#include <list>
#include <memory>
#include <ostream>
#include <queue>
#include <utility>

namespace jit::lir {

// This header file contains classes implementing linear scan register
// allocation. The algorithm employed is based on papers "Linear Scan Register
// Allocation on SSA Form" and "Optimized Interval Splitting in a Linear Scan
// Register Allocator" by C. Wimmer, et al. A location in LIR is represented by
// a basic block index and an instruction index pair. A range in LIR is
// represented by a two locations - start and end. All the ranges are
// half-opened, with start included and end excluded. A interval in LIR is
// composed of a set of ranges.

struct RegallocBlockState {
  const lir::BasicBlock* bb;
  int block_start_index;
  // the first instruction of the basic block before rewrite.
  lir::Instruction* block_first_instr;
  UnorderedSet<const lir::Operand*> livein;

  RegallocBlockState(
      const lir::BasicBlock* b,
      int index,
      lir::Instruction* instr)
      : bb(b), block_start_index(index), block_first_instr(instr) {}
};

// Location index in LIR.
using LIRLocation = int;

constexpr LIRLocation START_LOCATION = 0;
constexpr LIRLocation INVALID_LOCATION = -1;
constexpr LIRLocation MAX_LOCATION = std::numeric_limits<LIRLocation>::max();

struct LiveRange {
  LiveRange(LIRLocation s, LIRLocation e) : start(s), end(e) {
    JIT_CHECK(s < e, "Invalid live range.");
  }

  LIRLocation start;
  LIRLocation end;

  bool isInRange(const LIRLocation& loc) const {
    return loc >= start && loc < end;
  }

  bool intersectsWith(const LiveRange& lr) const;
};

struct LiveInterval {
  explicit LiveInterval(const lir::Operand* vr) : vreg(vr) {}
  LiveInterval(const lir::Operand* vr, PhyLocation loc)
      : vreg(vr), allocated_loc(loc) {}

  const lir::Operand* vreg;

  struct LiveRangeCompare {
    // make it searchable by LIRLocation
    using is_transparent = void;

    bool operator()(const LiveRange& lhs, const LiveRange& rhs) const {
      return lhs.start < rhs.start;
    }
    bool operator()(const LiveRange& lhs, LIRLocation rhs) const {
      return lhs.start < rhs;
    }
    bool operator()(LIRLocation lhs, const LiveRange& rhs) const {
      return lhs < rhs.start;
    }
  };

  std::vector<LiveRange> ranges;
  PhyLocation allocated_loc{PhyLocation::REG_INVALID};
  bool fixed{false}; // whether the allocated_loc is fixed.

  void addRange(LiveRange range);
  void setFrom(LIRLocation loc);
  LIRLocation startLocation() const {
    JIT_DCHECK(
        !ranges.empty(), "Cannot get start location for an empty interval.");
    return ranges.begin()->start;
  }

  LIRLocation endLocation() const {
    JIT_DCHECK(
        !ranges.empty(), "Cannot get end location for an empty interval.");
    return ranges.rbegin()->end;
  }

  bool covers(LIRLocation loc) const;
  bool isEmpty() const {
    return ranges.empty();
  }

  // the two functions below return the first intersect point with a
  // LiveRange or LiveInterval. If they are disjioint, return
  // INVALID_LOCATION.
  LIRLocation intersectWith(const LiveRange& range) const;
  LIRLocation intersectWith(const LiveInterval& interval) const;

  // split the current interval at location loc. After splitting, the
  // current object takes the first part of the original interval, and
  // the function returns a LiveInterval object pointer pointing to the second
  // part of the original interval. The new LiveInterval (second part)
  // starts either from loc (if loc falls into a LiveRange of the original
  // LiveInterval), or from the next LiveRange after loc (if loc falls outside
  // any LiveRange of the original LiveInterval).
  // If the current interval cannot be splitted at location loc, return nullptr.
  std::unique_ptr<LiveInterval> splitAt(LIRLocation loc);

  void allocateTo(PhyLocation loc) {
    allocated_loc = loc;
  }

  bool isAllocated() const {
    return allocated_loc != PhyLocation::REG_INVALID;
  }

  bool isRegisterAllocated() const {
    return isAllocated() && allocated_loc.is_register();
  }
};

// The linear scan allocator.
// The register allocator works in four steps:
//   1. reorder the basic blocks in RPO order,
//   2. calculate liveness intervals and use locations,
//   3. linear scan and allocate registers,
//   4. rewrite the original LIR.
class LinearScanAllocator {
 public:
  explicit LinearScanAllocator(
      lir::Function* func,
      int reserved_stack_space = 0)
      : func_(func), initial_max_stack_slot_(-reserved_stack_space) {}
  void run();

  jit::codegen::PhyRegisterSet getChangedRegs() const {
    return changed_regs_;
  }

  int getSpillSize() const {
    return -max_stack_slot_;
  }

  int initialYieldSpillSize() const;

  // returns true if the variables defined in the entry block is
  // used in the function.
  bool isPredefinedUsed(const lir::Operand* operand) const;

 private:
  lir::Function* func_;
  UnorderedMap<const lir::Operand*, LiveInterval> vreg_interval_;
  UnorderedMap<const lir::Operand*, OrderedSet<LIRLocation>> vreg_phy_uses_;
  UnorderedMap<const lir::BasicBlock*, RegallocBlockState> regalloc_blocks_;
  // collect the last uses for all the vregs
  // key: def operand
  // value: a map with key: the use operand
  //                   value: use location
  UnorderedMap<
      const lir::Operand*,
      UnorderedMap<const lir::LinkedOperand*, LIRLocation>>
      vreg_last_use_;

  // the global last use of an operand (vreg)
  UnorderedMap<const lir::Operand*, LIRLocation> vreg_global_last_use_;

  int initial_max_stack_slot_;
  // stack slot number always starts from -8, and it's up to the code generator
  // to translate stack slot number into the form of (RBP - offset).
  int max_stack_slot_;
  std::vector<int> free_stack_slots_;

  jit::codegen::PhyRegisterSet changed_regs_;
  int initial_yield_spill_size_{-1};

  LiveInterval& getIntervalByVReg(const lir::Operand* vreg) {
    return vreg_interval_.emplace(vreg, vreg).first->second;
  }

  void printAllIntervalsByVReg(const lir::Operand* vreg) const;
  void printAllVregIntervals() const;

  void sortBasicBlocks();
  void initialize();
  void calculateLiveIntervals();

  void spillRegistersForYield(int instr_id);
  void reserveCallerSaveRegisters(int instr_id);
  void reserveRegisters(int instr_id, jit::codegen::PhyRegisterSet phy_regs);

  struct LiveIntervalPtrGreater {
    bool operator()(const LiveInterval* lhs, const LiveInterval* rhs) const {
      const auto& lhs_start = lhs->startLocation();
      const auto& rhs_start = rhs->startLocation();
      return rhs_start < lhs_start;
    }
  };

  using UnhandledQueue = std::priority_queue<
      LiveInterval*,
      std::vector<LiveInterval*>,
      LiveIntervalPtrGreater>;

  std::vector<std::unique_ptr<LiveInterval>> allocated_;

  // record vreg-to-physical-location mapping at the end of each basic block,
  // which is needed for resolve edges.
  UnorderedMap<
      const lir::BasicBlock*,
      UnorderedMap<const lir::Operand*, const LiveInterval*>>
      bb_vreg_end_mapping_;

  void linearScan();
  bool tryAllocateFreeReg(
      LiveInterval* current,
      UnorderedSet<LiveInterval*>& active,
      UnorderedSet<LiveInterval*>& inactive,
      UnhandledQueue& unhandled);
  void allocateBlockedReg(
      LiveInterval* current,
      UnorderedSet<LiveInterval*>& active,
      UnorderedSet<LiveInterval*>& inactive,
      UnhandledQueue& unhandled);
  LIRLocation getUseAtOrAfter(const lir::Operand* vreg, LIRLocation loc) const;

  // split at loc and save the new interval to unhandled and allocated_
  void
  splitAndSave(LiveInterval* interval, LIRLocation loc, UnhandledQueue& queue);
  static void markDisallowedRegisters(std::vector<LIRLocation>& locs);

  // map operand to stack slot upon spilling
  UnorderedMap<const lir::Operand*, int> operand_to_slot_;
  int getStackSlot(const lir::Operand* operand);
  void freeStackSlot(const lir::Operand* operand) {
    int slot = map_get(operand_to_slot_, operand, 0);
    JIT_DCHECK(slot < 0, "should not map an operand to a register");

    operand_to_slot_.erase(operand);
    free_stack_slots_.push_back(slot);
  }

  void rewriteLIR();
  void rewriteInstrOutput(
      lir::Instruction* instr,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping,
      const UnorderedSet<const lir::LinkedOperand*>* last_use_vregs);
  void rewriteInstrInputs(
      lir::Instruction* instr,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping,
      const UnorderedSet<const lir::LinkedOperand*>* last_use_vregs);
  void rewriteInstrOneInput(
      lir::Instruction* instr,
      size_t i,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping,
      const UnorderedSet<const lir::LinkedOperand*>* last_use_vregs);
  void rewriteInstrOneIndirectOperand(
      lir::MemoryIndirect* indirect,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping,
      const UnorderedSet<const lir::LinkedOperand*>* last_use_vregs);
  void rewriteInstrInputs(
      lir::Instruction* instr,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping);
  void rewriteInstrOneInput(
      lir::Instruction* instr,
      size_t i,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping);
  void rewriteInstrOneIndirectOperand(
      lir::MemoryIndirect* indirect,
      const UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping);

  void computeInitialYieldSpillSize(
      const UnorderedMap<const Operand*, const LiveInterval*>& mapping);

  using CopyGraphWithOperand =
      jit::codegen::CopyGraphWithType<const lir::OperandBase::DataType>;

  // update virtual register to physical register mapping.
  // if the mapping is changed for a virtual register and copies is not nullptr,
  // insert a copy to copies for CopyGraph to generate a MOV instruction.
  void rewriteLIRUpdateMapping(
      UnorderedMap<const lir::Operand*, const LiveInterval*>& mapping,
      LiveInterval* interval,
      CopyGraphWithOperand* copies);
  // emit copies before instr_iter
  void rewriteLIREmitCopies(
      lir::BasicBlock* block,
      lir::BasicBlock::InstrList::iterator instr_iter,
      std::unique_ptr<CopyGraphWithOperand> copies);

  void resolveEdges();
  std::unique_ptr<CopyGraphWithOperand> resolveEdgesGenCopies(
      const lir::BasicBlock* basicblock,
      const lir::BasicBlock* successor,
      std::vector<LiveInterval*>& intervals);

  /* this function allocates (up to two) basic blocks for conditional branch and
   * connects them as shown below:
   *
   *          +---------------------------+
   *          | jump_if_zero              |
   *          |                           v
   *  <basic_block> ----> <new_bb1>  <new_bb2>
   *                          |           |
   *                          |           +------> bb2
   *                          +------------------> bb1
   */
  void resolveEdgesInsertBasicBlocks(
      lir::BasicBlock* basic_block,
      lir::BasicBlock* next_basic_block,
      lir::BasicBlock* true_bb,
      lir::BasicBlock* false_bb,
      std::unique_ptr<CopyGraphWithOperand> true_copies,
      std::unique_ptr<CopyGraphWithOperand> false_copies);

  FRIEND_TEST(LinearScanAllocatorTest, RegAllocationNoSpill);
  FRIEND_TEST(LinearScanAllocatorTest, RegAllocation);
};

std::ostream& operator<<(std::ostream& out, const LiveRange& rhs);
std::ostream& operator<<(std::ostream& out, const LiveInterval& rhs);

} // namespace jit::lir
