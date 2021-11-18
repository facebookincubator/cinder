// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"
#include "opcode.h"

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
  EXPECT_EQ(op_type.other_idx, -1);
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
