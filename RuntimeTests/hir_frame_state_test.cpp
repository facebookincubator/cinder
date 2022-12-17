// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"
#include "opcode.h"

#include "Jit/hir/builder.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/printer.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

using namespace jit::hir;

class FrameStateCreationTest : public RuntimeTest {};

TEST_F(FrameStateCreationTest, InitialInstrOffset) {
  FrameState frame;
  EXPECT_LT(frame.instr_offset(), 0);
  EXPECT_EQ(frame.instr_offset().value() % sizeof(_Py_CODEUNIT), 0);
}

#define EXPECT_HIR_EQ(irfunc, expected) \
  EXPECT_EQ(HIRPrinter(true).ToString(*(irfunc)), expected)

TEST_F(FrameStateCreationTest, LoadGlobal) {
  const char* src = R"(
def test():
  return foo
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    Snapshot {
      NextInstrOffset 0
    }
    v0 = LoadGlobal<0; "foo"> {
      FrameState {
        NextInstrOffset 2
      }
    }
    Snapshot {
      NextInstrOffset 2
      Stack<1> v0
    }
    Return v0
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, GetIterForIter) {
  const char* src = R"(
def test(fs):
  for x in xs:
    pass
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "fs">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v2 = LoadGlobal<0; "xs"> {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 2
      Locals<2> v0 v1
      Stack<1> v2
    }
    v3 = GetIter v2 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
      Stack<1> v3
    }
    v2 = Assign v3
    Branch<4>
  }

  bb 4 (preds 0, 2) {
    v6 = LoadEvalBreaker
    CondBranch<5, 1> v6
  }

  bb 5 (preds 4) {
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
      Stack<1> v2
    }
    v7 = RunPeriodicTasks {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v2
      }
    }
    Branch<1>
  }

  bb 1 (preds 4, 5) {
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
      Stack<1> v2
    }
    v4 = InvokeIterNext v2 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
        Stack<1> v2
      }
    }
    v3 = Assign v4
    CondBranchIterNotDone<2, 3> v3
  }

  bb 2 (preds 1) {
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<2> v2 v3
    }
    v1 = Assign v3
    Branch<4>
  }

  bb 3 (preds 1) {
    Snapshot {
      NextInstrOffset 10
      Locals<2> v0 v1
    }
    v5 = LoadConst<NoneType>
    Return v5
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, NonUniformConditionals1) {
  // This function has different operand stack contents along each branch of
  // the conditional
  const char* src = R"(
def test(x, y):
  return x and y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);

  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v2 = IsTruthy v0 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v3 = Assign v0
    CondBranch<1, 2> v2
  }

  bb 1 (preds 0) {
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
      }
    }
    v3 = Assign v1
    Branch<2>
  }

  bb 2 (preds 0, 1) {
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v3
    }
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, NonUniformConditionals2) {
  // This function has different operand stack contents along each branch of
  // the conditional
  const char* src = R"(
def test(x, y):
  return x or y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);

  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v2 = IsTruthy v0 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v3 = Assign v0
    CondBranch<2, 1> v2
  }

  bb 1 (preds 0) {
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
      }
    }
    v3 = Assign v1
    Branch<2>
  }

  bb 2 (preds 0, 1) {
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v3
    }
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, CallFunction) {
  const char* src = R"(
def test(f, a):
  return f(a)
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "f">
    v1 = LoadArg<1; "a">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"f"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v1 = CheckVar<"a"> v1 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v2 = VectorCall<1> v0 v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v2
    }
    Return v2
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, LoadCallMethod) {
  const char* src = R"(
def test(f, a):
  return f.bar(a)
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "f">
    v1 = LoadArg<1; "a">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"f"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v2 = LoadMethod<0; "bar"> v0 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
      }
    }
    v3 = GetLoadMethodInstance<1> v0
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
      Stack<2> v2 v3
    }
    v1 = CheckVar<"a"> v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
        Stack<2> v2 v3
      }
    }
    v4 = CallMethod<3> v2 v3 v1 {
      FrameState {
        NextInstrOffset 8
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 8
      Locals<2> v0 v1
      Stack<1> v4
    }
    Return v4
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, LoadAttr) {
  const char* src = R"(
def test(f):
  return f.a.b
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "f">
    Snapshot {
      NextInstrOffset 0
      Locals<1> v0
    }
    v0 = CheckVar<"f"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<1> v0
      }
    }
    v1 = LoadAttr<0; "a"> v0 {
      FrameState {
        NextInstrOffset 4
        Locals<1> v0
      }
    }
    Snapshot {
      NextInstrOffset 4
      Locals<1> v0
      Stack<1> v1
    }
    v2 = LoadAttr<1; "b"> v1 {
      FrameState {
        NextInstrOffset 6
        Locals<1> v0
      }
    }
    Snapshot {
      NextInstrOffset 6
      Locals<1> v0
      Stack<1> v2
    }
    Return v2
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, InPlaceOp) {
  const char* src = R"(
def test(x, y):
  x ^= y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v2 = InPlaceOp<Xor> v0 v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v2
    }
    v0 = Assign v2
    v3 = LoadConst<NoneType>
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, BinaryOp) {
  const char* src = R"(
def test(x, y):
  return x + y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v2 = BinaryOp<Add> v0 v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v2
    }
    Return v2
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, UnaryOp) {
  const char* src = R"(
def test(x):
  return not x
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    Snapshot {
      NextInstrOffset 0
      Locals<1> v0
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<1> v0
      }
    }
    v1 = UnaryOp<Not> v0 {
      FrameState {
        NextInstrOffset 4
        Locals<1> v0
      }
    }
    Snapshot {
      NextInstrOffset 4
      Locals<1> v0
      Stack<1> v1
    }
    Return v1
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, StoreAttr) {
  const char* src = R"(
def test(x, y):
  x.foo = y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v1
      }
    }
    v2 = StoreAttr<0; "foo"> v0 v1 {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
    }
    v3 = LoadConst<NoneType>
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, StoreSubscr) {
  const char* src = R"(
def test(x, y):
  x[1] = y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v1
      }
    }
    v2 = LoadConst<MortalLongExact[1]>
    v3 = StoreSubscr v0 v2 v1 {
      FrameState {
        NextInstrOffset 8
        Locals<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 8
      Locals<2> v0 v1
    }
    v4 = LoadConst<NoneType>
    Return v4
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, DictLiteral) {
  const char* src = R"(
def test(x, y):
  return {'x': x, 'y': y}
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v2 = LoadConst<MortalTupleExact[tuple:0xdeadbeef]>
    v3 = MakeDict<2> {
      FrameState {
        NextInstrOffset 8
        Locals<2> v0 v1
        Stack<3> v0 v1 v2
      }
    }
    v4 = LoadTupleItem<0> v2
    v5 = SetDictItem v3 v4 v0 {
      FrameState {
        NextInstrOffset 8
        Locals<2> v0 v1
        Stack<2> v0 v1
      }
    }
    v6 = LoadTupleItem<1> v2
    v7 = SetDictItem v3 v6 v1 {
      FrameState {
        NextInstrOffset 8
        Locals<2> v0 v1
        Stack<2> v0 v1
      }
    }
    Snapshot {
      NextInstrOffset 8
      Locals<2> v0 v1
      Stack<1> v3
    }
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, ListLiteral) {
  const char* src = R"(
def test(x, y):
  return [x, y]
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v2 = MakeListTuple<list, 2> {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
        Stack<2> v0 v1
      }
    }
    InitListTuple<list, 2> v2 v0 v1
    v3 = Assign v2
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v3
    }
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, TupleLiteral) {
  const char* src = R"(
def test(x, y):
  return x, y
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    v1 = LoadArg<1; "y">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v1 = CheckVar<"y"> v1 {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    v2 = MakeListTuple<tuple, 2> {
      FrameState {
        NextInstrOffset 6
        Locals<2> v0 v1
        Stack<2> v0 v1
      }
    }
    InitListTuple<tuple, 2> v2 v0 v1
    v3 = Assign v2
    Snapshot {
      NextInstrOffset 6
      Locals<2> v0 v1
      Stack<1> v3
    }
    Return v3
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, MakeFunction) {
  const char* src = R"(
def test(x):
  def foo(a=x):
    return a
  return foo
)";
  std::unique_ptr<Function> irfunc;
  CompileToHIR(src, "test", irfunc);
  const char* expected = R"(fun jittestmodule:test {
  bb 0 {
    v0 = LoadArg<0; "x">
    Snapshot {
      NextInstrOffset 0
      Locals<2> v0 v1
    }
    v0 = CheckVar<"x"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<2> v0 v1
      }
    }
    v2 = MakeListTuple<tuple, 1> {
      FrameState {
        NextInstrOffset 4
        Locals<2> v0 v1
        Stack<1> v0
      }
    }
    InitListTuple<tuple, 1> v2 v0
    v3 = Assign v2
    Snapshot {
      NextInstrOffset 4
      Locals<2> v0 v1
      Stack<1> v3
    }
    v4 = LoadConst<MortalCode["foo"]>
    v5 = LoadConst<MortalUnicodeExact["test.<locals>.foo"]>
    v6 = MakeFunction v5 v4 {
      FrameState {
        NextInstrOffset 10
        Locals<2> v0 v1
        Stack<1> v3
      }
    }
    SetFunctionAttr<func_defaults> v3 v6
    InitFunction v6
    Snapshot {
      NextInstrOffset 10
      Locals<2> v0 v1
      Stack<1> v6
    }
    v1 = Assign v6
    v1 = CheckVar<"foo"> v1 {
      FrameState {
        NextInstrOffset 14
        Locals<2> v0 v1
      }
    }
    Return v1
  }
}
)";
  EXPECT_HIR_EQ(irfunc, expected);
}

TEST_F(FrameStateCreationTest, GetDominatingFrameState) {
  CFG cfg;
  auto block = cfg.AllocateBlock();
  FrameState fs{10};
  block->append<Snapshot>(fs);

  auto addCheckExc = [&block]() {
    return block->append<CheckExc>(nullptr, nullptr);
  };

  auto i1 = addCheckExc();
  auto i1_fs = i1->getDominatingFrameState();
  ASSERT_NE(i1_fs, nullptr);
  ASSERT_EQ(*i1_fs, fs);

  for (int i = 0; i < 5; i++) {
    addCheckExc();
  }
  auto i2 = addCheckExc();
  auto i2_fs = i2->getDominatingFrameState();
  ASSERT_NE(i2_fs, nullptr);
  ASSERT_EQ(*i2_fs, fs);
  FrameState fs2{20};
  block->append<Snapshot>(fs2);

  for (int i = 0; i < 5; i++) {
    addCheckExc();
  }
  auto i3 = addCheckExc();
  auto i3_fs = i3->getDominatingFrameState();
  ASSERT_NE(i3_fs, nullptr);
  ASSERT_EQ(*i3_fs, fs2);
}
