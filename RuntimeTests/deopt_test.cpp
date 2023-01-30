// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"
#include "opcode.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/compiler.h"
#include "Jit/deopt.h"
#include "Jit/disassembler.h"
#include "Jit/hir/builder.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"
#include "Jit/log.h"
#include "Jit/ref.h"
#include "Jit/util.h"

#include "RuntimeTests/fixtures.h"

#include <asmjit/asmjit.h>

#include <algorithm>

using namespace jit;
using namespace jit::hir;
using namespace jit::codegen;
using jit::kPointerSize;

class ReifyFrameTest : public RuntimeTest {};

TEST_F(ReifyFrameTest, ReifyAtEntry) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);

  uint64_t regs[PhyLocation::NUM_GP_REGS];

  auto a = Ref<>::steal(PyLong_FromLong(10));
  ASSERT_NE(a, nullptr);
  regs[PhyLocation::RDI] = reinterpret_cast<uint64_t>(a.get());
  LiveValue a_val{
      PhyLocation{PhyLocation::RDI},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  auto b = Ref<>::steal(PyLong_FromLong(20));
  ASSERT_NE(b, nullptr);
  regs[PhyLocation::RSI] = reinterpret_cast<uint64_t>(b.get());
  LiveValue b_val{
      PhyLocation{PhyLocation::RSI},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  PyCodeObject* code =
      reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(func));
  CodeRuntime code_rt{func, FrameMode::kNormal};

  DeoptMetadata dm;
  dm.live_values = {a_val, b_val};
  DeoptFrameMetadata dfm;
  dfm.localsplus = {0, 1};
  dfm.next_instr_offset = BCOffset{0};
  dm.frame_meta.push_back(dfm);
  dm.code_rt = &code_rt;

  PyThreadState* tstate = PyThreadState_Get();
  auto frame = Ref<PyFrameObject>::steal(
      PyFrame_New(tstate, code, PyFunction_GetGlobals(func), nullptr));

  reifyFrame(frame, dm, dfm, regs);

  auto result = Ref<>::steal(PyEval_EvalFrame(frame));
  ASSERT_NE(result, nullptr);
  ASSERT_TRUE(PyLong_CheckExact(result));
  ASSERT_EQ(PyLong_AsLong(result), 30);
}

TEST_F(ReifyFrameTest, ReifyMidFunction) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);

  uint64_t regs[PhyLocation::NUM_GP_REGS];

  auto a = Ref<>::steal(PyLong_FromLong(10));
  ASSERT_NE(a, nullptr);
  regs[PhyLocation::RDI] = reinterpret_cast<uint64_t>(a.get());
  LiveValue a_val{
      PhyLocation{PhyLocation::RDI},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  auto b = Ref<>::steal(PyLong_FromLong(20));
  ASSERT_NE(b, nullptr);
  regs[PhyLocation::RSI] = reinterpret_cast<uint64_t>(b.get());
  LiveValue b_val{
      PhyLocation{PhyLocation::RSI},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  PyCodeObject* code =
      reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(func));
  CodeRuntime code_rt{func, FrameMode::kNormal};

  DeoptMetadata dm;
  dm.live_values = {a_val, b_val};
  DeoptFrameMetadata dfm;
  dfm.localsplus = {0, 1};
  dfm.stack = {0, 1};
  dfm.next_instr_offset = BCOffset{4};
  dm.frame_meta.push_back(dfm);
  dm.code_rt = &code_rt;

  PyThreadState* tstate = PyThreadState_Get();
  auto frame = Ref<PyFrameObject>::steal(
      PyFrame_New(tstate, code, PyFunction_GetGlobals(func), nullptr));

  reifyFrame(frame, dm, dfm, regs);

  auto result = Ref<>::steal(PyEval_EvalFrame(frame));
  ASSERT_NE(result, nullptr);
  ASSERT_TRUE(PyLong_CheckExact(result));
  ASSERT_EQ(PyLong_AsLong(result), 30);
}

TEST_F(ReifyFrameTest, ReifyWithMemoryValues) {
  const char* src = R"(
def test(a, b):
  return a + b
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);

  uint64_t mem[2];
  uint64_t regs[PhyLocation::NUM_GP_REGS];
  regs[PhyLocation::RBP] = reinterpret_cast<uint64_t>(mem) + sizeof(mem);

  auto a = Ref<>::steal(PyLong_FromLong(10));
  ASSERT_NE(a, nullptr);
  LiveValue a_val{
      PhyLocation{-2 * kPointerSize},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};
  mem[0] = reinterpret_cast<uint64_t>(a.get());

  auto b = Ref<>::steal(PyLong_FromLong(20));
  ASSERT_NE(b, nullptr);
  LiveValue b_val{
      PhyLocation{-kPointerSize},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};
  mem[1] = reinterpret_cast<uint64_t>(b.get());

  PyCodeObject* code =
      reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(func));
  CodeRuntime code_rt{func, FrameMode::kNormal};

  DeoptMetadata dm;
  dm.live_values = {a_val, b_val};
  DeoptFrameMetadata dfm;
  dfm.localsplus = {0, 1};
  dfm.stack = {0, 1};
  dfm.next_instr_offset = BCOffset{4};
  dm.frame_meta.push_back(dfm);
  dm.code_rt = &code_rt;

  PyThreadState* tstate = PyThreadState_Get();
  PyObject* globals = PyFunction_GetGlobals(func);
  auto frame =
      Ref<PyFrameObject>::steal(PyFrame_New(tstate, code, globals, nullptr));

  reifyFrame(frame, dm, dfm, regs);

  auto result = Ref<>::steal(PyEval_EvalFrame(frame));
  ASSERT_NE(result, nullptr);
  ASSERT_TRUE(PyLong_CheckExact(result));
  ASSERT_EQ(PyLong_AsLong(result), 30);
}

TEST_F(ReifyFrameTest, ReifyInLoop) {
  const char* src = R"(
def test(num):
  fact = 1
  while num > 1:
    fact *= num
    num -= 1
  return fact
)";
  Ref<PyFunctionObject> func(compileAndGet(src, "test"));
  ASSERT_NE(func, nullptr);

  uint64_t regs[PhyLocation::NUM_GP_REGS];
  auto num = Ref<>::steal(PyLong_FromLong(3));
  ASSERT_NE(num, nullptr);
  regs[PhyLocation::RDI] = reinterpret_cast<uint64_t>(num.get());
  LiveValue num_val{
      PhyLocation{PhyLocation::RDI},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  auto fact = Ref<>::steal(PyLong_FromLong(20));
  ASSERT_NE(fact, nullptr);
  regs[PhyLocation::RSI] = reinterpret_cast<uint64_t>(fact.get());
  LiveValue fact_val{
      PhyLocation{PhyLocation::RSI},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  auto tmp = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(tmp, nullptr);
  regs[PhyLocation::RDX] = reinterpret_cast<uint64_t>(tmp.get());
  LiveValue tmp_val{
      PhyLocation{PhyLocation::RDX},
      RefKind::kOwned,
      ValueKind::kObject,
      LiveValue::Source::kUnknown};

  PyCodeObject* code =
      reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(func));
  CodeRuntime code_rt{func, FrameMode::kNormal};

  DeoptMetadata dm;
  dm.live_values = {num_val, fact_val, tmp_val};
  DeoptFrameMetadata dfm;
  dfm.localsplus = {0, 1};
  dfm.stack = {0, 2};
  dfm.next_instr_offset = BCOffset{8};
  dm.frame_meta.push_back(dfm);
  dm.code_rt = &code_rt;

  PyThreadState* tstate = PyThreadState_Get();
  PyObject* globals = PyFunction_GetGlobals(func);
  auto frame =
      Ref<PyFrameObject>::steal(PyFrame_New(tstate, code, globals, nullptr));

  reifyFrame(frame, dm, dfm, regs);

  auto result = Ref<>::steal(PyEval_EvalFrame(frame));
  ASSERT_NE(result, nullptr);
  ASSERT_TRUE(PyLong_CheckExact(result));
  ASSERT_EQ(PyLong_AsLong(result), 120);
}

TEST_F(ReifyFrameTest, ReifyStaticCompareWithBool) {
  const char* src = R"(
from __static__ import size_t, unbox
def test(x, y):
    x1: size_t = unbox(x)
    y1: size_t = unbox(y)

    if x1 > y1:
        return True
    return False
)";
  Ref<PyFunctionObject> func(compileStaticAndGet(src, "test"));
  if (PyErr_Occurred()) {
    PyErr_Print();
  }
  ASSERT_NE(func, nullptr);

  uint64_t regs[PhyLocation::NUM_GP_REGS];

  for (int i = 0; i < 2; i++) {
    regs[PhyLocation::RDI] = i;
    LiveValue a_val{
        PhyLocation{PhyLocation::RDI},
        RefKind::kUncounted,
        ValueKind::kBool,
        LiveValue::Source::kUnknown};

    PyCodeObject* code =
        reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(func));
    const int jump_index = 20;
    ASSERT_EQ(PyBytes_AS_STRING(code->co_code)[24], (char)POP_JUMP_IF_ZERO);

    CodeRuntime code_rt{func, FrameMode::kNormal};

    DeoptMetadata dm;
    dm.live_values = {a_val};
    DeoptFrameMetadata dfm;
    dfm.localsplus = {0};
    dfm.stack = {0};
    dfm.next_instr_offset = BCOffset(jump_index);
    dm.frame_meta.push_back(dfm);
    dm.code_rt = &code_rt;

    PyThreadState* tstate = PyThreadState_Get();
    auto frame = Ref<PyFrameObject>::steal(
        PyFrame_New(tstate, code, PyFunction_GetGlobals(func), nullptr));

    reifyFrame(frame, dm, dfm, regs);

    auto result = Ref<>::steal(PyEval_EvalFrame(frame));
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(PyBool_Check(result));
    ASSERT_EQ(result, i ? Py_True : Py_False);
  }
}

class DeoptStressTest : public RuntimeTest {
 public:
  void runTest(
      const char* src,
      PyObject** args,
      Py_ssize_t nargs,
      PyObject* expected) {
    Ref<PyFunctionObject> funcobj(compileAndGet(src, "test"));
    ASSERT_NE(funcobj, nullptr);
    std::unique_ptr<Function> irfunc(buildHIR(funcobj));
    ASSERT_NE(irfunc, nullptr);
    auto guards = insertDeopts(*irfunc);
    jit::Compiler::runPasses(*irfunc, PassConfig::kDefault);
    auto delete_one_deopt = [&](const DeoptMetadata& deopt_meta) {
      auto it = guards.find(deopt_meta.nonce);
      JIT_CHECK(it != guards.end(), "no guard for nonce %d", deopt_meta.nonce);
      it->second->unlink();
      delete it->second;
      guards.erase(it);
    };
    Runtime* ngen_rt = Runtime::get();
    auto pyfunc = reinterpret_cast<PyFunctionObject*>(funcobj.get());
    while (!guards.empty()) {
      NativeGenerator gen(irfunc.get());
      auto jitfunc = reinterpret_cast<vectorcallfunc>(gen.getVectorcallEntry());
      ASSERT_NE(jitfunc, nullptr);
      ngen_rt->setGuardFailureCallback(delete_one_deopt);
      auto res =
          jitfunc(reinterpret_cast<PyObject*>(pyfunc), args, nargs, NULL);
      ngen_rt->clearGuardFailureCallback();
      if ((res == nullptr) ||
          (PyObject_RichCompareBool(res, expected, Py_EQ) < 1)) {
        dumpDebuggingOutput(*irfunc, res, expected);
        FAIL();
      }
      Py_XDECREF(res);
    }
  }

 private:
  std::unordered_map<int, Instr*> insertDeopts(Function& irfunc) {
    std::unordered_map<int, Instr*> guards;
    Register* reg = irfunc.env.AllocateRegister();
    int next_nonce{0};
    for (auto& block : irfunc.cfg.blocks) {
      bool has_periodic_tasks =
          std::any_of(block.begin(), block.end(), [](auto& instr) {
            return instr.IsRunPeriodicTasks();
          });
      if (has_periodic_tasks) {
        // skip blocks that depend on the contents of the eval breaker
        continue;
      }
      for (auto it = block.begin(); it != block.end();) {
        auto& instr = *it++;
        if (instr.getDominatingFrameState() != nullptr) {
          // Nothing defines reg, so it will be null initialized and the guard
          // will fail, thus causing deopt.
          auto guard = Guard::create(reg);
          guard->InsertBefore(instr);
          auto nonce = next_nonce++;
          guard->set_nonce(nonce);
          guards[nonce] = guard;
        }
      }
    }
    return guards;
  }

  void dumpDebuggingOutput(
      const Function& irfunc,
      PyObject* actual,
      PyObject* expected) {
    auto expected_str = Ref<>::steal(PyObject_ASCII(expected));
    ASSERT_NE(expected_str, nullptr);
    std::cerr << "Expected: " << PyUnicode_AsUTF8(expected_str) << std::endl;
    std::cerr << "Actual: ";
    if (actual != nullptr) {
      auto actual_str = Ref<>::steal(PyObject_ASCII(actual));
      ASSERT_NE(actual_str, nullptr);
      std::cerr << PyUnicode_AsUTF8(actual_str) << std::endl;
    } else {
      std::cerr << "nullptr";
    }
    std::cerr << std::endl;
    std::cerr << "HIR of failed function:" << std::endl;
    std::cerr << HIRPrinter().ToString(irfunc) << std::endl;
    std::cerr << "Disassembly:" << std::endl;
    // Recompile so we get the annotated disassembly
    auto old_disas_funcs = jit::g_dump_asm;
    jit::g_dump_asm = 1;
    NativeGenerator gen(&irfunc);
    gen.getVectorcallEntry();
    jit::g_dump_asm = old_disas_funcs;
    std::cerr << std::endl;
    std::cerr << "Python traceback: ";
    PyErr_Print();
    std::cerr << std::endl;
  }
};

TEST_F(DeoptStressTest, BinaryOps) {
  const char* src = R"(
def test(a, b, c):
  return a + b + c
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  auto arg2 = Ref<>::steal(PyLong_FromLong(200));
  auto arg3 = Ref<>::steal(PyLong_FromLong(300));
  PyObject* args[] = {arg1, arg2, arg3};
  auto result = Ref<>::steal(PyLong_FromLong(600));
  runTest(src, args, 3, result);
}

TEST_F(DeoptStressTest, InPlaceOps) {
  const char* src = R"(
def test(a, b, c):
  res = 0
  res += a
  res += b
  res += c
  return res
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  auto arg2 = Ref<>::steal(PyLong_FromLong(200));
  auto arg3 = Ref<>::steal(PyLong_FromLong(300));
  PyObject* args[] = {arg1, arg2, arg3};
  auto result = Ref<>::steal(PyLong_FromLong(600));
  runTest(src, args, 3, result);
}

TEST_F(DeoptStressTest, BasicForLoop) {
  const char* src = R"(
def test(n):
  res = 1
  for i in range(1, n + 1):
    res *= i
  return res
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(120));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, NestedForLoops) {
  const char* src = R"(
def test():
  vals = [10, 20, 30]
  ret = 0
  for x in vals:
    for y in vals:
      for z in vals:
        ret += x + y + z
  return ret
)";
  auto result = Ref<>::steal(PyLong_FromLong(1620));
  runTest(src, nullptr, 0, result);
}

TEST_F(DeoptStressTest, NestedWhileLoops) {
  const char* src = R"(
def test():
  vals = [10, 20, 30]
  ret = 0
  x = 0
  while x < len(vals):
    y = 0
    while y < len(vals):
      z = 0
      while z < len(vals):
        ret += vals[x] + vals[y] + vals[z]
        z += 1
      y += 1
    x += 1
  return ret
)";
  auto result = Ref<>::steal(PyLong_FromLong(1620));
  runTest(src, nullptr, 0, result);
}

TEST_F(DeoptStressTest, CallInstanceMethod) {
  const char* src = R"(
class Accum:
  def __init__(self):
    self.val = 1

  def mul(self, x):
    self.val *= x

def test(n):
  acc = Accum()
  for x in range(1, n + 1):
    acc.mul(x)
  return acc.val
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(120));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, CallMethodDescr) {
  const char* src = R"(
def test(n):
  nums = []
  for x in range(n + 1):
    nums.append(x)
  return sum(nums)
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(15));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, NestedCallMethods) {
  const char* src = R"(
class Counter:
  def __init__(self):
    self.val = 0

  def get(self):
    val = self.val
    self.val += 1
    return val

def test(n):
  c = Counter()
  nums = []
  for x in range(n + 1):
    nums.append(c.get())
  return sum(nums)
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(15));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, CallClassMethod) {
  const char* src = R"(
class BinOps:
  @classmethod
  def mul(cls, x, y):
    return x * y

def test(n):
  acc = 1
  for x in range(1, n + 1):
    acc = BinOps.mul(acc, x)
  return acc
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(120));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, CallDescriptor) {
  const char* src = R"(
class Multiplier:
  def __call__(self, *args, **kwargs):
    acc = 1
    for arg in args:
      acc *= arg
    return acc

class Descr:
  def __get__(self, obj, typ):
    return Multiplier()

class Methods:
  mul = Descr()

def test(n):
  acc = 1
  m = Methods()
  for x in range(1, n + 1):
    acc = m.mul(acc, x)
  return acc
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(120));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, CallDescriptor2) {
  const char* src = R"(
class C:
  def _get_func(self):
    def f(*args):
      return args[0] + args[1]
    return f

  a_method = property(_get_func)

def test(x, y):
  c = C()
  return c.a_method(x, y)
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  auto arg2 = Ref<>::steal(PyLong_FromLong(200));
  PyObject* args[] = {arg1, arg2};
  auto result = Ref<>::steal(PyLong_FromLong(300));
  runTest(src, args, 2, result);
}

TEST_F(DeoptStressTest, Closures) {
  const char* src = R"(
def test(n):
  x = n
  def inc():
    x += 1
  x += 10
  return x
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(15));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, StoreSubscr) {
  const char* src = R"(
def test(x, y):
  d = {'x': 1, 'y': 2}
  d['x'] = x
  d['y'] = y
  return d['x'] + d['y']
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  auto arg2 = Ref<>::steal(PyLong_FromLong(200));
  PyObject* args[] = {arg1, arg2};
  auto result = Ref<>::steal(PyLong_FromLong(300));
  runTest(src, args, 2, result);
}

TEST_F(DeoptStressTest, LoadStoreAttr) {
  const char* src = R"(
class Container:
  pass

def test(x, y, z):
  c = Container()
  c.x = x
  c.y = y
  c.z = z
  return c.x + c.y + c.z
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  auto arg2 = Ref<>::steal(PyLong_FromLong(200));
  auto arg3 = Ref<>::steal(PyLong_FromLong(300));
  PyObject* args[] = {arg1, arg2, arg3};
  auto result = Ref<>::steal(PyLong_FromLong(600));
  runTest(src, args, 3, result);
}

TEST_F(DeoptStressTest, BuildSlice) {
  const char* src = R"(
def test(n):
  vals = list(range(n))
  res = 0
  x = int(n / 2)
  for x in vals[0:x]:
    res += x
  return res
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(10));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(10));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, Conditionals) {
  const char* src = R"(
def test(n):
  res = 0
  res += n
  if n > 0:
    res += n
  return res
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(10));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(20));
  runTest(src, args, 1, result);
}

TEST_F(DeoptStressTest, Inliner) {
  const char* src = R"(
def bar(n):
  return n + 1

def test(n):
  res = 0
  res += bar(n)
  return res
)";
  auto arg1 = Ref<>::steal(PyLong_FromLong(10));
  PyObject* args[] = {arg1};
  auto result = Ref<>::steal(PyLong_FromLong(11));
  _PyJIT_EnableHIRInliner();
  runTest(src, args, 1, result);
}

using DeoptTest = RuntimeTest;

TEST_F(DeoptTest, ValueKind) {
  EXPECT_EQ(deoptValueKind(TCBool), ValueKind::kBool);

  EXPECT_EQ(deoptValueKind(TCInt8), ValueKind::kSigned);
  EXPECT_EQ(deoptValueKind(TCInt8 | TNullptr), ValueKind::kSigned);

  EXPECT_EQ(deoptValueKind(TCUInt32), ValueKind::kUnsigned);
  EXPECT_EQ(deoptValueKind(TCUInt32 | TNullptr), ValueKind::kUnsigned);

  EXPECT_EQ(deoptValueKind(TLong), ValueKind::kObject);
  EXPECT_EQ(deoptValueKind(TNullptr), ValueKind::kObject);
}
