// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "cinderx/Jit/codegen/environ.h"
#include "cinderx/Jit/lir/parser.h"
#include "cinderx/Jit/lir/postalloc.h"
#include "cinderx/Jit/lir/verify.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

using namespace jit;

namespace jit::lir {
class LIRPostAllocRewriteTest : public RuntimeTest {};

TEST_F(LIRPostAllocRewriteTest, TestInsertBranchForSuccessorsInCondBranch) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1 %2
       CondBranch RAX:Object, BB%1, BB%2
BB %1 - preds: %0 - succs: %3 %4
       CondBranch RAX:Object, BB%3, BB%4
BB %2 - preds: %0 - succs: %3 %4
       CondBranch RAX:Object, BB%3, BB%4
BB %3 - preds: %1 %2
       RAX = Move RDI:Object
BB %4 - preds: %1 %2
       RAX = Move R13:Object
)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  parsed_func->sortBasicBlocks();

  jit::codegen::Environ env_;
  PostRegAllocRewrite post_rewrite(parsed_func.get(), &env_);
  post_rewrite.run();

  std::stringstream ss;
  ss << *parsed_func;
  auto expected_lir_str = fmt::format(R"(Function:
BB %0 - succs: %1 %2
                   Test RAX:Object, RAX:Object
                   BranchNZ BB%1

BB %2 - preds: %0 - succs: %3 %4
                   Test RAX:Object, RAX:Object
                   BranchNZ BB%3
                   Branch BB%4

BB %1 - preds: %0 - succs: %3 %4
                   Test RAX:Object, RAX:Object
                   BranchZ BB%4

BB %3 - preds: %1 %2
      RAX:Object = Move RDI:Object

BB %4 - preds: %1 %2
      RAX:Object = Move R13:Object

)");
  ASSERT_EQ(expected_lir_str, ss.str());
  ASSERT_TRUE(verifyPostRegAllocInvariants(parsed_func.get(), std::cout));
}

TEST_F(
    LIRPostAllocRewriteTest,
    TestInsertBranchForSuccessorsInCondBranchDifferentSection) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1 %2 - section: .text
       CondBranch RAX:Object, BB%1, BB%2
BB %1 - preds: %0 - section: .coldtext
       RAX:Object = Move R13:Object
BB %2 - preds: %0
       RAX:Object = Move RDI:Object
)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  parsed_func->sortBasicBlocks();

  jit::codegen::Environ env_;
  PostRegAllocRewrite post_rewrite(parsed_func.get(), &env_);
  post_rewrite.run();

  std::stringstream ss;
  ss << *parsed_func;
  auto expected_lir_str = fmt::format(R"(Function:
BB %0 - succs: %1 %2
                   Test RAX:Object, RAX:Object
                   BranchZ BB%2
                   Branch BB%1

BB %1 - preds: %0 - section: .coldtext
      RAX:Object = Move R13:Object

BB %2 - preds: %0
      RAX:Object = Move RDI:Object

)");
  ASSERT_EQ(expected_lir_str, ss.str());
  ASSERT_TRUE(verifyPostRegAllocInvariants(parsed_func.get(), std::cout));
}

TEST_F(LIRPostAllocRewriteTest, TestInsertBranchInDifferentSection) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1 - section: .text
       RAX:Object = Move R13:Object
BB %1 - preds: %0 - section: .coldtext
       RAX:Object = Move RDI:Object
)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  parsed_func->sortBasicBlocks();

  jit::codegen::Environ env_;
  PostRegAllocRewrite post_rewrite(parsed_func.get(), &env_);
  post_rewrite.run();

  std::stringstream ss;
  ss << *parsed_func;
  auto expected_lir_str = fmt::format(R"(Function:
BB %0 - succs: %1
      RAX:Object = Move R13:Object
                   Branch BB%1

BB %1 - preds: %0 - section: .coldtext
      RAX:Object = Move RDI:Object

)");
  ASSERT_EQ(expected_lir_str, ss.str());
  ASSERT_TRUE(verifyPostRegAllocInvariants(parsed_func.get(), std::cout));
}

} // namespace jit::lir
