// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <memory>
#include <string>
#include <utility>

#include "Jit/lir/operand.h"
#include "gtest/gtest.h"

#include "fixtures.h"
#include "testutil.h"

#include "Jit/codegen/environ.h"
#include "Jit/compiler.h"
#include "Jit/hir/hir.h"
#include "Jit/lir/generator.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/parser.h"
#include "Jit/ref.h"

#include <iostream>

#include <Python.h>
#include <asm-generic/errno-base.h>
#include <math.h>

using namespace asmjit;
using namespace jit;
using namespace jit::lir;

class LIRGeneratorTest : public RuntimeTest {
 public:
  std::string getLIRString(PyObject* func) {
    JIT_CHECK(
        PyFunction_Check(func),
        "trying to compile something that isn't a function");

    PyObject* globals = PyFunction_GetGlobals(func);
    if (!PyDict_CheckExact(globals)) {
      return nullptr;
    }

    PyObject* builtins =
        _PyFunction_GetBuiltins(reinterpret_cast<PyFunctionObject*>(func));
    if (!PyDict_CheckExact(builtins)) {
      return nullptr;
    }

    jit::hir::HIRBuilder hir_builder;
    std::unique_ptr<jit::hir::Function> irfunc(hir_builder.BuildHIR(func));
    if (irfunc == nullptr) {
      return nullptr;
    }

    Compiler::runPasses(*irfunc);

    jit::codegen::Environ env;
    jit::Runtime rt;

    env.rt = &rt;

    LIRGenerator lir_gen(irfunc.get(), &env);

    auto lir_func = lir_gen.TranslateFunction();

    std::stringstream ss;

    lir_func->sortBasicBlocks();
    ss << *lir_func << std::endl;
    return ss.str();
  }

  std::string removeCommentsAndWhitespace(const std::string& input_s) {
    std::istringstream iss(input_s);
    std::string line;
    std::string output_s;
    while (std::getline(iss, line)) {
      if (line.length() == 0) {
        // skip blank lines
        continue;
      } else if (line.length() > 0 && line.at(0) == '#') {
        // skip comments
        continue;
      } else {
        output_s += line + '\n';
      }
    }
    return output_s;
  }
};

TEST_F(LIRGeneratorTest, StaticLoadInteger) {
  const char* pycode = R"(
from __static__ import int64

def f() -> None:
  d: int64 = 12
)";

  Ref<PyObject> pyfunc(compileStaticAndGet(pycode, "f"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());
#ifdef Py_DEBUG
  auto bb = "BB %3 - preds: %0 - succs: %13 %14";
#else
  auto bb = "BB %3 - preds: %0 - succs: %10 %11";
#endif

  auto lir_expected = fmt::format(
      R"(Function:
BB %0 - succs: %3
       %1:Object = Bind R10:Object
       %2:Object = Bind R11:Object

{0}

# v3:Nullptr = LoadConst<Nullptr>
       %4:Object = Move 0(0x0):Object

# v4:CInt64[12] = LoadConst<CInt64[12]>
        %5:64bit = Move 12(0xc):Object

)",
      bb);
  // Note - we only check whether the LIR has the stuff we care about
  ASSERT_EQ(lir_str.substr(0, lir_expected.size()), lir_expected);
}

TEST_F(LIRGeneratorTest, StaticLoadDouble) {
  const char* pycode = R"(
from __static__ import double

def f() -> None:
  d: double = 3.1415
)";

  Ref<PyObject> pyfunc(compileStaticAndGet(pycode, "f"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());
#ifdef Py_DEBUG
  auto bb = "BB %3 - preds: %0 - succs: %14 %15";
#else
  auto bb = "BB %3 - preds: %0 - succs: %11 %12";
#endif

  auto lir_expected = fmt::format(
      R"(Function:
BB %0 - succs: %3
       %1:Object = Bind R10:Object
       %2:Object = Bind R11:Object

{0}

# v3:Nullptr = LoadConst<Nullptr>
       %4:Object = Move 0(0x0):Object

# v4:CDouble[3.1415] = LoadConst<CDouble[3.1415]>
        %5:64bit = Move 4614256447914709615(0x400921cac083126f):Object
       %6:Double = Move %5:64bit

)",
      bb);
  // Note - we only check whether the LIR has the stuff we care about
  ASSERT_EQ(lir_str.substr(0, lir_expected.size()), lir_expected);
}

TEST_F(LIRGeneratorTest, StaticBoxDouble) {
  const char* pycode = R"(
from __static__ import double, box

def f() -> float:
  d: double = 3.1415
  return box(d)
)";

  Ref<PyObject> pyfunc(compileStaticAndGet(pycode, "f"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());

  auto lir_expected = fmt::format(R"(Function:
BB %0 - succs: %3
       %1:Object = Bind R10:Object
       %2:Object = Bind R11:Object

BB %3 - preds: %0 - succs: %10

# v3:Nullptr = LoadConst<Nullptr>
       %4:Object = Move 0(0x0):Object

# v4:CDouble[3.1415] = LoadConst<CDouble[3.1415]>
        %5:64bit = Move 4614256447914709615(0x400921cac083126f):Object
       %6:Double = Move %5:64bit

# v6:OptFloatExact = PrimitiveBox<false> v4
       %7:Object = Call)");
  // Note - we only check whether the LIR has the stuff we care about
  ASSERT_EQ(lir_str.substr(0, lir_expected.size()), lir_expected);
}

TEST_F(LIRGeneratorTest, StaticAddDouble) {
  const char* pycode = R"(
from __static__ import double, box

def f() -> float:
  d: double = 1.14
  e: double = 2.00
  return box(d + e)
)";

  Ref<PyObject> pyfunc(compileStaticAndGet(pycode, "f"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());

  auto lir_expected = fmt::format(R"(Function:
BB %0 - succs: %3
       %1:Object = Bind R10:Object
       %2:Object = Bind R11:Object

BB %3 - preds: %0 - succs: %13

# v6:Nullptr = LoadConst<Nullptr>
       %4:Object = Move 0(0x0):Object

# v7:CDouble[1.14] = LoadConst<CDouble[1.14]>
        %5:64bit = Move 4607812922747849277(0x3ff23d70a3d70a3d):Object
       %6:Double = Move %5:64bit

# v9:CDouble[2.0] = LoadConst<CDouble[2.0]>
        %7:64bit = Move 4611686018427387904(0x4000000000000000):Object
       %8:Double = Move %7:64bit

# v11:CDouble = DoubleBinaryOp<Add> v7 v9
       %9:Double = Fadd %6:Double, %8:Double)");
  // Note - we only check whether the LIR has the stuff we care about
  ASSERT_EQ(lir_str.substr(0, lir_expected.size()), lir_expected);
}

// disabled due to unstable Guard instruction
TEST_F(LIRGeneratorTest, DISABLED_Fallthrough) {
  const char* src = R"(
def func2(x):
  y = 0
  if x:
    y = 100
  return y
)";

  Ref<PyObject> pyfunc(compileAndGet(src, "func2"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());

  auto lir_expected = fmt::format(
      R"(Function:
BB %0
              %1 = Bind RDI
              %2 = Bind RSI
              %3 = Bind RDX
              %4 = Bind R9
              %5 = Bind R10
              %6 = Bind R11

BB %7 - preds: %0
              %8 = Load %2, 0(0x0)
              %9 = Load %5, 8(0x8)
             %10 = Call {0}({0:#x}), %8
                   Guard 1(0x1), 0(0x0), %10, %9, %8
                   CondBranch %10, BB%14, BB%13

BB %13 - preds: %7

BB %14 - preds: %7
             %15 = Load %5, 16(0x10)

BB %16 - preds: %13 %14
             %17 = Phi (BB%14, %15), (BB%13, %9)
                   Call {1}({1:#x}), %17
                   Return %17

BB %20 - preds: %16
             RDI = Move %6


)",
      reinterpret_cast<uint64_t>(PyObject_IsTrue),
      reinterpret_cast<uint64_t>(Py_IncRef));
  ASSERT_EQ(lir_str, lir_expected);
}

// disabled due to unstable Guard instruction
TEST_F(LIRGeneratorTest, DISABLED_CondBranch) {
  const char* pycode = R"(
def func(x):
    if x:
        return True
    return False
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "func"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());

  auto lir_expected = fmt::format(
      R"(Function:
BB %0
              %1 = Bind RDI
              %2 = Bind RSI
              %3 = Bind RDX
              %4 = Bind R9
              %5 = Bind R10
              %6 = Bind R11

BB %7 - preds: %0
              %8 = Load %2, 0(0x0)
              %9 = Call {0}({0:#x}), %8
                   Guard 1(0x1), 0(0x0), %9, %8
                   CondBranch %9, BB%16, BB%12

BB %12 - preds: %7
             %13 = Load %5, 16(0x10)
                   Call {1}({1:#x}), %13
                   Return %13

BB %16 - preds: %7
             %17 = Load %5, 8(0x8)
                   Call {1}({1:#x}), %17
                   Return %17

BB %20 - preds: %12 %16
             RDI = Move %6


)",
      reinterpret_cast<uint64_t>(PyObject_IsTrue),
      reinterpret_cast<uint64_t>(Py_IncRef));
  ASSERT_EQ(lir_str, lir_expected);
}

TEST_F(LIRGeneratorTest, ParserDataTypeTest) {
  auto lir_str = fmt::format(R"(Function:
BB %0 - succs: %7 %10
         %1:8bit = Bind RDI:8bit
        %2:32bit = Bind RSI:32bit
        %3:16bit = Bind R9:16bit
        %4:64bit = Bind R10:64bit
       %5:Object = Move 0(0x0):Object
                   CondBranch %5:Object, BB%7, BB%10

BB %7 - preds: %0 - succs: %10
       %8:Object = Move [0x5]:Object
                   Return %8:Object

BB %10 - preds: %0 %7

)");

  Parser parser;
  auto parsed_func = parser.parse(lir_str);
  std::stringstream ss;
  parsed_func->sortBasicBlocks();
  ss << *parsed_func;
  // Assume that the parser assigns basic block and register numbers
  // based on the parsing order of the instructions.
  // If the parser behavior is modified and assigns numbers differently,
  // then the assert may fail.
  ASSERT_EQ(lir_str, ss.str());
}

TEST_F(LIRGeneratorTest, ParserMemIndTest) {
  auto lir_str = fmt::format(R"(Function:
BB %0
        %1:64bit = Bind RDI:Object
        %2:64bit = Move [RDI:Object + RSI:Object * 8 + 0x8]:Object
        %3:64bit = Move [%2:64bit + 0x3]:Object
        %4:64bit = Move [%2:64bit + %3:64bit * 16]:Object
[%4:64bit - 0x16]:Object = Move [RAX:Object + %4:64bit]:Object

)");

  Parser parser;
  auto parsed_func = parser.parse(lir_str);
  std::stringstream ss;
  parsed_func->sortBasicBlocks();
  ss << *parsed_func;
  // Assume that the parser assigns basic block and register numbers
  // based on the parsing order of the instructions.
  // If the parser behavior is modified and assigns numbers differently,
  // then the assert may fail.
  ASSERT_EQ(lir_str, ss.str());
}

// TODO(tiansi): The parser does not recognize the new instructions.
// I'm planning to fix and improve LIR printing/parsing with a
// separate diff. Disabled this test for now.
TEST_F(LIRGeneratorTest, ParserTest) {
  const char* pycode = R"(
def func(x):
    if x:
        return True
    return False
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "func"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = removeCommentsAndWhitespace(getLIRString(pyfunc.get()));

  Parser parser;
  auto parsed_func = parser.parse(lir_str);
  std::stringstream ss;
  parsed_func->sortBasicBlocks();
  ss << *parsed_func;
  ASSERT_EQ(lir_str, removeCommentsAndWhitespace(ss.str()));
}

template <typename... Args>
bool MemoryIndirectTestCase(std::string_view expected, Args&&... args) {
  jit::lir::MemoryIndirect im(nullptr);
  im.setMemoryIndirect(std::forward<Args>(args)...);
  auto output = fmt::format("{}", im);
  return output == expected;
}

TEST(LIRTest, MemoryIndirectTests) {
  ASSERT_TRUE(MemoryIndirectTestCase("[RCX:Object]", PhyLocation::RCX));
  ASSERT_TRUE(MemoryIndirectTestCase(
      "[RCX:Object + 0x7fff]", PhyLocation::RCX, 0x7fff));
  ASSERT_TRUE(MemoryIndirectTestCase(
      "[RCX:Object + RDX:Object]", PhyLocation::RCX, PhyLocation::RDX, 0));
  ASSERT_TRUE(MemoryIndirectTestCase(
      "[RCX:Object + RDX:Object * 4]", PhyLocation::RCX, PhyLocation::RDX, 2));
  ASSERT_TRUE(MemoryIndirectTestCase(
      "[RCX:Object + RDX:Object + 0x100]",
      PhyLocation::RCX,
      PhyLocation::RDX,
      0,
      0x100));
  ASSERT_TRUE(MemoryIndirectTestCase(
      "[RCX:Object + RDX:Object * 2 + 0x1000]",
      PhyLocation::RCX,
      PhyLocation::RDX,
      1,
      0x1000));
}
