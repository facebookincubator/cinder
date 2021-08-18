// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"
#include "opcode.h"

#include "Jit/hir/hir.h"
#include "Jit/hir/parser.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

using namespace jit::hir;

TEST(BasicBlockTest, CanAppendInstrs) {
  Environment env;
  BasicBlock block;
  auto v0 = env.AllocateRegister();
  block.append<LoadConst>(v0, TNoneType);
  block.append<Return>(v0);
  ASSERT_TRUE(block.GetTerminator()->IsReturn());
}

TEST(BasicBlockTest, CanIterateInstrs) {
  Environment env;
  BasicBlock block;
  auto v0 = env.AllocateRegister();
  block.append<LoadConst>(v0, TNoneType);
  block.append<Return>(v0);

  auto it = block.begin();
  ASSERT_TRUE(it->IsLoadConst());
  it++;
  ASSERT_TRUE(it->IsReturn());
  it++;
  ASSERT_TRUE(it == block.end());
}

TEST(BasicBlockTest, SplitAfterSplitsBlockAfterInstruction) {
  Environment env;
  CFG cfg;
  BasicBlock* head = cfg.AllocateBlock();
  auto v0 = env.AllocateRegister();
  head->append<LoadConst>(v0, TNoneType);
  Instr* load_const = head->GetTerminator();
  head->append<Return>(v0);
  BasicBlock* tail = head->splitAfter(*load_const);
  ASSERT_NE(nullptr, head->GetTerminator());
  EXPECT_TRUE(head->GetTerminator()->IsLoadConst());
  ASSERT_NE(nullptr, tail->GetTerminator());
  EXPECT_TRUE(tail->GetTerminator()->IsReturn());
}

TEST(CFGIterTest, IteratingEmptyCFGReturnsEmptyTraversal) {
  CFG cfg;
  std::vector<BasicBlock*> traversal = cfg.GetRPOTraversal();
  ASSERT_EQ(traversal.size(), 0);
}

TEST(CFGIterTest, IteratingSingleBlockCFGReturnsOneBlock) {
  Environment env;
  CFG cfg;
  BasicBlock* block = cfg.AllocateBlock();
  cfg.entry_block = block;

  // Add a single instuction to the block
  block->append<Return>(env.AllocateRegister());

  std::vector<BasicBlock*> traversal = cfg.GetRPOTraversal();
  ASSERT_EQ(traversal.size(), 1) << "Incorrect number of blocks returned";
  ASSERT_EQ(traversal[0], block) << "Incorrect block returned";
}

TEST(CFGIterTest, VisitsBlocksOnlyOnce) {
  CFG cfg;
  BasicBlock* block = cfg.AllocateBlock();
  cfg.entry_block = block;

  // The block loops on itself
  block->append<Branch>(block);

  std::vector<BasicBlock*> traversal = cfg.GetRPOTraversal();
  ASSERT_EQ(traversal.size(), 1) << "Incorrect number of blocks returned";
  ASSERT_EQ(traversal[0], block) << "Incorrect block returned";
}

TEST(CFGIterTest, VisitsAllBranches) {
  Environment env;
  CFG cfg;
  BasicBlock* cond = cfg.AllocateBlock();
  cfg.entry_block = cond;

  BasicBlock* true_block = cfg.AllocateBlock();
  true_block->append<Return>(env.AllocateRegister());

  BasicBlock* false_block = cfg.AllocateBlock();
  false_block->append<Return>(env.AllocateRegister());

  cond->append<CondBranch>(env.AllocateRegister(), true_block, false_block);

  std::vector<BasicBlock*> traversal = cfg.GetRPOTraversal();
  ASSERT_EQ(traversal.size(), 3) << "Incorrect number of blocks returned";
  ASSERT_EQ(traversal[0], cond) << "Should have visited cond block first";
  ASSERT_EQ(traversal[1], true_block)
      << "Should have visited true block second";
  ASSERT_EQ(traversal[2], false_block)
      << "Should have visited false block last";
}

TEST(CFGIterTest, VisitsLoops) {
  Environment env;
  CFG cfg;

  // Create the else block
  BasicBlock* outer_else = cfg.AllocateBlock();
  outer_else->append<Return>(env.AllocateRegister());

  // Create the inner loop
  BasicBlock* loop_cond = cfg.AllocateBlock();
  BasicBlock* loop_body = cfg.AllocateBlock();
  loop_body->append<Branch>(loop_cond);
  loop_cond->append<CondBranch>(env.AllocateRegister(), loop_body, outer_else);

  // Create the outer conditional
  BasicBlock* outer_cond = cfg.AllocateBlock();
  outer_cond->append<CondBranch>(env.AllocateRegister(), loop_cond, outer_else);
  cfg.entry_block = outer_cond;

  std::vector<BasicBlock*> traversal = cfg.GetRPOTraversal();
  ASSERT_EQ(traversal.size(), 4) << "Incorrect number of blocks returned";
  ASSERT_EQ(traversal[0], outer_cond) << "Should have visited outer cond first";
  ASSERT_EQ(traversal[1], loop_cond) << "Should have visited loop cond second";
  ASSERT_EQ(traversal[2], loop_body) << "Should have visited loop body third";
  ASSERT_EQ(traversal[3], outer_else) << "Should have visited else block last";
}

TEST(SplitCriticalEdgesTest, SplitsCriticalEdges) {
  auto hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<1, 2> v0
  }
  bb 1 {
    v1 = LoadConst<NoneType>
    Branch<2>
  }
  bb 2 {
    v2 = Phi<0, 1> v0 v1
    CondBranch<3, 5> v2
  }
  bb 3 {
    Branch<5>
  }
  bb 5 {
    Return v2
  }
}
)";
  auto func = HIRParser{}.ParseHIR(hir_source);
  ASSERT_NE(func, nullptr);
  ASSERT_TRUE(checkFunc(*func, std::cout));

  func->cfg.splitCriticalEdges();
  const char* expected_hir = R"(fun test {
  bb 0 {
    v0 = LoadConst<NoneType>
    CondBranch<1, 5> v0
  }

  bb 1 (preds 0) {
    v1 = LoadConst<NoneType>
    Branch<2>
  }

  bb 5 (preds 0) {
    Branch<2>
  }

  bb 2 (preds 1, 5) {
    v2 = Phi<1, 5> v1 v0
    CondBranch<3, 6> v2
  }

  bb 3 (preds 2) {
    Branch<5>
  }

  bb 6 (preds 2) {
    Branch<5>
  }

  bb 5 (preds 3, 6) {
    Return v2
  }
}
)";
  EXPECT_EQ(HIRPrinter{}.ToString(*func), expected_hir);
}

TEST(RemoveTrampolineBlocksTest, DoesntModifySingleBlockLoops) {
  CFG cfg;
  Environment env;

  cfg.entry_block = cfg.AllocateBlock();
  cfg.entry_block->append<Branch>(cfg.entry_block);

  cfg.RemoveTrampolineBlocks();

  auto s = HIRPrinter().ToString(cfg);
  const char* expected = R"(bb 0 (preds 0) {
  Branch<0>
}
)";
  ASSERT_EQ(s, expected);
}

TEST(RemoveTrampolineBlocksTest, ReducesSimpleLoops) {
  CFG cfg;
  Environment env;

  auto t1 = cfg.AllocateBlock();
  cfg.entry_block = cfg.AllocateBlock();
  cfg.entry_block->append<Branch>(t1);
  t1->append<Branch>(cfg.entry_block);

  cfg.RemoveTrampolineBlocks();

  auto s = HIRPrinter().ToString(cfg);
  const char* expected = R"(bb 1 (preds 1) {
  Branch<1>
}
)";
  ASSERT_EQ(s, expected);
}

TEST(RemoveTrampolineBlocksTest, RemovesSimpleChain) {
  CFG cfg;
  Environment env;

  // This constructs a CFG that looks like
  //
  // entry -> t2 -> t1 -> exit
  //
  // after removing tramponline blocks we should be left
  // with only the exit block
  auto exit_block = cfg.AllocateBlock();
  exit_block->append<Return>(env.AllocateRegister());

  auto t1 = cfg.AllocateBlock();
  t1->append<Branch>(exit_block);

  auto t2 = cfg.AllocateBlock();
  t2->append<Branch>(t1);

  cfg.entry_block = cfg.AllocateBlock();
  cfg.entry_block->append<Branch>(t2);

  cfg.RemoveTrampolineBlocks();

  auto s = HIRPrinter().ToString(cfg);
  auto expected = R"(bb 0 {
  Return v0
}
)";
  ASSERT_EQ(s, expected);
}

TEST(RemoveTrampolineBlocksTest, ReducesLoops) {
  CFG cfg;
  Environment env;

  // This constructs a CFG that look like
  //
  //              entry
  //                |
  //   +--- true ---+--- false ---+
  //   |                          |
  //  exit                        1->2->3->4-+
  //                                 ^       |
  //                                 |       |
  //                                 +-------+
  //
  // the loop of trampoline blocks on the right should be
  // reduced to a single block that loops back on itself:
  //
  //              entry
  //                |
  //   +--- true ---+--- false ---+
  //   |                          |
  //  exit                        4--+
  //                              ^  |
  //                              |  |
  //                              +--+
  Register* v0 = env.AllocateRegister();
  auto exit_block = cfg.AllocateBlock();
  exit_block->append<Return>(v0);

  auto t1 = cfg.AllocateBlock();
  auto t2 = cfg.AllocateBlock();
  auto t3 = cfg.AllocateBlock();
  auto t4 = cfg.AllocateBlock();
  t1->append<Branch>(t2);
  t2->append<Branch>(t3);
  t3->append<Branch>(t4);
  t4->append<Branch>(t2);

  cfg.entry_block = cfg.AllocateBlock();
  cfg.entry_block->append<CondBranch>(v0, exit_block, t1);

  cfg.RemoveTrampolineBlocks();

  auto after = HIRPrinter().ToString(cfg);
  const char* expected = R"(bb 5 {
  CondBranch<0, 4> v0
}

bb 0 (preds 5) {
  Return v0
}

bb 4 (preds 4, 5) {
  Branch<4>
}
)";
  ASSERT_EQ(after, expected);
}

TEST(RemoveTrampolineBlocksTest, UpdatesAllPredecessors) {
  CFG cfg;
  Environment env;

  // This constructs a CFG that look like
  //
  //              entry
  //                |
  //   +--- true ---+--- false ---+
  //   |                          |
  //   4                          3
  //   |                          |
  //   +----------->2<------------+
  //                |
  //                v
  //                1
  //                |
  //                v
  //               exit
  //
  // After removing trampoline blocks this should look like
  //
  //              entry
  //                |
  //                v
  //               exit
  Register* v0 = env.AllocateRegister();
  auto exit_block = cfg.AllocateBlock();
  exit_block->append<Return>(v0);

  auto t1 = cfg.AllocateBlock();
  t1->append<Branch>(exit_block);

  auto t2 = cfg.AllocateBlock();
  t2->append<Branch>(t1);

  auto t3 = cfg.AllocateBlock();
  t3->append<Branch>(t2);

  auto t4 = cfg.AllocateBlock();
  t4->append<Branch>(t2);

  cfg.entry_block = cfg.AllocateBlock();
  cfg.entry_block->append<CondBranch>(v0, t4, t3);

  cfg.RemoveTrampolineBlocks();

  auto after = HIRPrinter().ToString(cfg);
  const char* expected = R"(bb 5 {
  Branch<0>
}

bb 0 (preds 5) {
  Return v0
}
)";
  ASSERT_EQ(after, expected);
}

class EdgeCaseTest : public RuntimeTest {};

TEST_F(EdgeCaseTest, IgnoreUnreachableLoops) {
  //  0 LOAD_CONST    0
  //  2 RETURN_VALUE
  //
  //  4 LOAD_CONST    0
  //  6 RETURN_VALUE
  //  8 JUMP_ABSOLUTE 4
  const char bc[] = {
      LOAD_CONST,
      0,
      RETURN_VALUE,
      0,
      LOAD_CONST,
      0,
      RETURN_VALUE,
      0,
      JUMP_ABSOLUTE,
      4};
  auto bytecode = Ref<>::steal(PyBytes_FromStringAndSize(bc, sizeof(bc)));
  ASSERT_NE(bytecode.get(), nullptr);
  auto filename = Ref<>::steal(PyUnicode_FromString("filename"));
  auto funcname = Ref<>::steal(PyUnicode_FromString("funcname"));
  auto consts = Ref<>::steal(PyTuple_New(1));
  Py_INCREF(Py_None);
  PyTuple_SET_ITEM(consts.get(), 0, Py_None);
  auto empty_tuple = Ref<>::steal(PyTuple_New(0));
  auto code = Ref<PyCodeObject>::steal(PyCode_New(
      0,
      0,
      0,
      0,
      0,
      bytecode,
      consts,
      empty_tuple,
      empty_tuple,
      empty_tuple,
      empty_tuple,
      filename,
      funcname,
      0,
      PyBytes_FromString("")));
  ASSERT_NE(code.get(), nullptr);

  auto func = Ref<PyFunctionObject>::steal(PyFunction_New(code, MakeGlobals()));
  ASSERT_NE(func.get(), nullptr);

  HIRBuilder builder;
  std::unique_ptr<Function> irfunc(HIRBuilder().BuildHIR(func));
  ASSERT_NE(irfunc.get(), nullptr);

  const char* expected = R"(fun jittestmodule:funcname {
  bb 0 {
    Snapshot {
      NextInstrOffset 0
    }
    v0 = LoadConst<NoneType>
    Return v0
  }
}
)";
  EXPECT_EQ(HIRPrinter(true).ToString(*(irfunc)), expected);
}
