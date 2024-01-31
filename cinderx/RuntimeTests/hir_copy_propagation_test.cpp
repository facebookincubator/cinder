// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/hir/optimization.h"
#include "cinderx/Jit/hir/parser.h"
#include "cinderx/Jit/hir/printer.h"
#include "cinderx/Jit/hir/ssa.h"

using namespace jit::hir;

TEST(CopyPropagationTest, EliminatesCopies) {
  // TODO(bsimmers): This can be converted to a .txt-based pass test when we
  // lower directly to SSA.

  const char* hir_source = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadArg<1>
    v2 = Assign v1
    CondBranch<1, 2> v1
  }
  bb 1 {
    v3 = Assign v1
    Branch<3>
  }
  bb 2 {
    v4 = Assign v2
    Branch<3>
  }
  bb 3 {
    v5 = Phi<1, 2> v3 v4
    Return v5
  }
}
)";

  const char* expected_hir = R"(fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadArg<1>
    CondBranch<1, 2> v1
  }

  bb 1 (preds 0) {
    Branch<3>
  }

  bb 2 (preds 0) {
    Branch<3>
  }

  bb 3 (preds 1, 2) {
    v5 = Phi<1, 2> v1 v1
    Return v5
  }
}
)";

  auto func = HIRParser().ParseHIR(hir_source);
  ASSERT_NE(func, nullptr);
  ASSERT_TRUE(checkFunc(*func, std::cout));

  CopyPropagation copy_prop;
  copy_prop.Run(*func);

  EXPECT_EQ(HIRPrinter().ToString(*func), expected_hir);
}
