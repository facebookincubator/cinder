// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"

#include "Jit/codegen/autogen.h"
#include "Jit/codegen/environ.h"
#include "Jit/codegen/gen_asm.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/inliner.h"
#include "Jit/lir/instruction.h"
#include "Jit/lir/lir.h"
#include "Jit/lir/parser.h"
#include "Jit/lir/postalloc.h"
#include "Jit/lir/postgen.h"
#include "Jit/lir/regalloc.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <fstream>
#include <regex>

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
    InitEnviron(environ);
    PostGenerationRewrite post_gen(lir_func, &environ);
    post_gen.run();

    LinearScanAllocator lsalloc(lir_func);
    lsalloc.run();

    environ.spill_size = lsalloc.getSpillSize();
    environ.changed_regs = lsalloc.getChangedRegs();

    PostRegAllocRewrite post_rewrite(lir_func, &environ);
    post_rewrite.run();

    asmjit::CodeHolder code;
    code.init(CodeAllocator::get()->asmJitCodeInfo());

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

    NativeGenerator gen(nullptr);
    gen.env_ = std::move(environ);
    gen.lir_func_.reset(lir_func);
    gen.generateAssemblyBody(code);

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
    CodeAllocator::get()->addCode(&func, &code);
    gen.lir_func_.release();
    return func;
  }

  void InitEnviron(Environ& environ) {
    for (const auto& loc : ARGUMENT_REGS) {
      environ.arg_locations.push_back(loc);
    }
  }

  void CheckFromArray(Function* lir_func) {
    auto func = (uint64_t(*)(char*, int64_t, ssize_t))SimpleCompile(lir_func);

    int64_t a[6] = {-1, 0, 1, 128, -2147483646, 214748367};
    ASSERT_EQ(
        func((char*)a, 0, 0),
        JITRT_GetI64_FromArray((char*)a, 0, /*offset=*/0));
    ASSERT_EQ(
        func((char*)a, 1, 0),
        JITRT_GetI64_FromArray((char*)a, 1, /*offset=*/0));
    ASSERT_EQ(
        func((char*)a, 2, 0),
        JITRT_GetI64_FromArray((char*)a, 2, /*offset=*/0));
    ASSERT_EQ(
        func((char*)a, 3, 0),
        JITRT_GetI64_FromArray((char*)a, 3, /*offset=*/0));
    ASSERT_EQ(
        func((char*)a, 4, 0),
        JITRT_GetI64_FromArray((char*)a, 4, /*offset=*/0));
    ASSERT_EQ(
        func((char*)a, 5, 0),
        JITRT_GetI64_FromArray((char*)a, 5, /*offset=*/0));
    ASSERT_EQ(
        func((char*)a, 0, 16),
        JITRT_GetI64_FromArray((char*)a, 0, /*offset=*/16));
    ASSERT_EQ(
        func((char*)a, 1, 24),
        JITRT_GetI64_FromArray((char*)a, 1, /*offset=*/24));
    ASSERT_EQ(
        func((char*)a, 4, -24),
        JITRT_GetI64_FromArray((char*)a, 4, /*offset=*/-24));
    ASSERT_EQ(
        func((char*)a, 5, -16),
        JITRT_GetI64_FromArray((char*)a, 5, /*offset=*/-16));
  }

  void CheckCast(Function* lir_func) {
    auto func =
        (PyObject * (*)(PyObject*, PyTypeObject*)) SimpleCompile(lir_func);

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

  auto start =
      bb->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(0));
  auto index = bb->allocateInstr(
      Instruction::kLoadArg, nullptr, OutVReg(OperandBase::k64bit), Imm(1));
  auto offset = bb->allocateInstr(
      Instruction::kLoadArg, nullptr, OutVReg(OperandBase::k64bit), Imm(2));

  auto base_address = bb->allocateInstr(
      Instruction::kAdd,
      nullptr,
      OutVReg(OperandBase::k64bit),
      VReg(start),
      VReg(offset));

  auto address = Ind(base_address, index, 3, 0);

  auto ret = bb->allocateInstr(
      Instruction::kMove, nullptr, OutVReg(OperandBase::k64bit), address);
  bb->allocateInstr(Instruction::kReturn, nullptr, VReg(ret));

  // need this because the register allocator assumes the basic blocks
  // end with Return should have one and only one successor.
  auto epilogue = lirfunc->allocateBasicBlock();
  bb->addSuccessor(epilogue);

  CheckFromArray(lirfunc.get());
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
  auto a =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(0));
  auto b =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(1));

  auto a_tp = bb1->allocateInstr(
      Instruction::kMove,
      nullptr,
      OutVReg(),
      Ind(a, offsetof(PyObject, ob_type)));
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
      Ind(a_tp, offsetof(PyTypeObject, tp_name)));
  auto b_name = bb4->allocateInstr(
      Instruction::kMove,
      nullptr,
      OutVReg(),
      Ind(b, offsetof(PyTypeObject, tp_name)));
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

  CheckCast(lirfunc.get());
}

TEST_F(BackendTest, ParserGetI32FromArrayTest) {
  std::ifstream t("Jit/lir/c_helper_translations/JITRT_GetI64_FromArray.lir");
  std::stringstream buffer;
  buffer << t.rdbuf();
  Parser parser;
  auto parsed_func = parser.parse(buffer.str());

  CheckFromArray(parsed_func.get());
}

TEST_F(BackendTest, ParserCastTest) {
  std::ifstream t("Jit/lir/c_helper_translations/JITRT_Cast.lir");
  std::stringstream buffer;
  buffer << t.rdbuf();

  Parser parser;
  auto parsed_func = parser.parse(buffer.str());

  CheckCast(parsed_func.get());
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

TEST_F(BackendTest, ParserMultipleStringInputTest) {
  auto lir_str = fmt::format(R"(Function:
BB %0 - succs: %8
        %1:Object = Move "hello1"
        %2:Object = Move "hello2"
        %3:Object = Move "hello3"
        %4:Object = Move "hello4"
        %5:Object = Move "hello5"
        %6:Object = Move "hello6"
                    Return %1:Object

BB %8 - preds: %0

)");
  Parser parser;
  auto parsed_func = parser.parse(lir_str);
  auto func = (char* (*)())SimpleCompile(parsed_func.get());
  std::string ret = func();
  ASSERT_EQ(ret, "hello1");
}

TEST_F(BackendTest, SplitBasicBlockTest) {
  auto lirfunc = std::make_unique<Function>();
  auto bb1 = lirfunc->allocateBasicBlock();
  auto bb2 = lirfunc->allocateBasicBlock();
  auto bb3 = lirfunc->allocateBasicBlock();
  auto bb4 = lirfunc->allocateBasicBlock();
  auto epilogue = lirfunc->allocateBasicBlock();

  auto r1 =
      bb1->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(0));
  bb1->allocateInstr(Instruction::kCondBranch, nullptr, VReg(r1));
  bb1->addSuccessor(bb2);
  bb1->addSuccessor(bb3);

  auto r2 = bb2->allocateInstr(
      Instruction::kAdd, nullptr, OutVReg(), VReg(r1), Imm(8));
  bb2->addSuccessor(bb4);

  auto r3 = bb3->allocateInstr(
      Instruction::kAdd, nullptr, OutVReg(), VReg(r1), Imm(8));
  auto r4 = bb3->allocateInstr(
      Instruction::kAdd, nullptr, OutVReg(), VReg(r3), Imm(8));
  bb3->addSuccessor(bb4);

  auto r5 = bb4->allocateInstr(
      Instruction::kPhi,
      nullptr,
      OutVReg(),
      Lbl(bb2),
      VReg(r2),
      Lbl(bb3),
      VReg(r4));
  bb4->allocateInstr(Instruction::kReturn, nullptr, VReg(r5));
  bb4->addSuccessor(epilogue);

  // split blocks and then test that function output is still correct
  auto bb_new = bb1->splitBefore(r1);
  bb_new->splitBefore(r1); // test that bb_new is valid
  bb2->splitBefore(r2); // test fixupPhis
  auto bb_nullptr = bb2->splitBefore(r3); // test instruction not in block
  ASSERT_EQ(bb_nullptr, nullptr);
  bb3->splitBefore(r4); // test split in middle of block

  auto func = (uint64_t(*)(int64_t))SimpleCompile(lirfunc.get());

  ASSERT_EQ(func(0), 16);
  ASSERT_EQ(func(1), 9);
}

TEST_F(BackendTest, CopyFromArrayTest) {
  std::ifstream t("Jit/lir/c_helper_translations/JITRT_GetI64_FromArray.lir");
  std::stringstream buffer;
  buffer << t.rdbuf();
  Parser parser;
  auto parsed_func = parser.parse(buffer.str());

  auto caller = std::make_unique<Function>();
  auto bb1 = caller->allocateBasicBlock();
  auto bb2 = caller->allocateBasicBlock();
  bb1->addSuccessor(bb2);
  auto [begin_bb, end_bb] =
      caller->copyFrom(parsed_func.get(), bb1, bb2, nullptr);
  parsed_func.reset();

  // Check that the caller is what we expected.
  auto expected_caller = fmt::format(R"(Function:
BB %0 - succs: %2

BB %2 - preds: %0 - succs: %3
       %4:Object = LoadArg 0(0x0):Object
        %5:64bit = LoadArg 1(0x1):Object
        %6:64bit = LoadArg 2(0x2):Object
        %7:64bit = Add %4:Object, %6:64bit
        %8:64bit = Move [%7:64bit + %5:64bit * 8]:Object
                   Return %8:64bit

BB %3 - preds: %2 - succs: %1

BB %1 - preds: %3

)");
  std::stringstream ss;
  caller->sortBasicBlocks();
  ss << *caller;
  ASSERT_EQ(expected_caller, ss.str());

  // Remove bb1 and bb2,
  // so that the function can execute correctly.
  auto basicblocks = &caller->basicblocks();
  auto start = basicblocks->at(begin_bb);
  start->predecessors().clear();
  auto end = basicblocks->at(end_bb - 1);
  end->successors().clear();
  basicblocks->erase(
      std::remove(basicblocks->begin(), basicblocks->end(), bb1),
      basicblocks->end());
  basicblocks->erase(
      std::remove(basicblocks->begin(), basicblocks->end(), bb2),
      basicblocks->end());
  CheckFromArray(caller.get());
}

TEST_F(BackendTest, CopyCastTest) {
  std::ifstream t("Jit/lir/c_helper_translations/JITRT_Cast.lir");
  std::stringstream buffer;
  buffer << t.rdbuf();

  Parser parser;
  auto parsed_func = parser.parse(buffer.str());

  auto caller = std::make_unique<Function>();
  auto bb1 = caller->allocateBasicBlock();
  auto bb2 = caller->allocateBasicBlock();
  bb1->addSuccessor(bb2);
  auto [begin_bb, end_bb] =
      caller->copyFrom(parsed_func.get(), bb1, bb2, nullptr);
  parsed_func.reset();

  auto expected_caller = fmt::format(
      R"(Function:
BB %0 - succs: %2

BB %2 - preds: %0 - succs: %4 %3
       %7:Object = LoadArg 0(0x0):Object
       %8:Object = LoadArg 1(0x1):Object
       %9:Object = Move [%7:Object + 0x8]:Object
      %10:Object = Equal %9:Object, %8:Object
                   CondBranch %10:Object

BB %3 - preds: %2 - succs: %4 %5
      %12:Object = Call {0}({0:#x}):Object, %9:Object, %8:Object
                   CondBranch %12:Object

BB %5 - preds: %3 - succs: %6
      %15:Object = Move [%9:Object + 0x18]:Object
      %16:Object = Move [%8:Object + 0x18]:Object
                   Call {1}({1:#x}):Object, {2}({2:#x}):Object, string_literal, %16:Object, %15:Object
      %18:Object = Move 0(0x0):Object
                   Return %18:Object

BB %4 - preds: %2 %3 - succs: %6
                   Return %7:Object

BB %6 - preds: %4 %5 - succs: %1

BB %1 - preds: %6

)",
      reinterpret_cast<uint64_t>(PyType_IsSubtype),
      reinterpret_cast<uint64_t>(PyErr_Format),
      reinterpret_cast<uint64_t>(PyExc_TypeError));
  std::stringstream ss;
  caller->sortBasicBlocks();
  ss << *caller;
  // Replace the string literal address
  std::regex reg("\\d+\\(0x[0-9a-fA-F]+\\):Object, %16:Object, %15:Object");
  std::string caller_str =
      regex_replace(ss.str(), reg, "string_literal, %16:Object, %15:Object");
  ASSERT_EQ(expected_caller, caller_str);

  // Remove bb1 and bb2,
  // so that the function can execute correctly.
  auto basicblocks = &caller->basicblocks();
  auto start = basicblocks->at(begin_bb);
  start->predecessors().clear();
  auto end = basicblocks->at(end_bb - 1);
  end->successors().clear();
  basicblocks->erase(
      std::remove(basicblocks->begin(), basicblocks->end(), bb1),
      basicblocks->end());
  basicblocks->erase(
      std::remove(basicblocks->begin(), basicblocks->end(), bb2),
      basicblocks->end());
  CheckCast(caller.get());
}

TEST_F(BackendTest, InlineJITRTCastTest) {
  auto caller = std::make_unique<Function>();
  auto bb = caller->allocateBasicBlock();
  auto r1 =
      bb->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(0));
  auto r2 =
      bb->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(1));
  auto call_instr = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(),
      Imm(reinterpret_cast<uint64_t>(JITRT_Cast)),
      VReg(r1),
      VReg(r2));
  bb->allocateInstr(Instruction::kReturn, nullptr, VReg(call_instr));
  auto epilogue = caller->allocateBasicBlock();
  bb->addSuccessor(epilogue);
  LIRInliner inliner(call_instr);
  inliner.inlineCall();

  // Check that caller LIR is as expected.
  auto expected_caller = fmt::format(
      R"(Function:
BB %0 - succs: %7
       %1:Object = LoadArg 0(0x0):64bit
       %2:Object = LoadArg 1(0x1):64bit

BB %7 - preds: %0 - succs: %9 %8
      %14:Object = Move [%1:Object + 0x8]:Object
      %15:Object = Equal %14:Object, %2:Object
                   CondBranch %15:Object

BB %8 - preds: %7 - succs: %9 %10
      %17:Object = Call {0}({0:#x}):Object, %14:Object, %2:Object
                   CondBranch %17:Object

BB %10 - preds: %8 - succs: %11
      %20:Object = Move [%14:Object + 0x18]:Object
      %21:Object = Move [%2:Object + 0x18]:Object
                   Call {1}({1:#x}):Object, {2}({2:#x}):Object, string_literal, %21:Object, %20:Object
      %23:Object = Move 0(0x0):Object

BB %9 - preds: %7 %8 - succs: %11

BB %11 - preds: %9 %10 - succs: %6
      %25:Object = Phi (BB%9, %1:Object), (BB%10, %23:Object)

BB %6 - preds: %11 - succs: %5
       %3:Object = Move %25:Object
                   Return %3:Object

BB %5 - preds: %6

)",
      reinterpret_cast<uint64_t>(PyType_IsSubtype),
      reinterpret_cast<uint64_t>(PyErr_Format),
      reinterpret_cast<uint64_t>(PyExc_TypeError));
  std::stringstream ss;
  caller->sortBasicBlocks();
  ss << *caller;
  // Replace the string literal address
  std::regex reg("\\d+\\(0x[0-9a-fA-F]+\\):Object, %21:Object, %20:Object");
  std::string caller_str =
      regex_replace(ss.str(), reg, "string_literal, %21:Object, %20:Object");
  ASSERT_EQ(expected_caller, caller_str);

  // Test execution of caller
  CheckCast(caller.get());
}

TEST_F(BackendTest, PostgenJITRTCastTest) {
  auto caller = std::make_unique<Function>();
  auto bb = caller->allocateBasicBlock();
  auto r1 =
      bb->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(0));
  auto r2 =
      bb->allocateInstr(Instruction::kLoadArg, nullptr, OutVReg(), Imm(1));
  auto call_instr = bb->allocateInstr(
      Instruction::kCall,
      nullptr,
      OutVReg(),
      Imm(reinterpret_cast<uint64_t>(JITRT_Cast)),
      VReg(r1),
      VReg(r2));
  bb->allocateInstr(Instruction::kReturn, nullptr, VReg(call_instr));
  auto epilogue = caller->allocateBasicBlock();
  bb->addSuccessor(epilogue);

  Environ environ;
  InitEnviron(environ);
  PostGenerationRewrite post_gen(caller.get(), &environ);
  post_gen.run();

  // Check that caller LIR is as expected.
  auto expected_caller = fmt::format(
      R"(Function:
BB %0 - succs: %7
       %1:Object = Bind RDI:Object
       %2:Object = Bind RSI:Object

BB %7 - preds: %0 - succs: %9 %8
      %14:Object = Move [%1:Object + 0x8]:Object
      %15:Object = Equal %14:Object, %2:Object
                   CondBranch %15:Object

BB %8 - preds: %7 - succs: %9 %10
      %17:Object = Call {0}({0:#x}):Object, %14:Object, %2:Object
                   CondBranch %17:Object

BB %10 - preds: %8 - succs: %11
      %20:Object = Move [%14:Object + 0x18]:Object
      %21:Object = Move [%2:Object + 0x18]:Object
                   Call {1}({1:#x}):Object, {2}({2:#x}):Object, string_literal, %21:Object, %20:Object
      %23:Object = Move 0(0x0):Object

BB %9 - preds: %7 %8 - succs: %11

BB %11 - preds: %9 %10 - succs: %6
      %25:Object = Phi (BB%9, %1:Object), (BB%10, %23:Object)

BB %6 - preds: %11 - succs: %5
       %3:Object = Move %25:Object
                   Return %3:Object

BB %5 - preds: %6

)",
      reinterpret_cast<uint64_t>(PyType_IsSubtype),
      reinterpret_cast<uint64_t>(PyErr_Format),
      reinterpret_cast<uint64_t>(PyExc_TypeError));
  std::stringstream ss;
  caller->sortBasicBlocks();
  ss << *caller;
  // Replace the string literal address
  std::regex reg("\\d+\\(0x[0-9a-fA-F]+\\):Object, %21:Object, %20:Object");
  std::string caller_str =
      regex_replace(ss.str(), reg, "string_literal, %21:Object, %20:Object");
  ASSERT_EQ(expected_caller, caller_str);
}

TEST_F(BackendTest, ParserErrorFromExpectTest) {
  // Test throw from expect
  Parser parser;
  parser.parse(R"(Function:
BB %0
)");
  try {
    // Bad basic block header
    parser.parse(R"(Function:
BB %0 %3
)");
    FAIL();
  } catch (ParserException&) {
  }

  try {
    // Dupicate ID
    parser.parse(R"(Function:
BB %0
%1:Object = Bind RDI:Object
%1:Object
)");
    FAIL();
  } catch (ParserException&) {
  }
}

TEST_F(BackendTest, ParserErrorFromMapGetTest) {
  // Test throw from map_get_throw
  Parser parser;
  try {
    // Invalid opcode
    parser.parse(R"(Function:
BB %0
%1:Object = InvalidInstruction
)");
    FAIL();
  } catch (ParserException&) {
  }
  try {
    // Missing basic block
    parser.parse(R"(Function:
BB %0 - succs: %2
Return 0(0x0):Object
BB %1
)");
    FAIL();
  } catch (ParserException&) {
  }
}

} // namespace jit::codegen
