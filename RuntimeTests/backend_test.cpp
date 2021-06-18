// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/codegen/environ.h"
#include "Jit/codegen/gen_asm.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/lir/instruction.h"
#include "gtest/gtest.h"

#include "Jit/codegen/autogen.h"
#include "Jit/codegen/postalloc.h"
#include "Jit/codegen/postgen.h"
#include "Jit/codegen/regalloc.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/parser.h"
#include "Jit/ref.h"
#include "fixtures.h"
#include "testutil.h"

#include <fstream>
#include "Python.h"

using namespace jit;
using namespace jit::lir;

namespace jit::codegen {
class BackendTest : public RuntimeTest {
 public:
  // compile a function without generating prologue and epilogue.
  // the function is self-contained.
  // this function is used to test LIR, rewrite passes, register allocation,
  // and machine code generation.
  void* SimpleCompile(Function* lir_func, int arg_buffer_size = 0) {
    Environ environ;
    PostGenerationRewrite post_gen(lir_func, &environ);
    post_gen.run();

    LinearScanAllocator lsalloc(lir_func);
    lsalloc.run();

    environ.spill_size = lsalloc.getSpillSize();
    environ.changed_regs = lsalloc.getChangedRegs();

    PostRegAllocRewrite post_rewrite(lir_func, &environ);
    post_rewrite.run();

    asmjit::CodeHolder code;
    code.init(rt_.codeInfo());

    asmjit::x86::Builder as(&code);

    environ.as = &as;

    as.push(asmjit::x86::rbp);
    as.mov(asmjit::x86::rbp, asmjit::x86::rsp);

    auto saved_regs = environ.changed_regs & CALLEE_SAVE_REGS;
    int saved_regs_size = saved_regs.count() * 8;

    int allocate_stack = std::max(environ.spill_size, 8);
    if ((allocate_stack + saved_regs_size + arg_buffer_size) % 16 != 0) {
      allocate_stack += 8;
    }

    // Allocate stack space and save the size of the function's stack.
    as.sub(asmjit::x86::rsp, allocate_stack);

    // Push used callee-saved registers.
    std::vector<int> pushed_regs;
    pushed_regs.reserve(saved_regs.count());
    while (!saved_regs.Empty()) {
      as.push(asmjit::x86::gpq(saved_regs.GetFirst()));
      pushed_regs.push_back(saved_regs.GetFirst());
      saved_regs.RemoveFirst();
    }

    if (arg_buffer_size > 0) {
      as.sub(asmjit::x86::rsp, arg_buffer_size);
    }

    NativeGenerator gen(nullptr, &rt_);
    gen.env_ = std::move(environ);
    gen.lir_func_.reset(lir_func);
    gen.generateAssemblyBody();

    if (arg_buffer_size > 0) {
      as.add(asmjit::x86::rsp, arg_buffer_size);
    }

    for (auto riter = pushed_regs.rbegin(); riter != pushed_regs.rend();
         ++riter) {
      as.pop(asmjit::x86::gpq(*riter));
    }

    as.leave();
    as.ret();

    as.finalize();
    void* func = nullptr;
    rt_.add(&func, &code);
    gen.lir_func_.release();
    return func;
  }

 private:
  asmjit::JitRuntime rt_;
};

// This is a test harness for experimenting with backends
TEST_F(BackendTest, SimpleLoadAttr) {
  const char* src = R"(
class User:
  def __init__(self, user_id):
    self._user_id = user_id

def get_user_id(user):
    return user._user_id
)";
  Ref<PyObject> globals(MakeGlobals());
  ASSERT_NE(globals.get(), nullptr) << "Failed creating globals";

  auto locals = Ref<>::steal(PyDict_New());
  ASSERT_NE(locals.get(), nullptr) << "Failed creating locals";

  auto st = Ref<>::steal(PyRun_String(src, Py_file_input, globals, locals));
  ASSERT_NE(st.get(), nullptr) << "Failed executing code";

  // Borrowed from locals
  PyObject* get_user_id = PyDict_GetItemString(locals, "get_user_id");
  ASSERT_NE(get_user_id, nullptr) << "Couldn't get get_user_id function";

  // Borrowed from get_user_id
  // code holds the code object for the function
  // code->co_consts holds the constants referenced by LoadConst
  // code->co_names holds the names referenced by LoadAttr
  PyObject* code = PyFunction_GetCode(get_user_id);
  ASSERT_NE(code, nullptr) << "Couldn't get code for user_id";

  // At this point you could patch user_id->vectorcall with a pointer to
  // your generated code for get_user_id.
  //
  // The HIR should be:
  //
  // fun get_user_id {
  //   bb 0 {
  //     CheckVar a0
  //     t0 = LoadAttr a0 0
  //     CheckExc t0
  //     Incref t0
  //     Return t0
  //   }
  // }

  // Create a user object we can use to call our function
  PyObject* user_klass = PyDict_GetItemString(locals, "User");
  ASSERT_NE(user_klass, nullptr) << "Couldn't get class User";

  auto user_id = Ref<>::steal(PyLong_FromLong(12345));
  ASSERT_NE(user_id.get(), nullptr) << "Couldn't create user id";

  auto user = Ref<>::steal(
      PyObject_CallFunctionObjArgs(user_klass, user_id.get(), NULL));
  ASSERT_NE(user.get(), nullptr) << "Couldn't create user";

  // Finally, call get_user_id
  auto result =
      Ref<>::steal(PyObject_CallFunctionObjArgs(get_user_id, user.get(), NULL));
  ASSERT_NE(result.get(), nullptr) << "Failed getting user id";
  ASSERT_TRUE(PyLong_CheckExact(result)) << "Incorrect type returned";
  ASSERT_EQ(PyLong_AsLong(result), PyLong_AsLong(user_id))
      << "Incorrect user id returned";
}

// floating-point arithmetic test
TEST_F(BackendTest, FPArithmetic) {
  double a = 3.12;
  double b = 1.1616;

  auto test = [&](Instruction::Opcode opcode) -> double {
    auto lirfunc = std::make_unique<Function>();
    auto bb = lirfunc->allocateBasicBlock();

    auto pa = bb->allocateInstr(
        Instruction::kMove,
        nullptr,
        OutVReg(),
        Imm(reinterpret_cast<uint64_t>(&a)));
    auto fa = bb->allocateInstr(
        Instruction::kMove, nullptr, OutVReg(OperandBase::kDouble), Ind(pa));

    auto pb = bb->allocateInstr(
        Instruction::kMove,
        nullptr,
        OutVReg(),
        Imm(reinterpret_cast<uint64_t>(&b)));
    auto fb = bb->allocateInstr(
        Instruction::kMove, nullptr, OutVReg(OperandBase::kDouble), Ind(pb));

    auto sum = bb->allocateInstr(
        opcode, nullptr, OutVReg(OperandBase::kDouble), VReg(fa), VReg(fb));
    bb->allocateInstr(Instruction::kReturn, nullptr, VReg(sum));

    // need this because the register allocator assumes the basic blocks
    // end with Return should have one and only one successor.
    auto epilogue = lirfunc->allocateBasicBlock();
    bb->addSuccessor(epilogue);

    auto func = (double (*)())SimpleCompile(lirfunc.get());

    return func();
  };

  ASSERT_DOUBLE_EQ(test(Instruction::kFadd), a + b);
  ASSERT_DOUBLE_EQ(test(Instruction::kFsub), a - b);
  ASSERT_DOUBLE_EQ(test(Instruction::kFmul), a * b);
  ASSERT_DOUBLE_EQ(test(Instruction::kFdiv), a / b);
}

TEST_F(BackendTest, FPCompare) {
  double a = 3.12;
  double b = 1.1616;

  auto test = [&](Instruction::Opcode opcode) -> double {
    auto lirfunc = std::make_unique<Function>();
    auto bb = lirfunc->allocateBasicBlock();

    auto pa = bb->allocateInstr(
        Instruction::kMove,
        nullptr,
        OutVReg(),
        Imm(reinterpret_cast<uint64_t>(&a)));
    auto fa = bb->allocateInstr(
        Instruction::kMove, nullptr, OutVReg(OperandBase::kDouble), Ind(pa));

    auto pb = bb->allocateInstr(
        Instruction::kMove,
        nullptr,
        OutVReg(),
        Imm(reinterpret_cast<uint64_t>(&b)));
    auto fb = bb->allocateInstr(
        Instruction::kMove, nullptr, OutVReg(OperandBase::kDouble), Ind(pb));

    auto compare =
        bb->allocateInstr(opcode, nullptr, OutVReg(), VReg(fa), VReg(fb));
    bb->allocateInstr(Instruction::kReturn, nullptr, VReg(compare));

    // need this because the register allocator assumes the basic blocks
    // end with Return should have one and only one successor.
    auto epilogue = lirfunc->allocateBasicBlock();
    bb->addSuccessor(epilogue);

    auto func = (bool (*)())SimpleCompile(lirfunc.get());

    return func();
  };

  ASSERT_DOUBLE_EQ(test(Instruction::kEqual), a == b);
  ASSERT_DOUBLE_EQ(test(Instruction::kNotEqual), a != b);
  ASSERT_DOUBLE_EQ(test(Instruction::kGreaterThanUnsigned), a > b);
  ASSERT_DOUBLE_EQ(test(Instruction::kLessThanUnsigned), a < b);
  ASSERT_DOUBLE_EQ(test(Instruction::kGreaterThanEqualUnsigned), a >= b);
  ASSERT_DOUBLE_EQ(test(Instruction::kLessThanEqualUnsigned), a <= b);
}

namespace {
double rt_func(
    int a,
    int b,
    int c,
    int d,
    int e,
    double fa,
    double fb,
    double fc,
    double fd,
    double fe,
    double ff,
    double fg,
    double fh,
    double fi,
    int f,
    int g,
    int h,
    double fj) {
  return fj + a + b + c + d + e + fa * fb * fc * fd * fe * ff * fg * fh * fi +
      f + g + h;
}

template <typename... Arg>
struct AllocateOperand;

template <typename Arg, typename... Args>
struct AllocateOperand<Arg, Args...> {
  Instruction* instr;
  explicit AllocateOperand(Instruction* i) : instr(i) {}

  void operator()(Arg arg, Args... args) {
    if constexpr (std::is_same_v<int, Arg>) {
      instr->allocateImmediateInput(arg);
    } else {
      instr->allocateFPImmediateInput(arg);
    }

    (AllocateOperand<Args...>(instr))(args...);
  }
};

template <>
struct AllocateOperand<> {
  Instruction* instr;
  explicit AllocateOperand(Instruction* i) : instr(i) {}

  void operator()() {}
};

template <typename... Ts>
auto getAllocateOperand(Instruction* instr, std::tuple<Ts...>) {
  return AllocateOperand<Ts...>(instr);
}
} // namespace

TEST_F(BackendTest, ManyArguments) {
  auto args = std::make_tuple(
      1,
      2,
      3,
      4,
      5,
      1.1,
      2.2,
      3.3,
      4.4,
      5.5,
      6.6,
      7.7,
      8.8,
      9.9,
      6,
      7,
      8,
      10.1);

  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  Instruction* call = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(),
      Imm(reinterpret_cast<uint64_t>(rt_func)));

  std::apply(getAllocateOperand(call, args), args);

  bb->allocateInstr(Instruction::kReturn, nullptr, VReg(call));

  // need this because the register allocator assumes the basic blocks
  // end with Return should have one and only one successor.
  auto epilogue = lirfunc->allocateBasicBlock();
  bb->addSuccessor(epilogue);

  constexpr int kArgBufferSize = 32; // 4 arguments need to pass by stack
  auto func = (double (*)())SimpleCompile(lirfunc.get(), kArgBufferSize);

  double expected = std::apply(rt_func, args);
  double result = func();

  ASSERT_DOUBLE_EQ(result, expected);
}

namespace {
static double add(double a, double b) {
  return a + b;
}
} // namespace

TEST_F(BackendTest, FPMultipleCalls) {
  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  double a = 1.1;
  double b = 2.2;
  double c = 3.3;
  double d = 4.4;

  auto loadFP = [&](double* n) {
    auto m1 = bb->allocateInstr(
        Instruction::kMove,
        nullptr,
        OutVReg(),
        Imm(reinterpret_cast<uint64_t>(n)));
    auto m2 = bb->allocateInstr(
        Instruction::kMove, nullptr, OutVReg(OperandBase::kDouble), Ind(m1));
    return m2;
  };

  auto la = loadFP(&a);
  auto lb = loadFP(&b);
  auto sum1 = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(OperandBase::kDouble),
      Imm(reinterpret_cast<uint64_t>(add)),
      VReg(la),
      VReg(lb));

  auto lc = loadFP(&c);
  auto ld = loadFP(&d);
  auto sum2 = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(OperandBase::kDouble),
      Imm(reinterpret_cast<uint64_t>(add)),
      VReg(lc),
      VReg(ld));

  auto sum = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(OperandBase::kDouble),
      Imm(reinterpret_cast<uint64_t>(add)),
      VReg(sum1),
      VReg(sum2));

  bb->allocateInstr(Instruction::kReturn, nullptr, VReg(sum));

  auto epilogue = lirfunc->allocateBasicBlock();
  bb->addSuccessor(epilogue);

  auto func = (double (*)())SimpleCompile(lirfunc.get());
  double result = func();

  ASSERT_DOUBLE_EQ(result, a + b + c + d);
}

TEST_F(BackendTest, MoveSequenceOptTest) {
  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  bb->allocateInstr(
      Instruction::kMove, nullptr, OutStk(-16), PhyReg(PhyLocation::RAX));
  bb->allocateInstr(
      Instruction::kMove, nullptr, OutStk(-24), PhyReg(PhyLocation::RSI));
  bb->allocateInstr(
      lir::Instruction::kMove, nullptr, OutStk(-32), PhyReg(PhyLocation::RCX));

  auto call = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      Imm(0),
      lir::Stk(-16),
      lir::Stk(-24),
      lir::Stk(-32));
  call->getInput(3)->setLastUse();

  Environ env;
  PostRegAllocRewrite post_rewrite(lirfunc.get(), &env);
  post_rewrite.run();

  /*
  BB %0
  [RBP - 16]:Object = Move RAX:Object
  [RBP - 24]:Object = Move RSI:Object
        RDI:Object = Move RAX:Object
        RDX:Object = Move RCX:Object
                     Xor RAX:Object, RAX:Object
                     Call RAX:Object
  */
  ASSERT_EQ(bb->getNumInstrs(), 6);
  auto& instrs = bb->instructions();

  auto iter = instrs.begin();

  ASSERT_EQ((*(iter++))->opcode(), Instruction::kMove);
  ASSERT_EQ((*(iter++))->opcode(), Instruction::kMove);
  ASSERT_EQ((*(iter++))->opcode(), Instruction::kMove);
  ASSERT_EQ((*(iter++))->opcode(), Instruction::kMove);
  ASSERT_EQ((*(iter++))->opcode(), Instruction::kXor);
  ASSERT_EQ((*(iter++))->opcode(), Instruction::kCall);
}

TEST_F(BackendTest, MoveSequenceOpt2Test) {
  // OptimizeMoveSequence should not set reg operands that are also output
  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  bb->allocateInstr(
      Instruction::kMove, nullptr, OutStk(-16), PhyReg(PhyLocation::RAX));

  bb->allocateInstr(
      Instruction::kAdd,
      nullptr,
      OutPhyReg(PhyLocation::RAX),
      PhyReg(PhyLocation::RSI),
      lir::Stk(-16));

  Environ env;
  PostRegAllocRewrite post_rewrite(lirfunc.get(), &env);
  post_rewrite.run();

  /*
  BB %0
  [RBP - 16]:Object = Move RAX:Object
        RAX:Object = Add RSI:Object, [RBP - 16]:Object
  */
  ASSERT_EQ(bb->getNumInstrs(), 2);
  auto& instrs = bb->instructions();

  auto iter = instrs.begin();

  ASSERT_EQ((*(iter++))->opcode(), Instruction::kMove);
  ASSERT_EQ((*iter)->opcode(), Instruction::kAdd);
  ASSERT_EQ((*iter)->getInput(1)->type(), OperandBase::kStack);
}

TEST_F(BackendTest, GetI32FromArrayTest) {
  auto lirfunc = std::make_unique<Function>();
  auto bb = lirfunc->allocateBasicBlock();

  auto start = bb->allocateInstr(
      Instruction::kBind, nullptr, OutVReg(), PhyReg(PhyLocation::RDI));
  auto index = bb->allocateInstr(
      Instruction::kBind,
      nullptr,
      OutVReg(OperandBase::k64bit),
      PhyReg(PhyLocation::RSI));

  auto address = Ind(start, index, 3, 0);

  auto ret = bb->allocateInstr(
      Instruction::kMove, nullptr, OutVReg(OperandBase::k64bit), address);
  bb->allocateInstr(Instruction::kReturn, nullptr, VReg(ret));

  // need this because the register allocator assumes the basic blocks
  // end with Return should have one and only one successor.
  auto epilogue = lirfunc->allocateBasicBlock();
  bb->addSuccessor(epilogue);

  auto func = (uint64_t(*)(char*, int64_t))SimpleCompile(lirfunc.get());

  long a[6] = {-1, 0, 1, 128, -2147483646, 214748367};
  ASSERT_EQ(func((char*)a, 0), JITRT_GetI32_FromArray((char*)a, 0));
  ASSERT_EQ(func((char*)a, 1), JITRT_GetI32_FromArray((char*)a, 1));
  ASSERT_EQ(func((char*)a, 2), JITRT_GetI32_FromArray((char*)a, 2));
  ASSERT_EQ(func((char*)a, 3), JITRT_GetI32_FromArray((char*)a, 3));
  ASSERT_EQ(func((char*)a, 4), JITRT_GetI32_FromArray((char*)a, 4));
  ASSERT_EQ(func((char*)a, 5), JITRT_GetI32_FromArray((char*)a, 5));
}

TEST_F(BackendTest, CastTest) {
  // constants used to print out error
  static const char* errmsg = "expected '%s', got '%s'";

  auto lirfunc = std::make_unique<Function>();
  auto bb1 = lirfunc->allocateBasicBlock();
  auto bb2 = lirfunc->allocateBasicBlock();
  auto bb3 = lirfunc->allocateBasicBlock();
  auto bb4 = lirfunc->allocateBasicBlock();
  auto epilogue = lirfunc->allocateBasicBlock();

  // BB 1 : Py_TYPE(ob) == (tp)
  auto a = bb1->allocateInstr(
      Instruction::kBind, nullptr, OutVReg(), PhyReg(PhyLocation::RDI));

  auto b = bb1->allocateInstr(
      Instruction::kBind, nullptr, OutVReg(), PhyReg(PhyLocation::RSI));

  auto a_tp = bb1->allocateInstr(
      Instruction::kMove,
      nullptr,
      OutVReg(),
      Ind(a, GET_STRUCT_MEMBER_OFFSET(PyObject, ob_type)));
  auto eq1 = bb1->allocateInstr(
      Instruction::kEqual, nullptr, OutVReg(), VReg(a_tp), VReg(b));
  bb1->allocateInstr(Instruction::kCondBranch, nullptr, VReg(eq1));
  bb1->addSuccessor(bb3); // true
  bb1->addSuccessor(bb2); // false

  // BB2 : PyType_IsSubtype(Py_TYPE(ob), (tp))
  auto subtype = bb2->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(),
      Imm(reinterpret_cast<uint64_t>(PyType_IsSubtype)),
      VReg(a_tp),
      VReg(b));
  bb2->allocateInstr(Instruction::kCondBranch, nullptr, VReg(subtype));
  bb2->addSuccessor(bb3); // true
  bb2->addSuccessor(bb4); // false

  // BB3 : return object
  bb3->allocateInstr(Instruction::kReturn, nullptr, VReg(a));
  bb3->addSuccessor(epilogue);

  // BB4 : return null
  auto a_name = bb4->allocateInstr(
      Instruction::kMove,
      nullptr,
      OutVReg(),
      Ind(a_tp, GET_STRUCT_MEMBER_OFFSET(PyTypeObject, tp_name)));
  auto b_name = bb4->allocateInstr(
      Instruction::kMove,
      nullptr,
      OutVReg(),
      Ind(b, GET_STRUCT_MEMBER_OFFSET(PyTypeObject, tp_name)));
  bb4->allocateInstr(
      Instruction::kCall,
      nullptr,
      Imm(reinterpret_cast<uint64_t>(PyErr_Format)),
      Imm(reinterpret_cast<uint64_t>(PyExc_TypeError)),
      Imm(reinterpret_cast<uint64_t>(errmsg)),
      VReg(b_name),
      VReg(a_name));
  auto nll = bb4->allocateInstr(Instruction::kMove, nullptr, OutVReg(), Imm(0));
  bb4->allocateInstr(Instruction::kReturn, nullptr, VReg(nll));
  bb4->addSuccessor(epilogue);

  auto func =
      (PyObject * (*)(PyObject*, PyTypeObject*)) SimpleCompile(lirfunc.get());

  auto test_noerror = [&](PyObject* a_in, PyTypeObject* b_in) -> void {
    auto ret_test = func(a_in, b_in);
    ASSERT_TRUE(PyErr_Occurred() == NULL);
    auto ret_jitrt = JITRT_Cast(a_in, b_in);
    ASSERT_TRUE(PyErr_Occurred() == NULL);
    ASSERT_EQ(ret_test, ret_jitrt);
  };

  auto test_error = [&](PyObject* a_in, PyTypeObject* b_in) -> void {
    auto ret_test = func(a_in, b_in);
    ASSERT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();

    auto ret_jitrt = JITRT_Cast(a_in, b_in);
    ASSERT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();

    ASSERT_EQ(ret_test, ret_jitrt);
  };

  test_noerror(Py_False, &PyBool_Type);
  test_noerror(Py_False, &PyLong_Type);
  test_error(Py_False, &PyUnicode_Type);
}

TEST_F(BackendTest, ParserGetI32FromArrayTest) {
  std::ifstream t("Jit/lir/c_helper_translations/JITRT_GetI32_FromArray.lir");
  std::stringstream buffer;
  buffer << t.rdbuf();
  Parser parser;
  auto parsed_func = parser.parse(buffer.str());

  auto func = (uint64_t(*)(char*, int64_t))SimpleCompile(parsed_func.get());

  long a[6] = {-1, 0, 1, 128, -2147483646, 214748367};
  ASSERT_EQ(func((char*)a, 0), JITRT_GetI32_FromArray((char*)a, 0));
  ASSERT_EQ(func((char*)a, 1), JITRT_GetI32_FromArray((char*)a, 1));
  ASSERT_EQ(func((char*)a, 2), JITRT_GetI32_FromArray((char*)a, 2));
  ASSERT_EQ(func((char*)a, 3), JITRT_GetI32_FromArray((char*)a, 3));
  ASSERT_EQ(func((char*)a, 4), JITRT_GetI32_FromArray((char*)a, 4));
  ASSERT_EQ(func((char*)a, 5), JITRT_GetI32_FromArray((char*)a, 5));
}

TEST_F(BackendTest, ParserCastTest) {
  std::ifstream t("Jit/lir/c_helper_translations/JITRT_Cast.lir");
  std::stringstream buffer;
  buffer << t.rdbuf();

  Parser parser;
  auto parsed_func = parser.parse(buffer.str());
  auto func = (PyObject * (*)(PyObject*, PyTypeObject*))
      SimpleCompile(parsed_func.get());

  auto test_noerror = [&](PyObject* a_in, PyTypeObject* b_in) -> void {
    auto ret_test = func(a_in, b_in);
    ASSERT_TRUE(PyErr_Occurred() == NULL);
    auto ret_jitrt = JITRT_Cast(a_in, b_in);
    ASSERT_TRUE(PyErr_Occurred() == NULL);
    ASSERT_EQ(ret_test, ret_jitrt);
  };

  auto test_error = [&](PyObject* a_in, PyTypeObject* b_in) -> void {
    auto ret_test = func(a_in, b_in);
    ASSERT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();

    auto ret_jitrt = JITRT_Cast(a_in, b_in);
    ASSERT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();

    ASSERT_EQ(ret_test, ret_jitrt);
  };

  test_noerror(Py_False, &PyBool_Type);
  test_noerror(Py_False, &PyLong_Type);
  test_error(Py_False, &PyUnicode_Type);
}

TEST_F(BackendTest, ParserStringInputTest) {
  auto lir_str = fmt::format(R"(Function:
BB %0 - succs: %4
        %1:Object = Move "hello"
        Return %1:Object

BB %4 - preds: %0

)");
  Parser parser;
  auto parsed_func = parser.parse(lir_str);
  auto func = (char* (*)())SimpleCompile(parsed_func.get());
  std::string ret = func();
  ASSERT_EQ(ret, "hello");
}
} // namespace jit::codegen
