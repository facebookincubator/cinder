// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "cinderx/Jit/lir/parser.h"
#include "cinderx/Jit/lir/verify.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

using namespace jit;

namespace jit::lir {
class LIRVerifyTest : public RuntimeTest {};

TEST_F(LIRVerifyTest, TestImmediateFallthroughOK) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1
       %2:Object = Move[0x5]:Object
BB %1 - preds: %0
       %3:Object = Move [0x5]:Object
                   Return %3:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  parsed_func->sortBasicBlocks();
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), true);
}

TEST_F(LIRVerifyTest, TestNonImmediateFallthroughDisallowed) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %2
       %2:Object = Move[0x5]:Object
BB %1 - preds: %0
       %3:Object = Move [0x5]:Object
BB %2 - preds: %0
       %4:Object = Move [0x5]:Object
                   Return %2:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  testing::internal::CaptureStdout();
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), false);
  std::string output = testing::internal::GetCapturedStdout();
  ASSERT_TRUE(
      output ==
      "ERROR: Basic block 0 does not contain a jump to non-immediate successor "
      "2.\n");
}

TEST_F(LIRVerifyTest, TestSingleSuccessorOK) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1
       %2:Object = Move[0x5]:Object
BB %1 - preds: %0 - succs %2
       %3:Object = Move [0x5]:Object
BB %2 - preds: %1
       %4:Object = Move [0x5]:Object
                   Return %2:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), true);
}

TEST_F(LIRVerifyTest, TestAllSuccessorsChecked) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1 %2
       %2:Object = Move[0x5]:Object
BB %1 - preds: %0 - succs %2
       %3:Object = Move [0x5]:Object
BB %2 - preds: %0 %1
       %4:Object = Move [0x5]:Object
                   Return %2:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  testing::internal::CaptureStdout();
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), false);
  std::string output = testing::internal::GetCapturedStdout();
  ASSERT_TRUE(
      output ==
      "ERROR: Basic block 0 does not contain a jump to non-immediate successor "
      "2.\n");
}

TEST_F(LIRVerifyTest, TestExplicitBranchOK) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %2
       %2:Object = Move[0x5]:Object
       Branch BB%2
BB %1
       %3:Object = Move [0x5]:Object
BB %2 - preds: %0 %1
       %4:Object = Move [0x5]:Object
                   Return %2:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), true);
}

TEST_F(LIRVerifyTest, TestExplicitConditionalBranchOK) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1 %2
       %2:Object = Move[0x5]:Object
       BranchZ BB%2
BB %1
       %3:Object = Move [0x5]:Object
BB %2 - preds: %0 %1
       %4:Object = Move [0x5]:Object
                   Return %2:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), true);
}

TEST_F(LIRVerifyTest, TestFallthroughToBlockInDifferentSectionDisallowed) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %1 - section: .text
       %2:Object = Move[0x5]:Object
BB %1 - preds: %0 - section: .coldtext
       %3:Object = Move [0x5]:Object
                   Return %2:Object
)");
  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  testing::internal::CaptureStdout();
  ASSERT_EQ(verifyPostRegAllocInvariants(parsed_func.get(), std::cout), false);
  std::string output = testing::internal::GetCapturedStdout();
  ASSERT_TRUE(
      output ==
      "ERROR: Basic block 0 does not contain a jump to non-immediate successor "
      "1.\n");
}

} // namespace jit::lir
