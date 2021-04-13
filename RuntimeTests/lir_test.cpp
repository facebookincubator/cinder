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
};

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

// TODO(tiansi): The parser does not recognize the new instructions.
// I'm planning to fix and improve LIR printing/parsing with a
// separate diff. Disabled this test for now.
TEST_F(LIRGeneratorTest, DISABLED_ParserTest) {
  const char* pycode = R"(
def func(x):
    if x:
        return True
    return False
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "func"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto lir_str = getLIRString(pyfunc.get());

  Parser parser;
  auto parsed_func = parser.parse(lir_str);
  std::stringstream ss;
  parsed_func->sortBasicBlocks();
  ss << *parsed_func << std::endl;
  ASSERT_EQ(lir_str, ss.str());
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
