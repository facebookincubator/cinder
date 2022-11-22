// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/codegen/gen_asm.h"
#include "Jit/compiler.h"
#include "Jit/hir/builder.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <asmjit/asmjit.h>

#include <iostream>
#include <string>
#include <utility>

using namespace asmjit;
using namespace jit;
using namespace jit::codegen;
using namespace jit::hir;

class ASMGeneratorTest : public RuntimeTest {
 public:
  std::unique_ptr<CompiledFunction> GenerateCode(PyObject* func) {
    return Compiler().Compile(func);
  }
};

TEST_F(ASMGeneratorTest, SanityCheck) {
  const char* pycode = R"(
def func():
  a = 314159
  return a
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "func"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  PyObject* args[] = {};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 0));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyLong_AsLong(res), 314159);
}

TEST_F(ASMGeneratorTest, Fallthrough) {
  const char* src = R"(
def func2(x):
  y = 0
  if x:
    y = 100
  return y
)";

  Ref<PyObject> pyfunc(compileAndGet(src, "func2"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(16));
  ASSERT_NE(arg0, nullptr);
  PyObject* args[] = {arg0};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyObject_IsTrue(res), 1);
}

TEST_F(ASMGeneratorTest, CondBranchTest) {
  const char* pycode = R"(
def func2(x):
    if x:
        return True
    return False
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "func2"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(16));
  ASSERT_NE(arg0, nullptr);
  PyObject* args[] = {arg0};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyObject_IsTrue(res), 1);

  auto arg1 = Ref<>::steal(PyLong_FromLong(0));
  args[0] = arg1;
  auto res2 = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res2, nullptr);
  ASSERT_EQ(PyObject_IsTrue(res2), 0);
}

TEST_F(ASMGeneratorTest, UnboundLocalError) {
  const char* pycode = R"(
def test(x):
    if x:
        y = 1
    z = 100
    return y
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg = Ref<>::steal(PyLong_FromLong(0));
  ASSERT_NE(arg, nullptr);

  PyObject* args[] = {arg.get()};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_EQ(res, nullptr);

  PyObject *etyp, *eval, *etb;
  PyErr_Fetch(&etyp, &eval, &etb);
  ASSERT_NE(etyp, nullptr);

  auto typ = Ref<>::steal(etyp);
  auto val = Ref<>::steal(eval);
  auto tb = Ref<>::steal(etb);
  EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_UnboundLocalError));
  ASSERT_NE(val.get(), nullptr);
  ASSERT_TRUE(PyUnicode_Check(val));
  std::string msg = PyUnicode_AsUTF8(val);
  ASSERT_EQ(msg, "local variable 'y' referenced before assignment");

  auto tb_frame = Ref<>::steal(PyObject_GetAttrString(tb, "tb_frame"));
  ASSERT_NE(tb_frame.get(), nullptr);

  auto locals = Ref<>::steal(PyObject_GetAttrString(tb_frame, "f_locals"));
  ASSERT_NE(locals.get(), nullptr);
  EXPECT_EQ(PyObject_Length(locals), 2);
  PyObject* x = PyDict_GetItemString(locals, "x");
  ASSERT_TRUE(PyLong_CheckExact(x));
  EXPECT_EQ(PyLong_AsLong(x), 0);
  PyObject* y = PyDict_GetItemString(locals, "z");
  ASSERT_TRUE(PyLong_CheckExact(y));
  EXPECT_EQ(PyLong_AsLong(y), 100);
}

TEST_F(ASMGeneratorTest, InsertXDecrefForMaybeAssignedRegisters) {
  const char* pycode = R"(
def test(x):
    if x:
        y = 1
    z = y
    return z
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg = Ref<>::steal(PyLong_FromLong(133));
  ASSERT_NE(arg, nullptr);

  PyObject* args[] = {arg};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res, nullptr);
  EXPECT_EQ(PyLong_AsLong(res), 1);
}

TEST_F(ASMGeneratorTest, LoadAttr) {
  const char* pycode = R"(
def test(x):
    return x.denominator
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(16));
  ASSERT_NE(arg0, nullptr);
  PyObject* args[] = {arg0};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyLong_AsLong(res), 1);
}

TEST_F(ASMGeneratorTest, LoadAttrRaisesError) {
  const char* pycode = R"(
def test(x):
    y = 100
    return x.denominator
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg = Ref<>::steal(PyDict_New());
  ASSERT_NE(arg, nullptr);

  PyObject* args[] = {arg};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_EQ(res, nullptr);

  PyObject *etyp, *eval, *etb;
  PyErr_Fetch(&etyp, &eval, &etb);
  ASSERT_NE(etyp, nullptr);

  auto typ = Ref<>::steal(etyp);
  auto val = Ref<>::steal(eval);
  auto tb = Ref<>::steal(etb);
  EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_AttributeError));
  ASSERT_NE(val, nullptr);
  EXPECT_TRUE(PyObject_IsInstance(val, PyExc_AttributeError));
  auto ae = reinterpret_cast<PyAttributeErrorObject*>(val.get());
  ASSERT_TRUE(PyUnicode_Check(ae->name));
  std::string msg = PyUnicode_AsUTF8(ae->name);
  ASSERT_EQ(msg, "denominator");

  auto tb_frame = Ref<>::steal(PyObject_GetAttrString(tb, "tb_frame"));
  ASSERT_NE(tb_frame.get(), nullptr);

  auto locals = Ref<>::steal(PyObject_GetAttrString(tb_frame, "f_locals"));
  ASSERT_NE(locals.get(), nullptr);
  EXPECT_EQ(PyObject_Length(locals), 2);
  PyObject* x = PyDict_GetItemString(locals, "x");
  ASSERT_EQ(x, arg);
  PyObject* y = PyDict_GetItemString(locals, "y");
  ASSERT_TRUE(PyLong_CheckExact(y));
  EXPECT_EQ(PyLong_AsLong(y), 100);
}

TEST_F(ASMGeneratorTest, StoreAttr) {
  const char* klasscode = R"(
class TestClass:
  pass
)";
  Ref<PyObject> klass(compileAndGet(klasscode, "TestClass"));
  ASSERT_NE(klass, nullptr);

  const char* pycode = R"(
def test(x):
  x.foo = 100
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  PyObject* args[] = {klass};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res, nullptr);

  auto val = Ref<>::steal(PyObject_GetAttrString(klass, "foo"));
  ASSERT_NE(val.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(val));
  EXPECT_EQ(PyLong_AsLong(val), 100);
}

TEST_F(ASMGeneratorTest, Compare) {
  const char* pycode = R"(
def test(a, b):
    return a is b;
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(16));
  auto arg1 = Ref<>::steal(PyLong_FromLong(32));
  PyObject* args[] = {arg0, arg1};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyObject_IsTrue(res), 0);

  auto arg2 = Ref<>::steal(PyLong_FromLong(0));
  args[0] = arg2;
  args[1] = arg2;
  auto res2 = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));
  ASSERT_NE(res2, nullptr);
  ASSERT_EQ(PyObject_IsTrue(res2), 1);
}

TEST_F(ASMGeneratorTest, LoadGlobalTest) {
  const char* pycode = R"(
def test():
    return len
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  PyObject* args[] = {};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 0));
  ASSERT_NE(res, nullptr);

  PyObject* globals = PyFunction_GetGlobals(pyfunc);
  ASSERT_NE(globals, nullptr);

  PyObject* builtins = PyDict_GetItemString(globals, "__builtins__");
  ASSERT_NE(builtins, nullptr);

  if (PyModule_Check(builtins)) {
    builtins = PyModule_GetDict(builtins);
    ASSERT_NE(builtins, nullptr);
  }

  PyObject* len = PyDict_GetItemString(builtins, "len");
  ASSERT_EQ(res, len);
}

TEST_F(ASMGeneratorTest, CallCFunction) {
  const char* pycode = R"(
def test(x):
  return len(x)
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg = Ref<>::steal(PyDict_New());
  ASSERT_NE(arg.get(), nullptr);

  PyObject* args[] = {arg};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_Check(res.get()));
  EXPECT_EQ(PyLong_AsLong(res), 0);
}

TEST_F(ASMGeneratorTest, CallBoundMethod) {
  const char* pycode = R"(
def test(l):
  l.append(123)
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg = Ref<>::steal(PyList_New(0));
  ASSERT_NE(arg.get(), nullptr);

  PyObject* args[] = {arg};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_EQ(PyList_Size(arg), 1);

  PyObject* elem = PyList_GetItem(arg, 0);
  ASSERT_TRUE(PyLong_Check(elem));
  EXPECT_EQ(PyLong_AsLong(elem), 123);
}

TEST_F(ASMGeneratorTest, DefaultArgTest) {
  const char* pycode = R"(
def test(a, b, c=100):
    return a + b + c
)";

  Ref<PyFunctionObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  pyfunc->vectorcall = compiled->vectorcallEntry();
  ASSERT_NE(compiled, nullptr);

  auto one = Ref<>::steal(PyLong_FromLong(1));
  auto two = Ref<>::steal(PyLong_FromLong(2));
  auto three = Ref<>::steal(PyLong_FromLong(3));
  PyObject* args[] = {one, two, three};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_EQ(PyLong_AsLong(res.get()), 103);

  auto res2 = Ref<>::steal(compiled->Invoke(pyfunc, args, 3));
  ASSERT_NE(res2.get(), nullptr);
  ASSERT_EQ(PyLong_AsLong(res2.get()), 6);
}

TEST_F(ASMGeneratorTest, KWArgCall) {
  const char* pycode = R"(
def test(a, b):
    return a + b;
)";

  Ref<PyFunctionObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(16));
  ASSERT_NE(arg0, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(32));
  ASSERT_NE(arg1, nullptr);

  auto kwnames = Ref<>::steal(Py_BuildValue("(s)", "b"));
  ASSERT_NE(kwnames, nullptr);

  vectorcallfunc cfunc = compiled->vectorcallEntry();
  pyfunc->vectorcall = cfunc;
  auto pfunc = reinterpret_cast<PyObject*>(pyfunc.get());
  PyObject* args[] = {arg0, arg1};
  auto result = Ref<>::steal(cfunc(pfunc, args, 1, kwnames));

  ASSERT_NE(result, nullptr);
  ASSERT_EQ(PyLong_AsLong(result), 48);
}

TEST_F(ASMGeneratorTest, CallPythonFunction) {
  const char* pycode = R"(
def meaning_of_life():
  return 42

def test(f):
  return f()
)";

  Ref<PyObject> oracle(compileAndGet(pycode, "meaning_of_life"));
  ASSERT_NE(oracle.get(), nullptr) << "Failed compiling func";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  PyObject* args[] = {oracle};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_Check(res.get()));
  EXPECT_EQ(PyLong_AsLong(res), 42);
}

TEST_F(ASMGeneratorTest, CallType) {
  const char* klasscode = R"(
class TestClass:
  pass
)";
  Ref<PyObject> klass(compileAndGet(klasscode, "TestClass"));
  ASSERT_NE(klass.get(), nullptr);

  const char* pycode = R"(
def test(f):
  return f()
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  PyObject* args[] = {klass};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_EQ(PyObject_IsInstance(res, klass), 1);
}

TEST_F(ASMGeneratorTest, InvokeBinaryAdd) {
  const char* pycode = R"(
def test(a, b):
  return a + b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(200));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 300);
}

TEST_F(ASMGeneratorTest, InvokeBinaryAnd) {
  const char* pycode = R"(
def test(a, b):
  return a & b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(3));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 1);
}

TEST_F(ASMGeneratorTest, InvokeBinaryFloorDivide) {
  const char* pycode = R"(
def test(a, b):
  return a // b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyFloat_FromDouble(4.0));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyFloat_FromDouble(2.5));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyFloat_CheckExact(res.get()));
  EXPECT_EQ(PyFloat_AsDouble(res.get()), 1.0);
}

TEST_F(ASMGeneratorTest, InvokeBinaryLShift) {
  const char* pycode = R"(
def test(a, b):
  return a << b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(2));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 4);
}

TEST_F(ASMGeneratorTest, InvokeBinaryModulo) {
  const char* pycode = R"(
def test(a, b):
  return a % b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(200));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(150));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 50);
}

TEST_F(ASMGeneratorTest, InvokeBinaryMultiply) {
  const char* pycode = R"(
def test(a, b):
  return a * b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(2));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(4));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 8);
}

TEST_F(ASMGeneratorTest, InvokeBinaryOr) {
  const char* pycode = R"(
def test(a, b):
  return a | b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(2));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 3);
}

TEST_F(ASMGeneratorTest, InvokeBinarySubscr) {
  const char* pycode = R"(
def test(x):
  l = [1, 2]
  return l[x]
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(arg1.get(), nullptr);

  PyObject* args[] = {arg1};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 2);
}

TEST_F(ASMGeneratorTest, InvokeBinarySubtract) {
  const char* pycode = R"(
def test(a, b):
  return a - b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(3));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(2));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 1);
}

TEST_F(ASMGeneratorTest, InvokeBinaryTrueDivide) {
  const char* pycode = R"(
def test(a, b):
  return a / b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(3));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(2));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyFloat_CheckExact(res.get()));
  EXPECT_EQ(PyFloat_AsDouble(res.get()), 1.5);
}

TEST_F(ASMGeneratorTest, InvokeBinaryXor) {
  const char* pycode = R"(
def test(a, b):
  return a ^ b
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(3));
  ASSERT_NE(arg1.get(), nullptr);

  auto arg2 = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(arg2.get(), nullptr);

  PyObject* args[] = {arg1, arg2};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_NE(res.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(res.get()));
  EXPECT_EQ(PyLong_AsLong(res.get()), 2);
}

TEST_F(ASMGeneratorTest, ReplaceReassignedFirstArgInExceptionFrame) {
  const char* pycode = R"(
def test(x, y):
    if x:
      x = 2
    y.invalid
    # Need a use of x here, otherwise it's reclaimed at the end of the
    # if block
    y = x
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg1 = Ref<>::steal(PyLong_FromLong(100));
  ASSERT_NE(arg1, nullptr);

  auto arg2 = Ref<>::steal(PyDict_New());
  ASSERT_NE(arg2, nullptr);

  PyObject* args[] = {arg1.get(), arg2.get()};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));
  ASSERT_EQ(res, nullptr);

  PyObject *etyp, *eval, *etb;
  PyErr_Fetch(&etyp, &eval, &etb);
  ASSERT_NE(etyp, nullptr);

  auto typ = Ref<>::steal(etyp);
  auto val = Ref<>::steal(eval);
  auto tb = Ref<>::steal(etb);
  EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_AttributeError));
  EXPECT_TRUE(PyExceptionInstance_Check(val.get()));
  auto exc_args = Ref<>::steal(PyObject_GetAttrString(val.get(), "args"));
  EXPECT_TRUE(PyTuple_Check(exc_args.get()));
  EXPECT_EQ(PyObject_Length(exc_args.get()), 1);
  auto msg = Ref<>::create(PyTuple_GetItem(exc_args.get(), 0));
  ASSERT_TRUE(PyUnicode_Check(msg));
  std::string msg_str = PyUnicode_AsUTF8(msg);
  ASSERT_EQ(msg_str, "'dict' object has no attribute 'invalid'");

  auto tb_frame = Ref<>::steal(PyObject_GetAttrString(tb, "tb_frame"));
  ASSERT_NE(tb_frame.get(), nullptr);

  auto locals = Ref<>::steal(PyObject_GetAttrString(tb_frame, "f_locals"));
  ASSERT_NE(locals.get(), nullptr);
  EXPECT_EQ(PyObject_Length(locals), 2);
  PyObject* x = PyDict_GetItemString(locals, "x");
  ASSERT_TRUE(PyLong_CheckExact(x));
  EXPECT_EQ(PyLong_AsLong(x), 2);
  PyObject* y = PyDict_GetItemString(locals, "y");
  ASSERT_EQ(y, arg2.get());
}

TEST_F(ASMGeneratorTest, TupleListTest) {
  const char* pycode = R"(
def test_tuple(a):
    return (a, a, a)
def test_list(a):
    return [a, a, a]
)";

  auto three = Ref<>::steal(PyLong_FromLong(3));
  {
    Ref<PyObject> pyfunc(compileAndGet(pycode, "test_tuple"));
    ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

    auto compiled = GenerateCode(pyfunc);
    ASSERT_NE(compiled, nullptr);

    PyObject* args[] = {three.get()};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

    ASSERT_NE(res.get(), nullptr);
    ASSERT_EQ(PyTuple_GetItem(res.get(), 0), args[0]);
    ASSERT_EQ(PyTuple_GetItem(res.get(), 1), args[0]);
    ASSERT_EQ(PyTuple_GetItem(res.get(), 2), args[0]);
  }

  {
    Ref<PyObject> pyfunc(compileAndGet(pycode, "test_list"));
    ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

    auto compiled = GenerateCode(pyfunc);
    ASSERT_NE(compiled, nullptr);

    PyObject* args[] = {three.get()};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

    ASSERT_NE(res.get(), nullptr);
    ASSERT_EQ(PyList_GetItem(res.get(), 0), args[0]);
    ASSERT_EQ(PyList_GetItem(res.get(), 1), args[0]);
    ASSERT_EQ(PyList_GetItem(res.get(), 2), args[0]);
  }
}

static void
UnaryTest(ASMGeneratorTest* test, const char* pycode, int inp, int expected) {
  Ref<PyObject> pyfunc(test->compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = test->GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);
  {
    auto arg = Ref<>::steal(PyLong_FromLong(inp));
    PyObject* args[] = {
        arg,
    };
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

    ASSERT_NE(res.get(), nullptr);
    ASSERT_EQ(PyLong_AsLong(res.get()), expected);
  }
  {
    auto arg = Ref<>::steal(PyUnicode_FromString("foo"));
    PyObject* args[] = {
        arg,
    };
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

    ASSERT_EQ(res, nullptr);

    PyObject *etyp, *eval, *etb;
    PyErr_Fetch(&etyp, &eval, &etb);
    ASSERT_NE(etyp, nullptr);

    auto typ = Ref<>::steal(etyp);
    auto val = Ref<>::steal(eval);
    auto tb = Ref<>::steal(etb);
    EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_TypeError));
  }
}

TEST_F(ASMGeneratorTest, InvokeUnaryNot) {
  const char* pycode = R"(
def test(a):
    return not a
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  {
    auto one = Ref<>::steal(PyLong_FromLong(1));
    PyObject* args[] = {one.get()};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

    ASSERT_EQ(res.get(), Py_False);
  }

  {
    auto zero = Ref<>::steal(PyLong_FromLong(0));
    PyObject* args[] = {zero.get()};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));

    ASSERT_EQ(res.get(), Py_True);
  }
}

TEST_F(ASMGeneratorTest, InvokeUnaryNegative) {
  const char* pycode = R"(
def test(a):
    return -a
)";

  UnaryTest(this, pycode, 2, -2);
}

TEST_F(ASMGeneratorTest, InvokeUnaryPositive) {
  const char* pycode = R"(
def test(a):
    return +a
)";

  UnaryTest(this, pycode, 2, 2);
}

TEST_F(ASMGeneratorTest, InvokeUnaryInvert) {
  const char* pycode = R"(
def test(a):
    return ~a
)";

  UnaryTest(this, pycode, 1, -2);
}

TEST_F(ASMGeneratorTest, StoreSubscr) {
  const char* pycode = R"(
def test(c, s, v):
  c[s] = v
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto dict = Ref<>::steal(PyDict_New());
  auto one = Ref<>::steal(PyLong_FromLong(1));
  auto two = Ref<>::steal(PyLong_FromLong(2));

  // Success case
  {
    PyObject* args[] = {dict, one, two};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 3));

    ASSERT_EQ(res.get(), Py_None);
    ASSERT_EQ(PyDict_Size(dict), 1);
    ASSERT_EQ(PyDict_GetItem(dict, one), two);
  }
  // Error case
  {
    PyObject* args[] = {one, dict, two};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 3));
    ASSERT_EQ(res.get(), nullptr);

    PyObject *etyp, *eval, *etb;
    PyErr_Fetch(&etyp, &eval, &etb);
    ASSERT_NE(etyp, nullptr);

    auto typ = Ref<>::steal(etyp);
    auto val = Ref<>::steal(eval);
    auto tb = Ref<>::steal(etb);
    EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_TypeError));
    PyErr_Clear();
  }
}

static void InPlaceOpTest(
    ASMGeneratorTest* test,
    const char* pycode,
    int a,
    int b,
    int expected) {
  Ref<PyObject> pyfunc(test->compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = test->GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);
  {
    auto aval = Ref<>::steal(PyLong_FromLong(a));
    auto bval = Ref<>::steal(PyLong_FromLong(b));
    PyObject* args[] = {aval, bval};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

    ASSERT_NE(res.get(), nullptr);
    ASSERT_EQ(PyLong_AsLong(res.get()), expected);
  }
  {
    PyObject* args[] = {Py_None, Py_None};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

    ASSERT_EQ(res, nullptr);

    PyObject *etyp, *eval, *etb;
    PyErr_Fetch(&etyp, &eval, &etb);
    ASSERT_NE(etyp, nullptr);

    auto typ = Ref<>::steal(etyp);
    auto val = Ref<>::steal(eval);
    auto tb = Ref<>::steal(etb);
    EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_TypeError));
  }
}

TEST_F(ASMGeneratorTest, InvokeInPlaceAdd) {
  const char* pycode = R"(
def test(a, b):
    a += b
    return a
)";

  InPlaceOpTest(this, pycode, 1, 2, 3);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceAnd) {
  const char* pycode = R"(
def test(a, b):
    a &= b
    return a
)";

  InPlaceOpTest(this, pycode, 1, 2, 0);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceFloorDivide) {
  const char* pycode = R"(
def test(a, b):
    a //= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 5);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceLShift) {
  const char* pycode = R"(
def test(a, b):
    a <<= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 44);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceRemainder) {
  const char* pycode = R"(
def test(a, b):
    a %= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 1);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceMatrixMultiply) {
  const char* pycode = R"(
def test(a, b):
    a @= b
    return a
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);

  PyObject* args[] = {Py_None, Py_None};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

  ASSERT_EQ(res, nullptr);

  PyObject *etyp, *eval, *etb;
  PyErr_Fetch(&etyp, &eval, &etb);
  ASSERT_NE(etyp, nullptr);

  auto typ = Ref<>::steal(etyp);
  auto val = Ref<>::steal(eval);
  auto tb = Ref<>::steal(etb);
  EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_TypeError));
}

TEST_F(ASMGeneratorTest, InvokeInPlaceMultiply) {
  const char* pycode = R"(
def test(a, b):
    a *= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 22);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceOr) {
  const char* pycode = R"(
def test(a, b):
    a |= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 4, 15);
}

TEST_F(ASMGeneratorTest, InvokeInPlacePower) {
  const char* pycode = R"(
def test(a, b):
    a **= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 121);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceRShift) {
  const char* pycode = R"(
def test(a, b):
    a >>= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 2);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceSubtract) {
  const char* pycode = R"(
def test(a, b):
    a -= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 9);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceTrueDivide) {
  const char* pycode = R"(
def test(a, b):
    a /= b
    return a
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);
  {
    auto aval = Ref<>::steal(PyLong_FromLong(1));
    auto bval = Ref<>::steal(PyLong_FromLong(2));
    PyObject* args[] = {aval, bval};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

    ASSERT_NE(res.get(), nullptr);
    ASSERT_EQ(PyFloat_AsDouble(res.get()), 0.5);
  }
  {
    PyObject* args[] = {Py_None, Py_None};
    auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));

    ASSERT_EQ(res, nullptr);

    PyObject *etyp, *eval, *etb;
    PyErr_Fetch(&etyp, &eval, &etb);
    ASSERT_NE(etyp, nullptr);

    auto typ = Ref<>::steal(etyp);
    auto val = Ref<>::steal(eval);
    auto tb = Ref<>::steal(etb);
    EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_TypeError));
  }
}

TEST_F(ASMGeneratorTest, InvokeInPlaceXor) {
  const char* pycode = R"(
def test(a, b):
    a ^= b
    return a
)";

  InPlaceOpTest(this, pycode, 11, 2, 9);
}

TEST_F(ASMGeneratorTest, InvokeInPlaceNotDefined) {
  const char* pycode = R"(
def test():
    a += 1
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);

  PyObject* args[] = {};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 0));

  ASSERT_EQ(res, nullptr);

  PyObject *etyp, *eval, *etb;
  PyErr_Fetch(&etyp, &eval, &etb);
  ASSERT_NE(etyp, nullptr);

  auto typ = Ref<>::steal(etyp);
  auto val = Ref<>::steal(eval);
  auto tb = Ref<>::steal(etb);
  EXPECT_TRUE(PyErr_GivenExceptionMatches(typ, PyExc_UnboundLocalError));
}

TEST_F(ASMGeneratorTest, TestDeepRegUsage) {
  const char* helpercode = R"(
def f(*args):
    return sum(args)
)";
  Ref<PyObject> f(compileAndGet(helpercode, "f"));

  const char* pycode = R"(
def test(a, func):
    if a:
        b = a; c = a; d = a; e = a; f = a; g = a; h = a; i = a; j = a; k = a
        l = a; m = a; n = a; o = a; p = a; q = a; r = a; s = a; t = a
    return func(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t)
)";

  Ref<PyObject> pyfunc(compileAndGet(pycode, "test"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);

  auto aval = Ref<>::steal(PyLong_FromLong(1));

  PyObject* args[] = {aval, f};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));
  ASSERT_EQ(PyLong_AsLong(res), 20);
}

// This can't be tested in the pure Python test suite as it messes with
// __import__.
TEST_F(ASMGeneratorTest, TestImportNameWithImportOverride) {
  const char* pycode = R"(
def test_override_builtin_import(locals):
    captured_data = []

    def fake_import(name, globals, locals, fromlist, level):
        nonlocal captured_data
        captured_data.append([name, len(globals), locals, fromlist, level])

    old_import = __builtins__.__import__
    __builtins__.__import__ = fake_import
    # The CPython interpreter has strange (probably broken) behavior - it will
    # only pass a dictionary of locals to __builtins__.__import__() if the
    # locals have been materialized already, for example by a call to locals().
    # In our implementation of IMPORT_NAME we just always pass None for locals.
    if locals:
        locals()
    import x
    import x.y
    __builtins__.__import__ = old_import
    return repr(captured_data) == "[['x', 7, None, None, 0], ['x.y', 7, None, None, 0]]"
)";

  ASSERT_TRUE(runCode(pycode)) << "Failed compiling";

  auto pyfunc = getGlobal("test_override_builtin_import");
  ASSERT_NE(pyfunc, nullptr)
      << "Failed getting global test_override_builtin_import";
  auto compiled_func = GenerateCode(pyfunc);
  ASSERT_NE(compiled_func.get(), nullptr)
      << "Failed compiling test_override_builtin_import";

  // Without locals() call
  auto locals = Ref<>::steal(PyUnicode_FromString("locals"));
  PyObject* args[] = {Py_False};
  auto res = Ref<>::steal(compiled_func->Invoke(pyfunc, args, 1));
  EXPECT_EQ(res.get(), Py_True)
      << "Failed to run test_override_builtin_import(False)";

  // With locals() call
  PyObject* args1[] = {PyDict_GetItem(PyEval_GetBuiltins(), locals)};
  auto res1 = Ref<>::steal(compiled_func->Invoke(pyfunc, args1, 1));
  EXPECT_EQ(res1.get(), Py_True)
      << "Failed to run test_override_builtin_import(True)";
}

TEST_F(ASMGeneratorTest, GetLength) {
  //  0 LOAD_FAST  0
  //  2 GET_LENGTH
  //  4 RETURN_VALUE
  const char bc[] = {LOAD_FAST, 0, GET_LEN, 0, RETURN_VALUE, 0};
  auto bytecode = Ref<>::steal(PyBytes_FromStringAndSize(bc, sizeof(bc)));
  ASSERT_NE(bytecode.get(), nullptr);
  auto filename = Ref<>::steal(PyUnicode_FromString("filename"));
  auto funcname = Ref<>::steal(PyUnicode_FromString("funcname"));
  auto consts = Ref<>::steal(PyTuple_New(1));
  Py_INCREF(Py_None);
  PyTuple_SET_ITEM(consts.get(), 0, Py_None);
  auto param = Ref<>::steal(PyUnicode_FromString("param"));
  auto varnames = Ref<>::steal(PyTuple_Pack(1, param.get()));
  auto empty_tuple = Ref<>::steal(PyTuple_New(0));
  auto empty_string = Ref<>::steal(PyBytes_FromString(""));
  auto code = Ref<PyCodeObject>::steal(PyCode_New(
      /*argcount=*/1,
      0,
      /*nlocals=*/1,
      0,
      0,
      bytecode,
      consts,
      empty_tuple,
      varnames,
      empty_tuple,
      empty_tuple,
      filename,
      funcname,
      0,
      empty_string));
  ASSERT_NE(code.get(), nullptr);

  auto func = Ref<PyFunctionObject>::steal(PyFunction_New(code, MakeGlobals()));
  ASSERT_NE(func.get(), nullptr);

  auto compiled = GenerateCode(func);
  ASSERT_NE(compiled, nullptr);

  auto arg = Ref<>::steal(PyList_New(3));
  PyList_SET_ITEM(arg.get(), 0, PyLong_FromLong(4));
  PyList_SET_ITEM(arg.get(), 1, PyLong_FromLong(5));
  PyList_SET_ITEM(arg.get(), 2, PyLong_FromLong(6));
  auto args = std::to_array({arg.get()});
  auto result = Ref<>::steal(compiled->Invoke(func, args.data(), args.size()));
  EXPECT_TRUE(isIntEquals(result, 3));
}

class NewASMGeneratorTest : public RuntimeTest {
 public:
  std::unique_ptr<CompiledFunction> GenerateCode(PyObject* func) {
    return Compiler().Compile(func);
  }
};

TEST_F(NewASMGeneratorTest, Linear) {
  const char* src = R"(
def func(x):
  return 16 + x
)";

  Ref<PyObject> pyfunc(compileAndGet(src, "func"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(12));
  ASSERT_NE(arg0, nullptr);
  PyObject* args[] = {arg0};
  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 1));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyLong_AsLong(res), 28);
}

TEST_F(NewASMGeneratorTest, DiamondControlBlock) {
  const char* src = R"(
def func(a, b):
  c = 0
  if a:
    c = b + 100
  else:
    c = b + 4

  return a + c
)";

  Ref<PyObject> pyfunc(compileAndGet(src, "func"));
  ASSERT_NE(pyfunc.get(), nullptr) << "Failed compiling func";

  auto compiled = GenerateCode(pyfunc);
  ASSERT_NE(compiled, nullptr);

  auto arg0 = Ref<>::steal(PyLong_FromLong(1));
  auto arg1 = Ref<>::steal(PyLong_FromLong(5));
  ASSERT_NE(arg0, nullptr);
  PyObject* args[] = {arg0, arg1};

  auto res = Ref<>::steal(compiled->Invoke(pyfunc, args, 2));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyLong_AsLong(res), 106);
}

TEST_F(NewASMGeneratorTest, BlockSorter) {
  jit::lir::Function func;
  std::vector<jit::lir::BasicBlock*> blocks;
  for (int i = 0; i < 6; i++) {
    blocks.push_back(func.allocateBasicBlock());
  }

  // build cfg:

  /*
          --------------
         |     ----     |
         |    |    |    |
         v    v    |    |
    0--->2--->3--->1    4--->5
              |         ^
              |         |
               ---------
  */

  blocks[0]->addSuccessor(blocks[2]);
  blocks[2]->addSuccessor(blocks[3]);
  blocks[3]->addSuccessor(blocks[1]);
  blocks[3]->addSuccessor(blocks[4]);
  blocks[1]->addSuccessor(blocks[3]);
  blocks[4]->addSuccessor(blocks[2]);
  blocks[4]->addSuccessor(blocks[5]);

  func.sortBasicBlocks();

  size_t expected[] = {0, 2, 3, 1, 4, 5};
  for (size_t i = 0; i < 6; i++) {
    ASSERT_EQ(func.basicblocks()[i], blocks[expected[i]]) << "i = " << i;
  }
}
