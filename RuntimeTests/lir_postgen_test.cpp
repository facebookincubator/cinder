// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include <gtest/gtest.h>

#include "Jit/codegen/environ.h"
#include "Jit/lir/function.h"
#include "Jit/lir/parser.h"
#include "Jit/lir/postgen.h"

#include "RuntimeTests/fixtures.h"

namespace jit::lir {
class LIRPostGenerationRewriteTest : public RuntimeTest {};

static std::string runPostGenRewrite(const char* lir_input_str) {
  auto func = Parser().parse(lir_input_str);
  codegen::Environ env;
  PostGenerationRewrite(func.get(), &env).run();
  return fmt::format("{}", *func);
}

TEST_F(LIRPostGenerationRewriteTest, RetainsLoadSecondCallResultDataType) {
  const char* lir_input_str = R"(Function:
BB %0
  %10 = Call 0
  %11:16bit = LoadSecondCallResult %10
  Return %11
)";

  const char* expected_lir_str = R"(Function:
BB %0
      %10:Object = Call 0(0x0):64bit
       %11:16bit = Move RDX:16bit
                   Return %11:16bit

)";

  EXPECT_EQ(runPostGenRewrite(lir_input_str), expected_lir_str);
}

TEST_F(LIRPostGenerationRewriteTest, DoesNotAllowMultipleLSCRPerCall) {
  const char* lir_input_str = R"(Function:
BB %0
  %10 = Call 0
  %11 = LoadSecondCallResult %10
  CondBranch %11, BB%1, BB%2
BB %1
  %12 = LoadSecondCallResult %10
  Return %12
BB %2
  Return %10
)";

  EXPECT_DEATH(
      runPostGenRewrite(lir_input_str),
      "Call output consumed by multiple LoadSecondCallResult instructions");
}

TEST_F(LIRPostGenerationRewriteTest, RewritesLoadSecondCallResultThroughPhis) {
  const char* lir_input_str = R"(Function:
BB %0
  %10 = Call 0
  CondBranch %10, BB%1, BB%2
BB %1
  %11 = Call 0
  CondBranch %11, BB%3, BB%4
BB %2
  %12 = Call 0
  CondBranch %12, BB%20, BB%21
BB %20
  %120 = Call 0
  Branch BB%22
BB %21
  %121 = Call 0
  Branch BB%22
BB %22
  %122 = Phi BB%20, %120, BB%21, %121
  Branch BB%5
BB %3
  Call 0
  Branch BB%5
BB %4
  Call 0
  Branch BB%5
BB %5
  %13 = Phi BB%22, %122, BB%3, %11, BB%4, %11, BB%6, %13
  %14:32bit = LoadSecondCallResult %13
  Branch BB%6
BB %6
  Call 0
  Branch BB%5
)";

  const char* expected_lir_str = R"(Function:
BB %0
      %10:Object = Call 0(0x0):64bit
                   CondBranch %10:Object, BB%1, BB%2

BB %1
      %11:Object = Call 0(0x0):64bit
      %139:32bit = Move RDX:32bit
                   CondBranch %11:Object, BB%3, BB%4

BB %2
      %12:Object = Call 0(0x0):64bit
                   CondBranch %12:Object, BB%20, BB%21

BB %20
     %120:Object = Call 0(0x0):64bit
      %137:32bit = Move RDX:32bit
                   Branch BB%22

BB %21
     %121:Object = Call 0(0x0):64bit
      %138:32bit = Move RDX:32bit
                   Branch BB%22

BB %22
     %122:Object = Phi (BB%20, %120:Object), (BB%21, %121:Object)
      %136:32bit = Phi (BB%20, %137:32bit), (BB%21, %138:32bit)
                   Branch BB%5

BB %3
                   Call 0(0x0):64bit
                   Branch BB%5

BB %4
                   Call 0(0x0):64bit
                   Branch BB%5

BB %5
      %13:Object = Phi (BB%22, %122:Object), (BB%3, %11:Object), (BB%4, %11:Object), (BB%6, %13:Object)
       %14:32bit = Phi (BB%22, %136:32bit), (BB%3, %139:32bit), (BB%4, %139:32bit), (BB%6, %14:32bit)
                   Branch BB%6

BB %6
                   Call 0(0x0):64bit
                   Branch BB%5

)";

  EXPECT_EQ(runPostGenRewrite(lir_input_str), expected_lir_str);
}

} // namespace jit::lir
