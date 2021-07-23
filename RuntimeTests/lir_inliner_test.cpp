// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/instruction.h"
#include "gtest/gtest.h"

#include "Jit/codegen/inliner.h"
#include "Jit/lir/lir.h"

#include "Jit/ref.h"
#include "fixtures.h"
#include "testutil.h"

using namespace jit;
using namespace jit::lir;

namespace jit::codegen {
class LIRInlinerTest : public RuntimeTest {};

TEST_F(LIRInlinerTest, ResolveArgumentsTest) {
  auto caller = std::make_unique<Function>();
  auto caller_bb1 = caller->allocateBasicBlock();
  auto caller_r1 =
      caller_bb1->allocateInstr(Instruction::kMove, nullptr, OutVReg(), Imm(2));
  auto caller_r2 =
      caller_bb1->allocateInstr(Instruction::kMove, nullptr, OutVReg(), Imm(4));
  auto call_instr = caller_bb1->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(),
      Imm(123), // random call address
      Imm(1),
      VReg(caller_r1),
      Imm(3),
      VReg(caller_r2));

  // Let's temporarily add the callee basic blocks after the caller's
  auto bb1 = caller->allocateBasicBlock();
  auto a =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(0));
  auto b =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(1));
  auto c =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(2));
  auto d =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(3));

  // instructions that don't use arguments
  auto e = bb1->allocateInstr(Instruction::kMove, nullptr, OutVReg(), Imm(8));
  auto f = bb1->allocateInstr(Instruction::kMove, nullptr, OutVReg(), VReg(e));

  // use immediate argument
  auto g = bb1->allocateInstr(
      Instruction::kAdd, nullptr, OutVReg(), VReg(f), VReg(a));

  // indirect operands that contain linked argument
  bb1->allocateInstr(Instruction::kMove, nullptr, OutVReg(), Ind(b, c));
  bb1->allocateInstr(Instruction::kMove, nullptr, OutVReg(), Ind(c, b));

  // use linked argument
  auto h = bb1->allocateInstr(
      Instruction::kAdd, nullptr, OutVReg(), VReg(g), VReg(d));

  bb1->allocateInstr(Instruction::kReturn, nullptr, VReg(h));

  LIRInliner inliner(call_instr);
  inliner.callee_start_ = 1;
  inliner.callee_end_ = 2;
  inliner.resolveArguments();

  auto lir_expected = fmt::format(R"(Function:
BB %0
       %1:Object = Move 2(0x2):Object
       %2:Object = Move 4(0x4):Object
       %3:Object = Call 123(0x7b):Object, 1(0x1):Object, %1:Object, 3(0x3):Object, %2:Object

BB %4
       %5:Object = Move 1(0x1):64bit
       %7:Object = Move 3(0x3):64bit
       %9:Object = Move 8(0x8):Object
      %10:Object = Move %9:Object
      %11:Object = Add %10:Object, %5:Object
      %12:Object = Move [%1:Object + %7:Object]:Object
      %13:Object = Move [%7:Object + %1:Object]:Object
      %14:Object = Add %11:Object, %2:Object
                   Return %14:Object
)");
  std::stringstream ss;
  ss << *caller << std::endl;
  ASSERT_EQ(ss.str().substr(0, lir_expected.size()), lir_expected);
}

} // namespace jit::codegen
