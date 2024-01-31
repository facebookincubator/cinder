// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/lir/dce.h"
#include "cinderx/Jit/lir/parser.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

using namespace jit;

namespace jit::lir {
class LIRDeadCodeEliminationTest : public RuntimeTest {};

TEST_F(LIRDeadCodeEliminationTest, TestEliminateMov) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %7 %10
         %1:8bit = Bind RDI:8bit
        %2:32bit = Bind RSI:32bit
        %3:16bit = Bind R9:16bit
        %4:64bit = Bind R10:64bit
       %5:Object = Move 0(0x0):Object
                   CondBranch %5:Object, BB%7, BB%10
       %6:Object = Move 0(0x0):Object

BB %7 - preds: %0 - succs: %10
       %8:Object = Move [0x5]:Object
                   Return %8:Object

BB %10 - preds: %0 %7

)");
  auto lir_expected_str = fmt::format(R"(Function:
BB %0 - succs: %7 %10
       %5:Object = Move 0(0x0):Object
                   CondBranch %5:Object, BB%7, BB%10

BB %7 - preds: %0 - succs: %10
       %8:Object = Move [0x5]:Object
                   Return %8:Object

BB %10 - preds: %0 %7

)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  eliminateDeadCode(parsed_func.get());
  std::stringstream ss;
  ss << *parsed_func;
  // Assume that the parser assigns basic block and register numbers
  // based on the parsing order of the instructions.
  // If the parser behavior is modified and assigns numbers differently,
  // then the assert may fail.
  ASSERT_EQ(lir_expected_str, ss.str());
}

TEST_F(LIRDeadCodeEliminationTest, TestLocalBaseForIndirectNotEliminated) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %8 %10
         %1:8bit = Bind RDI:8bit
        %2:32bit = Bind RSI:32bit
        %3:16bit = Bind R9:16bit
        %4:64bit = Bind R10:64bit
       %5:Object = Move 0(0x0):Object
       %6:Object = Move 0(0x0):Object
       %7:Object = Move [%5:Object + 0x18]:Object
                   CondBranch %7:Object, BB%8, BB%10

BB %8 - preds: %0 - succs: %10
       %9:Object = Move [0x5]:Object
                   Return %9:Object

BB %10 - preds: %0 %8

)");
  auto lir_expected_str = fmt::format(R"(Function:
BB %0 - succs: %8 %10
       %5:Object = Move 0(0x0):Object
       %7:Object = Move [%5:Object + 0x18]:Object
                   CondBranch %7:Object, BB%8, BB%10

BB %8 - preds: %0 - succs: %10
       %9:Object = Move [0x5]:Object
                   Return %9:Object

BB %10 - preds: %0 %8

)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  eliminateDeadCode(parsed_func.get());
  std::stringstream ss;
  ss << *parsed_func;
  // Assume that the parser assigns basic block and register numbers
  // based on the parsing order of the instructions.
  // If the parser behavior is modified and assigns numbers differently,
  // then the assert may fail.
  ASSERT_EQ(lir_expected_str, ss.str());
}

TEST_F(LIRDeadCodeEliminationTest, TestLocalIndexForIndirectNotEliminated) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0 - succs: %8 %10
         %1:8bit = Bind RDI:8bit
        %2:32bit = Bind RSI:32bit
        %3:16bit = Bind R9:16bit
        %4:64bit = Bind R10:64bit
       %5:Object = Move 0(0x0):Object
       %6:Object = Move 0(0x0):Object
       %7:Object = Move [RDI:Object + %6:Object]:Object
                   CondBranch %7:Object, BB%8, BB%10

BB %8 - preds: %0 - succs: %10
       %9:Object = Move [0x5]:Object
                   Return %9:Object

BB %10 - preds: %0 %8

)");
  auto lir_expected_str = fmt::format(R"(Function:
BB %0 - succs: %8 %10
       %6:Object = Move 0(0x0):Object
       %7:Object = Move [RDI:Object + %6:Object]:Object
                   CondBranch %7:Object, BB%8, BB%10

BB %8 - preds: %0 - succs: %10
       %9:Object = Move [0x5]:Object
                   Return %9:Object

BB %10 - preds: %0 %8

)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  eliminateDeadCode(parsed_func.get());
  std::stringstream ss;
  ss << *parsed_func;
  // Assume that the parser assigns basic block and register numbers
  // based on the parsing order of the instructions.
  // If the parser behavior is modified and assigns numbers differently,
  // then the assert may fail.
  ASSERT_EQ(lir_expected_str, ss.str());
}

TEST_F(
    LIRDeadCodeEliminationTest,
    TestLocalBaseForIndirectNotEliminatedInOutput) {
  auto lir_input_str = fmt::format(R"(Function:
BB %0
         %1:8bit = Bind RDI:8bit
         %2:32bit = Bind RSI:32bit
         %3:16bit = Bind R9:16bit
         %4:64bit = Bind R10:64bit
         %5:Object = Move 0(0x0):Object
         %6:Object = Move 0(0x0):Object
         [%5:Object + 0x18]:Object = Move %4:64bit

)");
  auto lir_expected_str = fmt::format(R"(Function:
BB %0
        %4:64bit = Bind R10:64bit
       %5:Object = Move 0(0x0):Object
[%5:Object + 0x18]:Object = Move %4:64bit

)");

  Parser parser;
  auto parsed_func = parser.parse(lir_input_str);
  eliminateDeadCode(parsed_func.get());
  std::stringstream ss;
  ss << *parsed_func;
  // Assume that the parser assigns basic block and register numbers
  // based on the parsing order of the instructions.
  // If the parser behavior is modified and assigns numbers differently,
  // then the assert may fail.
  ASSERT_EQ(lir_expected_str, ss.str());
}

} // namespace jit::lir
