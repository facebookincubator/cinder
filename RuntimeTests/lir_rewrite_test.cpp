// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/lir/postgen.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

using namespace jit;

namespace jit::lir {
class LIRRewriteTest : public RuntimeTest {};

TEST_F(LIRRewriteTest, RewriteCondBranchTest) {
  Function f;
  BasicBlock basicblock(&f);

  auto instra =
      basicblock.allocateInstr(Instruction::kBind, nullptr, lir::OutVReg());
  basicblock.allocateInstr(
      Instruction::kEqual,
      nullptr,
      lir::OutVReg(),
      lir::VReg(instra),
      lir::VReg(instra));
  basicblock.allocateInstr(
      Instruction::kCondBranch, nullptr, lir::VReg(instra));

  PostGenerationRewrite::rewriteCondBranch(
      std::next(basicblock.instructions().end(), -1));
}
} // namespace jit::lir
