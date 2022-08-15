// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/hir/hir.h"
#include "Jit/hir/parser.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <ostream>

class SSAifyTest : public RuntimeTest {};

using namespace jit::hir;

static void testCheckFunc(const char* hir_source, const char* expected_err) {
  std::ostringstream err;
  auto func = HIRParser().ParseHIR(hir_source);
  ASSERT_NE(func, nullptr);
  ASSERT_FALSE(checkFunc(*func, err));
  EXPECT_EQ(err.str(), expected_err);
}

TEST(CheckFuncTest, UndefinedVariables) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    v1 = UnaryOp<Not> v100
    CondBranch<1, 2> v1
  }
  bb 1 {
    v3 = Assign v1
    Branch<3>
  }
  bb 2 {
    v4 = Assign v0
    Branch<3>
  }
  bb 3 {
    Return v3
  }
}
)";
  const char* expected_err =
      R"(ERROR: Operand 'v100' of instruction 'v1 = UnaryOp<Not> v100 {
  FrameState {
    NextInstrOffset 0
  }
}' not defined at use in bb 0
ERROR: Operand 'v3' of instruction 'Return v3' not defined at use in bb 3
)";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, UndefinedPhiInput) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<1, 2> v0
  }
  bb 1 {
    v1 = LoadConst<NoneType>
    Branch<3>
  }
  bb 2 {
    v2 = LoadConst<NoneType>
    Branch<3>
  }
  bb 3 {
    v3 = Phi<1, 2> v1 v3
    Return v3
  }
}
)";
  const char* expected_err =
      "ERROR: Phi input 'v3' to instruction 'v3 = Phi<1, 2> v1 v3' in bb 3 not "
      "defined at end of bb 2\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, NonFirstPhi) {
  // HIRParser() fixes the positions of Phis, so we have to manually construct
  // the bad code here.
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  auto b1 = func.cfg.AllocateBlock();
  auto v0 = func.env.AllocateRegister();
  auto v1 = func.env.AllocateRegister();
  auto v2 = func.env.AllocateRegister();

  b0->append<LoadConst>(v0, TNoneType);
  b0->append<CondBranch>(v0, b0, b1);

  b1->append<LoadConst>(v2, TNoneType);
  std::unordered_map<BasicBlock*, Register*> phi_args{{b0, v0}, {b1, v2}};
  b1->append<Phi>(v1, phi_args);
  b1->append<Branch>(b1);

  const char* expected_err =
      "ERROR: 'v1 = Phi<0, 1> v0 v2' in bb 1 comes after non-Phi instruction\n";
  std::ostringstream err;
  ASSERT_FALSE(checkFunc(func, err));
  EXPECT_EQ(err.str(), expected_err);
}

TEST(CheckFuncTest, RegisterInstr) {
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  auto v0 = func.env.AllocateRegister();

  b0->append<LoadConst>(v0, TNoneType);
  v0->set_instr(nullptr);
  b0->append<Return>(v0);

  const char* expected_err =
      "ERROR: v0's instr is not 'v0 = LoadConst<NoneType>', which claims to "
      "define "
      "it\n";
  std::ostringstream err;
  ASSERT_FALSE(checkFunc(func, err));
  EXPECT_EQ(err.str(), expected_err);
}

TEST(CheckFuncTest, RedefinedVariable) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<1, 2> v0
  }
  bb 1 {
    v1 = LoadConst<NoneType>
    Branch<3>
  }
  bb 2 {
    v2 = LoadConst<NoneType>
    Branch<3>
  }
  bb 3 {
    v3 = Phi<1, 2> v1 v2
    v1 = LoadConst<NoneType>
    Return v3
  }
}
)";
  const char* expected_err =
      "ERROR: v1's instr is not 'v1 = LoadConst<NoneType>', which claims to "
      "define "
      "it\nERROR: v1 redefined in bb 3; previous definition was in bb 1\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, MultipleTerminators) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<2, 3> v0
    v1 = LoadConst<NoneType>
    Branch<1>
  }
  bb 1 {
    Branch<2>
  }
  bb 2 {
    Branch<1>
  }
  bb 3 {
    Branch<1>
  }
}
)";
  const char* expected_err =
      "ERROR: bb 0 contains terminator 'CondBranch<2, 3> v0' in non-terminal "
      "position\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, NoTerminator) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<1, 2> v0
  }
  bb 1 {
    Branch<0>
  }
  bb 2 {
    v1 = LoadConst<NoneType>
    v2 = UnaryOp<Not> v1
  }
}
)";
  const char* expected_err = "ERROR: bb 2 has no terminator at end\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, NonTerminalTerminator) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<1, 2> v0
  }
  bb 1 {
    Branch<0>
  }
  bb 2 {
    v1 = LoadConst<NoneType>
    Return v1
    v2 = UnaryOp<Not> v1
  }
}
)";
  const char* expected_err =
      "ERROR: bb 2 contains terminator 'Return v1' in non-terminal "
      "position\nERROR: bb 2 has no terminator at end\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, MultipleEdgesFromSamePred) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    Branch<1>
  }
  bb 1 {
    CondBranch<0, 0> v0
  }
}
)";
  const char* expected_err = "ERROR: bb 0 has > 1 edge from predecessor bb 1\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, EmptyBlock) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    Branch<1>
  }
  bb 1 {
  }
}
)";
  const char* expected_err = "ERROR: bb 1 has no instructions\n";
  EXPECT_NO_FATAL_FAILURE(testCheckFunc(hir_source, expected_err));
}

TEST(CheckFuncTest, BadCFG) {
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  std::unique_ptr<BasicBlock> b1{func.cfg.AllocateUnlinkedBlock()};
  auto tmp = func.env.AllocateRegister();

  b0->append<Branch>(b1.get());

  b1->append<LoadConst>(tmp, TNoneType);
  b1->append<Return>(tmp);

  std::ostringstream err;
  ASSERT_FALSE(checkFunc(func, err));
  EXPECT_EQ(err.str(), "ERROR: Reachable bb 1 isn't part of CFG\n");

  // Unlink the orphan block from the CFG before it's destroyed, to avoid
  // exploding in ~BasicBlock.
  b0->GetTerminator()->edge(0)->set_to(nullptr);
}

TEST(CheckFuncTest, UnlinkedPredecessor) {
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  auto b1 = func.cfg.AllocateBlock();
  std::unique_ptr<BasicBlock> b2{func.cfg.AllocateUnlinkedBlock()};
  auto tmp = func.env.AllocateRegister();

  b0->append<Branch>(b1);

  b1->append<LoadConst>(tmp, TNoneType);
  b1->append<Return>(tmp);

  b2->append<Branch>(b1);

  std::ostringstream err;
  ASSERT_FALSE(checkFunc(func, err));
  EXPECT_EQ(err.str(), "ERROR: bb 1 has unreachable predecessor bb 2\n");
}

TEST(CheckFuncTest, UnreachableBlock) {
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  auto b1 = func.cfg.AllocateBlock();
  auto b2 = func.cfg.AllocateBlock();
  auto tmp0 = func.env.AllocateRegister();
  auto tmp1 = func.env.AllocateRegister();

  b0->append<Branch>(b1);

  b1->append<LoadConst>(tmp0, TNoneType);
  b1->append<Return>(tmp0);

  b2->append<LoadConst>(tmp1, TNoneType);
  b2->append<Return>(tmp1);

  std::ostringstream err;
  ASSERT_FALSE(checkFunc(func, err));
  EXPECT_EQ(err.str(), "ERROR: CFG contains unreachable bb 2\n");
}

static void testSSAify(const char* hir_source, const char* expected) {
  std::unique_ptr<Function> func(HIRParser().ParseHIR(hir_source));
  ASSERT_NE(func, nullptr);
  SSAify().Run(*func);
  ASSERT_EQ(HIRPrinter().ToString(*func), expected);
}

TEST_F(SSAifyTest, PlacesPhisCorrectlyAtCondBranchJoins) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    CondBranch<1, 2> v0
  }

  bb 1 {
    v1 = LoadConst<NoneType>
    Branch<3>
  }

  bb 2 {
    v1 = LoadConst<NoneType>
    Branch<3>
  }

  bb 3 {
    Return v1
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v2:Object = LoadArg<0>
    CondBranch<1, 2> v2
  }

  bb 1 (preds 0) {
    v3:NoneType = LoadConst<NoneType>
    Branch<3>
  }

  bb 2 (preds 0) {
    v4:NoneType = LoadConst<NoneType>
    Branch<3>
  }

  bb 3 (preds 1, 2) {
    v5:NoneType = Phi<1, 2> v3 v4
    Return v5
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, PlacesPhisCorrectlyInSimpleLoops) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 {
    CondBranch<2, 3> v1
  }

  bb 2 {
    v2 = LoadConst<NoneType>
    v3 = InPlaceOp<Subtract> v1 v2
    v1 = Assign v3
    Branch<1>
  }

  bb 3 {
    Return v1
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v4:Object = LoadArg<0>
    v5:NoneType = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 (preds 0, 2) {
    v6:Object = Phi<0, 2> v5 v8
    CondBranch<2, 3> v6
  }

  bb 2 (preds 1) {
    v7:NoneType = LoadConst<NoneType>
    v8:Object = InPlaceOp<Subtract> v6 v7 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Branch<1>
  }

  bb 3 (preds 1) {
    Return v6
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, PlacesPhisCorrectlyInNestedLoops) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 {
    CondBranch<2, 4> v1
  }

  bb 2 {
    CondBranch<3, 1> v1
  }

  bb 3 {
    v2 = LoadConst<NoneType>
    v3 = InPlaceOp<Subtract> v1 v2
    v1 = Assign v3
    Branch<2>
  }

  bb 4 {
    Return v0
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v4:Object = LoadArg<0>
    v5:NoneType = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 (preds 0, 2) {
    v6:Object = Phi<0, 2> v5 v7
    CondBranch<2, 4> v6
  }

  bb 2 (preds 1, 3) {
    v7:Object = Phi<1, 3> v6 v9
    CondBranch<3, 1> v7
  }

  bb 3 (preds 2) {
    v8:NoneType = LoadConst<NoneType>
    v9:Object = InPlaceOp<Subtract> v7 v8 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Branch<2>
  }

  bb 4 (preds 1) {
    Return v4
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, RemovesTrivialPhis) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 {
    CondBranch<2, 5> v1
  }

  bb 2 {
    CheckVar<"a"> v0 {
    }
    Branch<3>
  }

  bb 3 {
    CondBranch<4, 1> v1
  }

  bb 4 {
    CheckVar<"a"> v0 {
    }
    Branch<3>
  }

  bb 5 {
    Return v0
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v2:Object = LoadArg<0>
    v3:NoneType = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 (preds 0, 3) {
    CondBranch<2, 5> v3
  }

  bb 2 (preds 1) {
    CheckVar<"a"> v2 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Branch<3>
  }

  bb 3 (preds 2, 4) {
    CondBranch<4, 1> v3
  }

  bb 4 (preds 3) {
    CheckVar<"a"> v2 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Branch<3>
  }

  bb 5 (preds 1) {
    Return v2
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, HandlesLocalDefOfTrivialPhi) {
  // Make sure we correctly handle the case where the register corresponding to
  // the output of a trivial phi is redefined later in the same block.
  //
  // In the CFG below, bb1 uses v0 and later redefines it. When converting this
  // to SSA, an incomplete phi will be placed in bb1 for v0. After processing
  // bb3 we'll realize that the phi would have been trivial and never place
  // it. Since v0 was redefined in the same block, subsequent uses of v0 should
  // use the value produced by the redefinition, not whatever replaced the
  // trivial phi's output.
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadConst<NoneType>
    CondBranch<1, 2> v1
  }

  bb 1 {
    CheckVar<"a"> v0
    v0 = LoadConst<NoneType>
    Branch<4>
  }

  bb 2 {
    CondBranch<1, 3> v0
  }

  bb 3 {
    CheckVar<"a"> v0
    Branch<2>
  }

  bb 4 {
    Return v0
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v2:Object = LoadArg<0>
    v3:NoneType = LoadConst<NoneType>
    CondBranch<1, 2> v3
  }

  bb 2 (preds 0, 3) {
    CondBranch<1, 3> v2
  }

  bb 1 (preds 0, 2) {
    CheckVar<"a"> v2 {
      FrameState {
        NextInstrOffset 0
      }
    }
    v6:NoneType = LoadConst<NoneType>
    Branch<4>
  }

  bb 4 (preds 1) {
    Return v6
  }

  bb 3 (preds 2) {
    CheckVar<"a"> v2 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Branch<2>
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, PropagatesRegisterReplacements) {
  // This tests that we correctly handle chains of replaced registers.
  // (e.g. when $v3 has been replaced by $v2, which has been replaced by $v1.
  //
  // When processing the CFG below, the SSA conversion algorithm will
  // do the following:
  //
  // 0. When visiting bb 0, we record a local def for x, $v0.
  // 1. When visiting bb 2, we place an incomplete phi for x in bb 1
  //    and use its output as the local def for x in bb 2, $v1.
  // 2. When visiting bb 3, we place another incomplete phi for x, $v2.
  // 3. After visiting bb 3, we complete the phi that we placed in (2).
  //    It would be trivial, so we record that $v2 should be replaced
  //    with $v1.
  // 4. After visiting bb 5, we complete the phi in bb 1. It too would
  //    have been trivial, so we replace $v1 with $v0.
  //
  // This leads to the replacement chain of $v2 -> $v1 -> $v0.
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 {
    CondBranch<2, 5> v1
  }

  bb 2 {
    CheckVar<"a"> v0 {
    }
    Branch<3>
  }

  bb 3 {
    CondBranch<4, 3> v1
  }

  bb 4 {
    Return v0
  }

  bb 5 {
    CondBranch<6, 1> v1
  }

  bb 6 {
    v2 = LoadConst<NoneType>
    Return v2
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v3:Object = LoadArg<0>
    v4:NoneType = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 (preds 0, 5) {
    CondBranch<2, 5> v4
  }

  bb 2 (preds 1) {
    CheckVar<"a"> v3 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Branch<3>
  }

  bb 3 (preds 2, 3) {
    CondBranch<4, 3> v4
  }

  bb 4 (preds 3) {
    Return v3
  }

  bb 5 (preds 1) {
    CondBranch<6, 1> v4
  }

  bb 6 (preds 5) {
    v9:NoneType = LoadConst<NoneType>
    Return v9
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, HandleMultipleUsesOfSameValueInTrivialPhi) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    Branch<1>
  }

  bb 1 {
    CondBranch<1, 2> v0
  }

  bb 2 {
    CondBranch<1, 2> v0
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v1:Object = LoadArg<0>
    Branch<1>
  }

  bb 1 (preds 0, 1, 2) {
    CondBranch<1, 2> v1
  }

  bb 2 (preds 1, 2) {
    CondBranch<1, 2> v1
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, HandlesReplacementsInIncompletePhis) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadArg<1>
    v2 = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 {
    CondBranch<2, 3> v1
  }

  bb 2 {
    v3 = BinaryOp<Subscript> v0 v2
    v4 = LoadConst<NoneType>
    v5 = BinaryOp<Add> v3 v4
    Decref v3
    CondBranch<1, 3> v5
  }

  bb 3 {
    Incref v0
    Return v0
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v6:Object = LoadArg<0>
    v7:Object = LoadArg<1>
    v8:NoneType = LoadConst<NoneType>
    Branch<1>
  }

  bb 1 (preds 0, 2) {
    CondBranch<2, 3> v7
  }

  bb 2 (preds 1) {
    v12:Object = BinaryOp<Subscript> v6 v8 {
      FrameState {
        NextInstrOffset 0
      }
    }
    v13:NoneType = LoadConst<NoneType>
    v14:Object = BinaryOp<Add> v12 v13 {
      FrameState {
        NextInstrOffset 0
      }
    }
    Decref v12
    CondBranch<1, 3> v14
  }

  bb 3 (preds 1, 2) {
    Incref v6
    Return v6
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}

TEST_F(SSAifyTest, MakeSetReturnsSetExact) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<MortalLongExact[1]>
    v1 = LoadConst<MortalLongExact[2]>
    v2 = LoadConst<MortalLongExact[3]>
    v3 = MakeSet
    v4 = SetSetItem v3 v0
    v5 = SetSetItem v3 v1
    v6 = SetSetItem v3 v2
    Return v3
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v7:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v8:MortalLongExact[2] = LoadConst<MortalLongExact[2]>
    v9:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    v10:MortalSetExact = MakeSet {
    }
    v11:CInt32 = SetSetItem v10 v7 {
    }
    v12:CInt32 = SetSetItem v10 v8 {
    }
    v13:CInt32 = SetSetItem v10 v9 {
    }
    Return v10
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testSSAify(hir_source, expected));
}
