// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/compiler.h"
#include "cinderx/Jit/lir/operand.h"
#include "cinderx/Jit/lir/parser.h"
#include "cinderx/Jit/lir/printer.h"
#include "cinderx/Jit/lir/regalloc.h"

#include <fmt/ostream.h>

#include <algorithm>
#include <sstream>
#include <vector>

using namespace jit;

namespace jit::lir {
class LinearScanAllocatorTest : public ::testing::Test {
 public:
  static bool LiveIntervalPtrLess(
      const LiveInterval* lhs,
      const LiveInterval* rhs) {
    if (lhs->vreg == rhs->vreg) {
      return lhs->startLocation() < rhs->startLocation();
    }
    return lhs->vreg < rhs->vreg;
  }

  UnorderedMap<const Operand*, int> buildOperandToIndexMap(
      const UnorderedMap<int, Instruction*>& map) {
    UnorderedMap<const Operand*, int> res;
    for (auto& m : map) {
      res.emplace(m.second->output(), m.first);
    }
    return res;
  }

  template <typename T>
  UnorderedMap<int, T> buildIndexMap(
      const UnorderedMap<const Operand*, T>& opnd_interval,
      const UnorderedMap<const Operand*, int>& opnd_index) {
    UnorderedMap<int, T> res;
    for (auto& oi : opnd_interval) {
      auto iter = opnd_index.find(oi.first);

      if (iter == opnd_index.end()) {
        // cannot find an index for an operand, clear the result
        // and make the caller assert.
        res.clear();
        break;
      }

      res.emplace(iter->second, oi.second);
    }

    return res;
  }

  std::unique_ptr<LinearScanAllocator> runAllocator(Function* func) {
    auto allocator = std::make_unique<LinearScanAllocator>(func);
    allocator->run();
    return allocator;
  }
};

TEST_F(LinearScanAllocatorTest, IntervalIntersectWithRange) {
  LiveInterval i1(nullptr);
  i1.addRange(LiveRange{10, 30});
  i1.addRange(LiveRange{40, 60});

  EXPECT_EQ(i1.intersectWith(LiveRange{0, 10}), INVALID_LOCATION);
  EXPECT_EQ(i1.intersectWith(LiveRange{10, 11}), 10);
  EXPECT_EQ(i1.intersectWith(LiveRange{20, 50}), 20);
  EXPECT_EQ(i1.intersectWith(LiveRange{35, 45}), 40);
  EXPECT_EQ(i1.intersectWith(LiveRange{40, 50}), 40);
  EXPECT_EQ(i1.intersectWith(LiveRange{50, 70}), 50);
  EXPECT_EQ(i1.intersectWith(LiveRange{60, 65}), INVALID_LOCATION);
}

TEST_F(LinearScanAllocatorTest, IntervalIntersectWithInterval) {
  LiveInterval i1(nullptr);
  LiveInterval i2(nullptr);

  EXPECT_EQ(i1.intersectWith(i2), INVALID_LOCATION);
  EXPECT_EQ(i2.intersectWith(i1), INVALID_LOCATION);

  i1.addRange(LiveRange{20, 50});
  i2.addRange(LiveRange{10, 30});
  i2.addRange(LiveRange{40, 60});
  EXPECT_EQ(i1.intersectWith(i2), 20);
  EXPECT_EQ(i2.intersectWith(i1), 20);

  // Make sure the results are unchanged if i1 has more ranges than i2.
  i1.addRange(LiveRange{100, 200});
  i1.addRange(LiveRange{300, 400});
  EXPECT_EQ(i1.intersectWith(i2), 20);
  EXPECT_EQ(i2.intersectWith(i1), 20);
}

TEST_F(LinearScanAllocatorTest, RegAllocationNoSpill) {
  const char* lir_source = R"(
Function:
BB %0 - succs: %2
      %1 = Move 0(0x0)
           Branch BB%2

BB %2 - succs: %5 %8
      %3 = Add %1, 8(0x8)
           CondBranch %3, BB%5, BB%8

BB %5 - succs: %11
      %6 = Add %1, 8(0x8)
           Branch BB%11

BB %8 - succs: %11
      %9 = Add %1, 16(0x10)
           Branch BB%11

BB %11 - succs: %14
     %12 = Phi (BB%5, %6), (BB%8, %9)
           Return %12

BB %14

)";

  Parser parser;
  auto lir_func = parser.parse(lir_source);
  auto opnd_id_map = buildOperandToIndexMap(parser.getOutputInstrMap());

  LinearScanAllocator lsallocator(lir_func.get());
  lsallocator.initialize();
  lsallocator.calculateLiveIntervals();
  auto id_interval = buildIndexMap(lsallocator.vreg_interval_, opnd_id_map);
  ASSERT_FALSE(id_interval.empty());

  std::vector<int> vregs;
  vregs.reserve(id_interval.size());
  for (auto& ii : id_interval) {
    vregs.push_back(ii.first);
  }

  std::sort(vregs.begin(), vregs.end());

  std::stringstream ss_ranges;
  for (auto& vreg : vregs) {
    ss_ranges << vreg << ": " << id_interval.at(vreg) << "\n";
  }

  std::string live_expected = R"(1: [2, 12), [15, 17)
3: [7, 9)
6: [12, 15)
9: [17, 20)
12: [20, 24)
)";

  ASSERT_EQ(ss_ranges.str(), live_expected);

  auto index_uses_map = buildIndexMap(lsallocator.vreg_phy_uses_, opnd_id_map);
  std::stringstream ss_uses;
  for (auto& vreg : vregs) {
    auto sep = "";
    for (auto& s : index_uses_map[vreg]) {
      fmt::print(ss_uses, "{}{}", sep, s);
      sep = " ";
    }
    ss_uses << "\n";
  }

  std::string uses_expected = R"(2 6 11 16
7 8
12
17

)";
  ASSERT_EQ(ss_uses.str(), uses_expected);

  lsallocator.linearScan();

  std::stringstream allocated;
  for (auto& interval : lsallocator.allocated_) {
    fmt::print(
        allocated,
        "{}->{}\n",
        opnd_id_map.at(interval->vreg),
        interval->allocated_loc);
  }

  std::string allocated_expected = R"(1->0
3->1
6->0
9->0
12->0
)";

  ASSERT_EQ(allocated.str(), allocated_expected);
}

TEST_F(LinearScanAllocatorTest, RegAllocation) {
  const char* lir_source = R"(Function:
BB %0 - succs: %5 %8
  %1 = Move 0(0x0)
  %2 = Add %1, 0(0x0)
  %3 = Add %1, 8(0x8)
  CondBranch %2, BB%5, BB%8
BB %5 - succs: %25
  %6 = Call 1024(0x400), %2, %3
  Branch BB%25
BB %8 - succs: %25
  %9 = Add %2, %3
  %10 = Add %9, 1
  %11 = Add %10, 1
  %12 = Add %11, 1
  %13 = Add %12, 1
  %14 = Add %13, 1
  %15 = Add %14, 1
  %16 = Add %15, 1
  %17 = Add %16, 1
  %18 = Call 1024(0x400), %3, %2, %9, %17
  %19 = Add %2, %9
  %20 = Add %10, %11
  %21 = Add %20, %13
  %22 = Add %21, %15
  %23 = Call 1024(0x400), %19, %18, %22
  Branch BB%25
BB %25 - succs: %28
  %26 = Phi (BB%8, %23), (BB%5, %6)
  Return %26
BB %28

)";

  Parser parser;
  auto lir_func = parser.parse(lir_source);
  auto opnd_id_map = buildOperandToIndexMap(parser.getOutputInstrMap());

  LinearScanAllocator lsallocator(lir_func.get());
  lsallocator.initialize();
  lsallocator.sortBasicBlocks();
  lsallocator.calculateLiveIntervals();
  lsallocator.linearScan();
  ASSERT_FALSE(lsallocator.allocated_.empty());

  ASSERT_GT(lsallocator.getFrameSize(), 0)
      << "Incorrect results - no registers have been spilled.";

  std::vector<LiveInterval*> intervals;
  UnorderedMap<int, std::vector<LiveInterval*>> loc_interval_map;
  UnorderedMap<const Operand*, std::vector<LiveInterval*>> vreg_location_map;
  for (auto& alloc : lsallocator.allocated_) {
    if (!opnd_id_map.count(alloc->vreg)) {
      continue;
    }
    loc_interval_map[alloc->allocated_loc].push_back(alloc.get());
    vreg_location_map[alloc->vreg].push_back(alloc.get());
  }

  // check if the intervals allocated to the same location do not overlop
  for (auto& pair : loc_interval_map) {
    auto& loc_intervals = pair.second;
    for (size_t i = 0; i < loc_intervals.size() - 1; i++) {
      for (size_t j = i + 1; j < loc_intervals.size(); j++) {
        auto ival1 = loc_intervals[i];
        auto ival2 = loc_intervals[j];
        auto intersection = ival1->intersectWith(*ival2);

        ASSERT_EQ(intersection, -1) << fmt::format(
            "Location {} has conflicting intervals: {} intersects with {}",
            pair.first,
            *ival1,
            *ival2);
      }
    }
  }

  // check if the same virtual register does not allocate to multiple locations
  // at the same time
  for (auto& pair : vreg_location_map) {
    auto& vreg_intervals = pair.second;
    for (size_t i = 0; i < vreg_intervals.size() - 1; i++) {
      for (size_t j = i + 1; j < vreg_intervals.size(); j++) {
        auto ival1 = vreg_intervals[i];
        auto ival2 = vreg_intervals[j];
        auto intersection = ival1->intersectWith(*ival2);

        ASSERT_EQ(intersection, -1) << fmt::format(
            "Vreg {} has conflicting intervals: {} intersects with {}",
            opnd_id_map.at(pair.first),
            *ival1,
            *ival2);
      }
    }
  }
}

TEST_F(LinearScanAllocatorTest, InoutRegTest) {
  // OptimizeMoveSequence should not set reg operands that are also output
  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  auto a =
      bb->allocateInstr(Instruction::kMove, nullptr, lir::OutVReg(), Imm(0));
  auto b =
      bb->allocateInstr(Instruction::kMove, nullptr, lir::OutVReg(), Imm(0));

  auto add = bb->allocateInstr(
      Instruction::kAdd, nullptr, lir::OutVReg(), lir::VReg(a), lir::VReg(b));

  bb->allocateInstr(Instruction::kReturn, nullptr, lir::VReg(add));

  auto epilogue = lirfunc->allocateBasicBlock();
  bb->addSuccessor(epilogue);

  runAllocator(lirfunc.get());

  ASSERT_TRUE(
      add->output()->getPhyRegister() == add->getInput(0)->getPhyRegister() ||
      add->output()->getPhyRegister() == add->getInput(1)->getPhyRegister());
}

TEST_F(LinearScanAllocatorTest, CallWithSideEffectTest) {
  // RewriteLIR should not remove function calls
  // since they may have side effects
  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  auto a = bb->allocateInstr(Instruction::kCall, nullptr, lir::OutVReg());

  auto b =
      bb->allocateInstr(Instruction::kMove, nullptr, lir::OutVReg(), Imm(0));

  bb->allocateInstr(Instruction::kReturn, nullptr, lir::VReg(b));

  auto epilogue = lirfunc->allocateBasicBlock();
  bb->addSuccessor(epilogue);
  ASSERT_TRUE(a->opcode() == Instruction::kCall);
  ASSERT_TRUE(a->output()->type() == lir::Operand::kVreg);
  runAllocator(lirfunc.get());
  ASSERT_TRUE(a->opcode() == Instruction::kCall);
  ASSERT_TRUE(a->output()->type() == lir::Operand::kNone);
}
} // namespace jit::lir
