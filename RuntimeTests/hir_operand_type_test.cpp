// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"
#include "opcode.h"

#include "Jit/hir/analysis.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/parser.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"

#include "RuntimeTests/fixtures.h"

using namespace jit::hir;

using HIROperandTypeTest = RuntimeTest;

TEST_F(HIROperandTypeTest, ReturnOperandTypesReturnInitializedType) {
  Function func;
  auto ret = func.env.AllocateRegister();
  std::unique_ptr<Instr> cint32(Return::create(ret, TCInt32));
  OperandType op_type = cint32->GetOperandType(0);

  EXPECT_EQ(op_type.type, TCInt32);
  EXPECT_EQ(op_type.kind, Constraint::kType);

  std::unique_ptr<Instr> cuint8(Return::create(ret, TCUInt8));
  op_type = cuint8->GetOperandType(0);

  EXPECT_EQ(op_type.type, TCUInt8);
  EXPECT_EQ(op_type.kind, Constraint::kType);
}

TEST_F(HIROperandTypeTest, MakeTupleFromListOperandTypesReturnsList) {
  Function func;
  auto value = func.env.AllocateRegister();
  auto dst = func.env.AllocateRegister();
  std::unique_ptr<Instr> instr(MakeTupleFromList::create(dst, value));
  OperandType op_type = instr->GetOperandType(0);

  EXPECT_EQ(op_type.type, TList);
  EXPECT_EQ(op_type.kind, Constraint::kType);
}

TEST_F(HIROperandTypeTest, VectorCallHasVariadicOperandTypes) {
  Function func;
  auto dst = func.env.AllocateRegister();
  auto f = func.env.AllocateRegister();
  auto arg1 = func.env.AllocateRegister();
  auto arg2 = func.env.AllocateRegister();
  std::unique_ptr<Instr> one_call(VectorCall::create(1, dst, false));
  one_call->SetOperand(0, f);
  OperandType op_type = one_call->GetOperandType(0);

  EXPECT_EQ(op_type.type, TOptObject);
  EXPECT_EQ(op_type.kind, Constraint::kType);

  std::unique_ptr<Instr> three_call(VectorCall::create(3, dst, false));
  three_call->SetOperand(0, f);
  three_call->SetOperand(1, arg1);
  three_call->SetOperand(2, arg2);

  op_type = three_call->GetOperandType(0);
  EXPECT_EQ(op_type.type, TOptObject);
  EXPECT_EQ(op_type.kind, Constraint::kType);

  op_type = three_call->GetOperandType(1);
  EXPECT_EQ(op_type.type, TOptObject);
  EXPECT_EQ(op_type.kind, Constraint::kType);

  op_type = three_call->GetOperandType(2);
  EXPECT_EQ(op_type.type, TOptObject);
  EXPECT_EQ(op_type.kind, Constraint::kType);
}

TEST_F(HIROperandTypeTest, LoadArrayItemReturnsMultipleTypesForOneOperand) {
  Function func;
  auto dst = func.env.AllocateRegister();
  auto arg1 = func.env.AllocateRegister();
  auto arg2 = func.env.AllocateRegister();
  auto arg3 = func.env.AllocateRegister();
  std::unique_ptr<Instr> instr(
      LoadArrayItem::create(dst, arg1, arg2, arg3, 0, TObject));

  OperandType op_type = instr->GetOperandType(0);

  EXPECT_EQ(op_type.kind, Constraint::kTupleExactOrCPtr);
}

TEST_F(HIROperandTypeTest, LoadMethodSuperReturnsTypesForMultipleOperands) {
  Function func;
  auto dst = func.env.AllocateRegister();
  auto arg1 = func.env.AllocateRegister();
  auto arg2 = func.env.AllocateRegister();
  auto arg3 = func.env.AllocateRegister();
  std::unique_ptr<Instr> instr(
      LoadMethodSuper::create(dst, arg1, arg2, arg3, 0, true));
  OperandType op_type = instr->GetOperandType(0);
  EXPECT_EQ(op_type.type, TObject);

  op_type = instr->GetOperandType(1);
  EXPECT_EQ(op_type.type, TObject);
  EXPECT_EQ(op_type.kind, Constraint::kType);

  op_type = instr->GetOperandType(2);
  EXPECT_EQ(op_type.type, TObject);
  EXPECT_EQ(op_type.kind, Constraint::kType);
}

TEST_F(HIROperandTypeTest, PrimitiveBoxGetOperandTypeImplReturnsCorrectType) {
  Function func;
  auto dst = func.env.AllocateRegister();
  auto val = func.env.AllocateRegister();
  FrameState frame;
  std::unique_ptr<Instr> instr_TCInt32(
      PrimitiveBox::create(dst, val, TCInt32, frame));
  OperandType op_type_1 = instr_TCInt32->GetOperandType(0);
  EXPECT_EQ(op_type_1.type, TCInt32);
}

static void funcTypeCheckPasses(const char* hir_source) {
  std::ostringstream err;
  auto func = HIRParser().ParseHIR(hir_source);
  ASSERT_NE(func, nullptr);
  ASSERT_TRUE(checkFunc(*func, std::cerr));
  reflowTypes(*func);
  ASSERT_TRUE(funcTypeChecks(*func, err));
}

static void funcTypeCheckFails(
    const char* hir_source,
    const char* expected_err) {
  std::ostringstream err;
  auto func = HIRParser().ParseHIR(hir_source);
  ASSERT_NE(func, nullptr);
  ASSERT_TRUE(checkFunc(*func, std::cerr));
  reflowTypes(*func);
  ASSERT_FALSE(funcTypeChecks(*func, err));
  EXPECT_EQ(err.str(), expected_err);
}

TEST(FuncTypeCheckTest, RefinedTuplePassesTypeVerification) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0:Object = LoadArg<0>
    v1 = RefineType<TupleExact> v0
    CondBranch<2, 1> v1
  }

  bb 2 (preds 0) {
    v2:Object = LoadTupleItem<0> v1
    Return v2
  }

  bb 1 (preds 0) {
    Deopt
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(funcTypeCheckPasses(hir_source));
}

TEST(FuncTypeCheckTest, IntBinaryOpWithBottomPassesTypeVerification) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0:Object = LoadArg<0>
    v1 = RefineType<Bottom> v0
    v2 = LoadConst<CInt8[10]>
    v3 = IntBinaryOp<Add> v1 v2
    Deopt
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(funcTypeCheckPasses(hir_source));
}

TEST(FuncTypeCheckTest, UnRefinedTupleFailsTypeVerification) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0:Object = LoadArg<0>
    CondBranch<2, 1> v0
  }

  bb 2 (preds 0) {
    v1:Object = LoadTupleItem<0> v0
    Return v1
  }

  bb 1 (preds 0) {
    Deopt
  }
}
)";
  const char* expected_err =
      "TYPE MISMATCH in bb 2 of 'test'\nInstr 'v1:Object = LoadTupleItem<0> "
      "v0' expected operand 0 to be of type "
      "Tuple but got Object from 'v0:Object = LoadArg<0>'\n";
  EXPECT_NO_FATAL_FAILURE(funcTypeCheckFails(hir_source, expected_err));
}

TEST(FuncTypeCheckTest, PrimitiveCompareExpectsSameTypes) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<CInt16[0]>
    v1 = LoadConst<CInt8[1]>
    v2 = PrimitiveCompare<LessThan> v0 v1
    Deopt
  }
}
)";
  const char* expected_err =
      "TYPE MISMATCH in bb 0 of 'test'\nInstr 'v2:CBool = "
      "PrimitiveCompare<LessThan> v0 v1' expected join of operands of type "
      "{CInt16|CInt8} to subclass 'Primitive'\n";
  EXPECT_NO_FATAL_FAILURE(funcTypeCheckFails(hir_source, expected_err));
}

TEST(FuncTypeCheckTest, PrimitiveCompareHandlesDifferentSpecializations) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadConst<CInt8[5]>
    v1 = LoadConst<CInt8[1]>
    v2 = PrimitiveCompare<LessThan> v0 v1
    Deopt
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(funcTypeCheckPasses(hir_source));
}

TEST(FuncTypeCheckTest, PrimitiveCompareExpectsPrimitives) {
  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadArg<1>
    v2 = PrimitiveCompare<LessThan> v0 v1
    Deopt
  }
}
)";
  const char* expected_err =
      "TYPE MISMATCH in bb 0 of 'test'\nInstr 'v2:CBool = "
      "PrimitiveCompare<LessThan> v0 v1' expected join of operands of type "
      "Object to subclass 'Primitive'\n";
  EXPECT_NO_FATAL_FAILURE(funcTypeCheckFails(hir_source, expected_err));
}
