// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/hir/ssa.h"
#include "Jit/hir/type.h"
#include "Jit/ref.h"

#include "RuntimeTests/fixtures.h"

using namespace jit::hir;

using HIRTypeTest = RuntimeTest;

TEST_F(HIRTypeTest, BuiltinEquality) {
  EXPECT_EQ(TObject, TObject);
  EXPECT_EQ(TTop, TTop);
  EXPECT_NE(TObject, TTop);
  EXPECT_NE(TObject, TUser);
}

TEST_F(HIRTypeTest, BuiltinSubtypes) {
  EXPECT_TRUE(TBottom <= TBottom);
  EXPECT_TRUE(TBottom < TBool);
  EXPECT_TRUE(TBottom <= TBool);
  EXPECT_TRUE(TBottom < TLong);
  EXPECT_TRUE(TBottom <= TLong);
  EXPECT_TRUE(TBottom < TObject);
  EXPECT_TRUE(TBottom <= TObject);
  EXPECT_TRUE(TBottom < TTop);
  EXPECT_TRUE(TBottom <= TTop);

  EXPECT_TRUE(TObjectExact < TObject);
  EXPECT_FALSE(TObject <= TObjectExact);

  EXPECT_TRUE(TBool <= TBool);
  EXPECT_TRUE(TBool < TLong);
  EXPECT_TRUE(TBool <= TLong);
  EXPECT_TRUE(TBool < TObject);
  EXPECT_TRUE(TBool <= TObject);
  EXPECT_TRUE(TBool < TTop);
  EXPECT_TRUE(TBool <= TTop);

  EXPECT_FALSE(TBool < TBool);
  EXPECT_FALSE(TBool < TFloat);
  EXPECT_FALSE(TBool < TLongUser);

  EXPECT_TRUE(TLong <= TLong);
  EXPECT_TRUE(TLong < TObject);
  EXPECT_TRUE(TLong <= TObject);
  EXPECT_TRUE(TLong < TTop);
  EXPECT_TRUE(TLong <= TTop);

  EXPECT_TRUE(TLongUser < TUser);

  EXPECT_FALSE(TLong < TLong);
  EXPECT_FALSE(TLong < TFloat);

  EXPECT_FALSE(TBottom < TBottom);
  EXPECT_FALSE(TTop < TTop);

  EXPECT_FALSE(TTop < TObject);
  EXPECT_LT(TObject, TTop);
  EXPECT_FALSE(TObject < TLong);
  EXPECT_FALSE(TLong < TBool);
  EXPECT_FALSE(TBool < TBottom);

  EXPECT_TRUE(TBaseExceptionUser < TUser);
  EXPECT_TRUE(TUnicodeUser < TUser);

  EXPECT_TRUE(TObject < TOptObject);
  EXPECT_TRUE(TBytes < TOptBytes);
  EXPECT_TRUE(TLong < TOptLong);

  EXPECT_TRUE(TImmortalLong < TLong);
  EXPECT_TRUE(TMortalLong < TLong);
  EXPECT_TRUE(TMortalLongExact < TLong);
  EXPECT_TRUE(TMortalLongExact < TMortalLong);
  EXPECT_TRUE(TMortalLongExact < TLongExact);
  EXPECT_FALSE(TImmortalLongUser < TImmortalLongExact);
  EXPECT_TRUE(TImmortalLong < TImmortalObject);
  EXPECT_FALSE(TMortalLong < TImmortalObject);
}

TEST_F(HIRTypeTest, BuiltinCouldBe) {
  EXPECT_TRUE(TBuiltinExact.couldBe(TLong));
  EXPECT_TRUE(TBytes.couldBe(TBuiltinExact));
  EXPECT_TRUE(TUser.couldBe(TUnicode));
  EXPECT_TRUE(TList.couldBe(TUser));
  EXPECT_TRUE(TLong.couldBe(TMortalObject));
  EXPECT_TRUE(TLong.couldBe(TImmortalObject));
  EXPECT_FALSE(TImmortalLong.couldBe(TMortalObject));
}

TEST_F(HIRTypeTest, FromBuiltinObjects) {
  EXPECT_EQ(Type::fromObject(Py_None), TNoneType);
  EXPECT_TRUE(Type::fromObject(Py_True) < TBool);
  EXPECT_TRUE(Type::fromObject(Py_False) < TLong);

  auto not_impl = Type::fromObject(Py_NotImplemented);
  EXPECT_TRUE(not_impl < TObject);
  ASSERT_TRUE(not_impl.hasObjectSpec());
  EXPECT_EQ(not_impl.objectSpec(), Py_NotImplemented);
  EXPECT_EQ(not_impl.typeSpec(), Py_TYPE(Py_NotImplemented));

  auto long_obj = Type::fromObject(reinterpret_cast<PyObject*>(&PyLong_Type));
  EXPECT_TRUE(long_obj < TType);
  EXPECT_TRUE(long_obj.hasObjectSpec());
  EXPECT_EQ(long_obj.objectSpec(), reinterpret_cast<PyObject*>(&PyLong_Type));
  EXPECT_EQ(long_obj.toString(), "MortalTypeExact[int:obj]");
}

TEST_F(HIRTypeTest, FromBuiltinType) {
  EXPECT_EQ(Type::fromType(&PyLong_Type), TLong);
  EXPECT_EQ(Type::fromType(&PyBool_Type), TBool);
  EXPECT_EQ(Type::fromType(&PyBytes_Type), TBytes);
  EXPECT_EQ(Type::fromType(&PyUnicode_Type), TUnicode);
  EXPECT_EQ(Type::fromType(&PySlice_Type), TSlice);

  EXPECT_EQ(Type::fromType(&PyBaseObject_Type), TObject);
  EXPECT_EQ(Type::fromTypeExact(&PyBaseObject_Type), TObjectExact);

  EXPECT_EQ(Type::fromType(&PyLong_Type), TLong);
  EXPECT_EQ(Type::fromTypeExact(&PyLong_Type), TLongExact);
  EXPECT_EQ(Type::fromType(&PyBool_Type), TBool);
  EXPECT_EQ(Type::fromTypeExact(&PyBool_Type), TBool);

  EXPECT_EQ(
      Type::fromType(reinterpret_cast<PyTypeObject*>(PyExc_BaseException)),
      TBaseException);
  EXPECT_EQ(
      Type::fromTypeExact(reinterpret_cast<PyTypeObject*>(PyExc_BaseException)),
      TBaseExceptionExact);
  auto exc = Type::fromType(reinterpret_cast<PyTypeObject*>(PyExc_Exception));
  EXPECT_EQ(exc.toString(), "BaseExceptionUser[Exception]");
  EXPECT_TRUE(exc < TBaseException);

  auto type = Type::fromType(&PyType_Type);
  auto type_obj = Type::fromObject(reinterpret_cast<PyObject*>(&PyType_Type));
  EXPECT_EQ(type.toString(), "Type");
  EXPECT_EQ(type_obj.toString(), "MortalTypeExact[type:obj]");
  EXPECT_TRUE(type_obj < type);
}

TEST_F(HIRTypeTest, UniquePyType) {
  EXPECT_EQ(TLong.uniquePyType(), &PyLong_Type);
  EXPECT_EQ(TImmortalLong.uniquePyType(), &PyLong_Type);
  EXPECT_EQ(TMortalList.uniquePyType(), &PyList_Type);
  EXPECT_EQ(TBool.uniquePyType(), &PyBool_Type);
  EXPECT_EQ(TUnicode.uniquePyType(), &PyUnicode_Type);
  EXPECT_EQ(TList.uniquePyType(), &PyList_Type);
  EXPECT_EQ(TListExact.uniquePyType(), &PyList_Type);
  EXPECT_EQ(TObject.uniquePyType(), &PyBaseObject_Type);
  EXPECT_EQ(TObjectExact.uniquePyType(), &PyBaseObject_Type);

  EXPECT_EQ(TBuiltinExact.uniquePyType(), nullptr);
  EXPECT_EQ((TLong | TUnicode).uniquePyType(), nullptr);
  EXPECT_EQ((TObject - TLong).uniquePyType(), nullptr);
  EXPECT_EQ(TNullptr.uniquePyType(), nullptr);
  EXPECT_EQ(TCInt32.uniquePyType(), nullptr);

  // None is a singleton, so Type makes no distinction between None the value
  // and NoneType.
  EXPECT_EQ(Type::fromObject(Py_None).uniquePyType(), &_PyNone_Type);

  // Other specialized values don't have unique PyTypeObjects*.
  auto one = Ref<>::steal(PyLong_FromLong(1));
  ASSERT_NE(one, nullptr);
  EXPECT_EQ(Type::fromObject(one).uniquePyType(), nullptr);

  ASSERT_TRUE(runCode(R"(
class MyClass:
  pass
my_obj = MyClass()
)"));
  Ref<PyTypeObject> my_class(getGlobal("MyClass"));
  ASSERT_NE(my_class, nullptr);
  Ref<> my_obj(getGlobal("my_obj"));
  ASSERT_NE(my_obj, nullptr);

  EXPECT_EQ(Type::fromType(my_class).uniquePyType(), my_class);
  EXPECT_EQ(Type::fromTypeExact(my_class).uniquePyType(), my_class);
  EXPECT_EQ(Type::fromObject(my_class).uniquePyType(), nullptr);
  EXPECT_EQ(Type::fromObject(my_obj).uniquePyType(), nullptr);
}

TEST_F(HIRTypeTest, IsExact) {
  EXPECT_FALSE(TObject.isExact());
  EXPECT_TRUE(TObjectExact.isExact());
  EXPECT_TRUE(TBool.isExact());
  EXPECT_FALSE(TLong.isExact());
  EXPECT_TRUE(TLongExact.isExact());
  EXPECT_TRUE((TUnicodeExact | TListExact).isExact());
  EXPECT_FALSE((TUnicodeExact | TList).isExact());

  EXPECT_FALSE(TNullptr.isExact());
  EXPECT_FALSE(TCInt32.isExact());
  EXPECT_FALSE(TCBool.isExact());

  EXPECT_TRUE(TMortalListExact.isExact());
  EXPECT_FALSE(TMortalList.isExact());

  auto three = Ref<>::steal(PyLong_FromLong(3));
  EXPECT_TRUE(Type::fromObject(three).isExact());

  ASSERT_TRUE(runCode(R"(
class MyClass:
  pass
my_obj = MyClass()
)"));
  Ref<PyTypeObject> my_class(getGlobal("MyClass"));
  ASSERT_NE(my_class, nullptr);
  Ref<> my_obj(getGlobal("my_obj"));
  ASSERT_NE(my_obj, nullptr);

  EXPECT_FALSE(Type::fromType(my_class).isExact());
  EXPECT_TRUE(Type::fromTypeExact(my_class).isExact());
  EXPECT_TRUE(Type::fromObject(my_class).isExact());
  EXPECT_TRUE(Type::fromObject(my_obj).isExact());
}

TEST_F(HIRTypeTest, ToString) {
  EXPECT_EQ(TLong.toString(), "Long");
  EXPECT_EQ(TObject.toString(), "Object");

  EXPECT_EQ((TBytes | TCInt32).toString(), "{Bytes|CInt32}");
  EXPECT_EQ(TCInt8.toString(), "CInt8");
  EXPECT_EQ(TCInt16.toString(), "CInt16");
  EXPECT_EQ(TCInt32.toString(), "CInt32");
  EXPECT_EQ(TCInt64.toString(), "CInt64");
  EXPECT_EQ(TCUInt8.toString(), "CUInt8");
  EXPECT_EQ(TCUInt16.toString(), "CUInt16");
  EXPECT_EQ(TCUInt32.toString(), "CUInt32");
  EXPECT_EQ(TCUInt64.toString(), "CUInt64");

  EXPECT_EQ((TList | TNullptr | TCInt64).toString(), "{CInt64|List|Nullptr}");

  EXPECT_EQ(TMortalList.toString(), "MortalList");
  EXPECT_EQ(TImmortalList.toString(), "ImmortalList");
  EXPECT_EQ(TOptImmortalTuple.toString(), "OptImmortalTuple");
  EXPECT_EQ(
      (TMortalObject & (TList | TTuple)).toString(), "Mortal{List|Tuple}");

  // These weird types are mostly impossible to hit in real code, but it's
  // easier to support them with a fully-general solution than to special case
  // the types we do care about.
  EXPECT_EQ(
      (TImmortalDict | TImmortalSet | TCInt64).toString(),
      "{CInt64|Immortal{Dict|Set}}");
  EXPECT_EQ(
      (TImmortalDict | TImmortalSet | TCInt64 | TCBool).toString(),
      "{CBool|CInt64|Immortal{Dict|Set}}");
  EXPECT_EQ(
      (TNullptr | TImmortalDict | TImmortalLong).toString(),
      "{Immortal{Dict|Long}|Nullptr}");
  EXPECT_EQ((TCBool | TImmortalUnicode).toString(), "{CBool|ImmortalUnicode}");
  EXPECT_EQ(
      (TMortalDict | TCBool | TNullptr).toString(),
      "{CBool|MortalDict|Nullptr}");

  EXPECT_EQ(
      Type::fromCPtr(reinterpret_cast<void*>(0x12345)).toString(),
      "CPtr[0xdeadbeef]");

  EXPECT_EQ(Type::fromObject(Py_True).toString(), "MortalBool[True]");
  EXPECT_EQ(Type::fromObject(Py_False).toString(), "MortalBool[False]");

  auto llong_max = Ref<>::steal(PyLong_FromLongLong(LLONG_MAX));
  ASSERT_NE(llong_max, nullptr);
  auto i = Ref<>::steal(PyLong_FromLong(24));
  ASSERT_NE(i, nullptr);
  auto negi = Ref<>::steal(PyNumber_Negative(i));
  ASSERT_NE(negi, nullptr);
  auto overflow = Ref<>::steal(PyNumber_Add(llong_max, i));
  ASSERT_NE(overflow, nullptr);
  auto underflow = Ref<>::steal(PyNumber_Multiply(llong_max, negi));
  ASSERT_NE(underflow, nullptr);

  EXPECT_EQ(Type::fromObject(i).toString(), "MortalLongExact[24]");
  EXPECT_EQ(Type::fromObject(negi).toString(), "MortalLongExact[-24]");
  EXPECT_EQ(Type::fromObject(overflow).toString(), "MortalLongExact[overflow]");
  EXPECT_EQ(
      Type::fromObject(underflow).toString(), "MortalLongExact[underflow]");

  auto dbl = Ref<>::steal(PyFloat_FromDouble(1234.5));
  ASSERT_NE(dbl, nullptr);
  EXPECT_EQ(Type::fromObject(dbl).toString(), "MortalFloatExact[1234.5]");

  auto str = Ref<>::steal(PyUnicode_FromString("Hello there!"));
  ASSERT_NE(str, nullptr);
  EXPECT_EQ(
      Type::fromObject(str).toString(), "MortalUnicodeExact[\"Hello there!\"]");

  auto long_str = Ref<>::steal(
      PyUnicode_FromString("The quick brown fox jumps over the lazy dog."));
  ASSERT_NE(long_str, nullptr);
  EXPECT_EQ(
      Type::fromObject(long_str).toString(),
      "MortalUnicodeExact[\"The quick brown fox \"...]");

  auto bytes = Ref<>::steal(PyBytes_FromString("hi"));
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(Type::fromObject(bytes).toString(), "MortalBytesExact['hi']");

  EXPECT_EQ(Type::fromCBool(true).toString(), "CBool[true]");
  EXPECT_EQ(Type::fromCBool(false).toString(), "CBool[false]");

  EXPECT_EQ(Type::fromCInt(127, TCInt8).toString(), "CInt8[127]");
  EXPECT_EQ(Type::fromCUInt(255, TCUInt8).toString(), "CUInt8[255]");

  EXPECT_EQ(Type::fromCInt(32123, TCInt16).toString(), "CInt16[32123]");
  EXPECT_EQ(Type::fromCUInt(56789, TCUInt16).toString(), "CUInt16[56789]");

  EXPECT_EQ(Type::fromCInt(1234, TCInt32).toString(), "CInt32[1234]");
  EXPECT_EQ(Type::fromCUInt(1234, TCUInt32).toString(), "CUInt32[1234]");

  EXPECT_EQ(Type::fromCInt(56789, TCInt64).toString(), "CInt64[56789]");
  EXPECT_EQ(Type::fromCUInt(56789, TCUInt64).toString(), "CUInt64[56789]");

  ASSERT_TRUE(runCode("class MyClass: pass\nobj = MyClass()"));
  Ref<> my_pyobj(getGlobal("obj"));
  ASSERT_NE(my_pyobj, nullptr);
  auto my_obj = Type::fromObject(my_pyobj);
  EXPECT_EQ(my_obj.toString(), "MortalObjectUser[MyClass:0xdeadbeef]");

  ASSERT_TRUE(runCode("obj = len"));
  Ref<> len_func(getGlobal("obj"));
  ASSERT_NE(len_func, nullptr);
  auto len_func_type = Type::fromObject(len_func);
  EXPECT_EQ(
      len_func_type.toString(),
      "MortalObjectUser[builtin_function_or_method:len:0xdeadbeef]");
}

static ::testing::AssertionResult
isLongTypeWithValue(Type actual, Type expected, Py_ssize_t value) {
  if (!(actual <= expected)) {
    return ::testing::AssertionFailure()
        << "Expected " << actual.toString() << " <= " << expected.toString()
        << ", but it was not";
  }
  if (!actual.hasObjectSpec()) {
    return ::testing::AssertionFailure() << "Expected " << actual.toString()
                                         << " to have int spec but it did not";
  }
  PyObject* obj = actual.objectSpec();
  if (PyLong_AsLong(obj) != value) {
    return ::testing::AssertionFailure()
        << "Expected " << actual.toString() << " to be == " << value
        << " but it was not";
  }
  return ::testing::AssertionSuccess();
}

static Type typeParseSimple(const char* str) {
  return Type::parse(/*env=*/nullptr, str);
}

TEST_F(HIRTypeTest, Parse) {
  EXPECT_EQ(typeParseSimple("Top"), TTop);
  EXPECT_EQ(typeParseSimple("Bottom"), TBottom);
  EXPECT_EQ(typeParseSimple("NoneType"), TNoneType);
  EXPECT_EQ(typeParseSimple("Long"), TLong);
  EXPECT_EQ(typeParseSimple("ImmortalTuple"), TImmortalTuple);
  EXPECT_EQ(typeParseSimple("MortalUser"), TMortalUser);

  EXPECT_EQ(typeParseSimple("CInt64[123456]"), Type::fromCInt(123456, TCInt64));
  EXPECT_EQ(typeParseSimple("CUInt8[42]"), Type::fromCUInt(42, TCUInt8));
  EXPECT_EQ(typeParseSimple("CInt32[-5678]"), Type::fromCInt(-5678, TCInt32));
  EXPECT_EQ(typeParseSimple("CBool[true]"), Type::fromCBool(true));
  EXPECT_EQ(typeParseSimple("CBool[false]"), Type::fromCBool(false));
  EXPECT_EQ(typeParseSimple("CBool[banana]"), TBottom);
  EXPECT_EQ(typeParseSimple("Bool[True]"), Type::fromObject(Py_True));
  EXPECT_EQ(typeParseSimple("Bool[False]"), Type::fromObject(Py_False));
  EXPECT_EQ(typeParseSimple("Bool[banana]"), TBottom);

  // Unknown types or unsupported specializations parse to Bottom
  EXPECT_EQ(typeParseSimple("Bootom"), TBottom);
  EXPECT_EQ(typeParseSimple("Banana"), TBottom);
}

TEST_F(HIRTypeTest, ParsePyObject) {
  Environment env;
  EXPECT_TRUE(isLongTypeWithValue(Type::parse(&env, "Long[1]"), TLong, 1));
  EXPECT_TRUE(
      isLongTypeWithValue(Type::parse(&env, "MortalLong[2]"), TMortalLong, 2));
  EXPECT_TRUE(isLongTypeWithValue(
      Type::parse(&env, "MortalLongExact[3]"), TMortalLongExact, 3));
  EXPECT_EQ(
      Type::parse(&env, "Long[123123123123123123123123123123123123]"), TBottom);
}

TEST_F(HIRTypeTest, SimpleUnion) {
  auto t1 = TBytes;
  auto t2 = TUnicode;
  auto u = t1 | t2;
  EXPECT_LT(t1, u);
  EXPECT_LT(t2, u);
  EXPECT_EQ(u.toString(), "{Bytes|Unicode}");

  EXPECT_EQ(TLongUser | TBool | TLongExact, TLong);

  EXPECT_EQ(TOptCode, TCode | TNullptr);
  EXPECT_EQ(TOptBytesExact, TBytesExact | TNullptr);
  EXPECT_EQ(TOptUnicode, TUnicode | TNullptr);
  EXPECT_EQ(TOptObject, TObject | TNullptr);

  EXPECT_EQ(TMortalUnicode | TImmortalUnicode, TUnicode);
  EXPECT_EQ(TMortalLong | TImmortalDict, TLong | TDict);
}

TEST_F(HIRTypeTest, SimpleIntersection) {
  EXPECT_EQ(TList & TLong, TBottom);
  EXPECT_EQ(TLong & TUser, TLongUser);
  EXPECT_EQ(TBytes & TBuiltinExact, TBytesExact);
  EXPECT_EQ(TCode & TUser, TBottom);
  EXPECT_EQ(TFunc & TBuiltinExact, TFunc);

  auto t1 = TUnicode | TBytes | TLong;
  auto t2 = TBool | TUser;
  auto t3 = t1 & t2;
  EXPECT_EQ(t3, TBool | TUnicodeUser | TBytesUser | TLongUser);

  EXPECT_EQ(TLong & TMortalObject, TMortalLong);
  EXPECT_EQ(TMortalList & TImmortalList, TBottom);
  EXPECT_EQ(TMortalList & TMortalDict, TBottom);
}

TEST_F(HIRTypeTest, SimpleSubtraction) {
  EXPECT_EQ(TLong - TBool - TLongUser, TLongExact);
  EXPECT_EQ(
      TUser - TBytes - TDict - TSet - TArray - TFloat - TList - TTuple -
          TUnicode - TType - TBaseException - TLong,
      TObjectUser);
  EXPECT_EQ(TUnicode - TUnicodeExact, TUnicodeUser);
  EXPECT_EQ(TLong - TBool, TLongExact | TLongUser);
  EXPECT_EQ(TOptLong - TNullptr, TLong);
  EXPECT_EQ(TTop - TObject, TPrimitive);

  EXPECT_EQ(TList - TMortalList, TImmortalList);
  EXPECT_EQ(TList - TImmortalObject, TMortalList);
  EXPECT_EQ(TMortalObject - TImmortalObject, TMortalObject);
  EXPECT_EQ(TMortalLong - TMortalObject, TBottom);
  EXPECT_EQ(TOptMortalList - TNullptr, TMortalList);
}

TEST_F(HIRTypeTest, SpecializedIntegerTypes) {
  auto five = Type::fromCInt(5, TCInt32);
  auto five64 = Type::fromCInt(5, TCInt64);
  auto ten = Type::fromCInt(10, TCInt32);
  auto ctrue = Type::fromCBool(true);

  ASSERT_TRUE(five.hasIntSpec());
  EXPECT_EQ(five.intSpec(), 5);
  ASSERT_TRUE(five64.hasIntSpec());
  EXPECT_EQ(five64.intSpec(), 5);
  ASSERT_TRUE(ctrue.hasIntSpec());
  EXPECT_EQ(ctrue.intSpec(), true);

  EXPECT_TRUE(five <= five);
  EXPECT_FALSE(five <= five64);
  EXPECT_FALSE(five <= ten);
  EXPECT_EQ(five & five, five);
  EXPECT_EQ(TCInt32 & five, five);
  EXPECT_EQ(TCInt32 & five64, TBottom);
  EXPECT_EQ(five | five64, TCInt32 | TCInt64);
  EXPECT_EQ(five & five64, TBottom);
  EXPECT_EQ(five | ten, TCInt32);
  EXPECT_EQ(five & ten, TBottom);

  EXPECT_EQ(five | five, five);
  EXPECT_TRUE(TBottom <= five);
  EXPECT_TRUE(TBottom < five);
  EXPECT_EQ(five | TBottom, five);
  EXPECT_EQ(TBottom | five, five);

  auto py_long1 = Ref<>::steal(PyLong_FromLong(24));
  ASSERT_NE(py_long1, nullptr);
  auto py_long2 = Ref<>::steal(PyLong_FromLong(42));
  ASSERT_NE(py_long2, nullptr);
  auto long_ty1 = Type::fromObject(py_long1);
  auto long_ty2 = Type::fromObject(py_long2);
  auto long_ty = long_ty1 | long_ty2;
  EXPECT_FALSE(long_ty.hasTypeSpec());
  EXPECT_EQ(long_ty, TMortalLongExact);
}

TEST_F(HIRTypeTest, SpecializedDoubleTypes) {
  auto five = Type::fromCDouble(5.0);

  ASSERT_TRUE(five.hasDoubleSpec());
  ASSERT_TRUE(five.hasDoubleSpec());
  ASSERT_FALSE(five.hasTypeSpec());
  EXPECT_EQ(five.doubleSpec(), 5.0);

  EXPECT_TRUE(five <= five);
  EXPECT_EQ(five & five, five);
  EXPECT_EQ(TCDouble & five, five);
  EXPECT_TRUE(five <= TCDouble);
  EXPECT_TRUE(five < TCDouble);
  EXPECT_NE(five, Type::fromCDouble(5.1));
  EXPECT_EQ(five & Type::fromCDouble(1.0), TBottom);
}

TEST_F(HIRTypeTest, Metaclasses) {
  const char* py_src = R"(
class Metaclass(type):
  pass

class MyClass(metaclass=Metaclass):
  pass

obj = MyClass()
)";
  ASSERT_TRUE(runCode(py_src));

  Ref<PyTypeObject> metaclass_pytype(getGlobal("Metaclass"));
  ASSERT_NE(metaclass_pytype, nullptr);
  Ref<PyTypeObject> my_class_pytype(getGlobal("MyClass"));
  ASSERT_NE(my_class_pytype, nullptr);
  Ref<> obj_pyobj(getGlobal("obj"));
  ASSERT_NE(obj_pyobj, nullptr);

  auto metaclass = Type::fromType(metaclass_pytype);
  auto metaclass_obj = Type::fromObject(metaclass_pytype);
  auto my_class = Type::fromType(my_class_pytype);
  auto my_class_obj = Type::fromObject(my_class_pytype);
  auto obj = Type::fromObject(obj_pyobj);

  EXPECT_EQ(metaclass.toString(), "TypeUser[Metaclass]");
  EXPECT_EQ(metaclass_obj.toString(), "MortalTypeExact[Metaclass:obj]");
  EXPECT_EQ(my_class.toString(), "User[MyClass]");
  EXPECT_EQ(my_class_obj.toString(), "MortalTypeUser[MyClass:obj]");
  EXPECT_EQ(obj.toString(), "MortalObjectUser[MyClass:0xdeadbeef]");

  EXPECT_TRUE(metaclass < TTypeUser);
  EXPECT_TRUE(metaclass_obj < TTypeExact);
  EXPECT_TRUE(my_class < TObject);
  EXPECT_TRUE(my_class_obj < TType);
  EXPECT_TRUE(my_class_obj < metaclass);
  EXPECT_TRUE(obj < my_class);

  EXPECT_FALSE(metaclass <= metaclass_obj);
  EXPECT_FALSE(my_class <= metaclass_obj);
  EXPECT_FALSE(my_class_obj <= metaclass_obj);
  EXPECT_FALSE(obj <= metaclass);
}

TEST_F(HIRTypeTest, TypeUserSpecializations) {
  const char* py_src = R"(
class MyClass:
  pass

class MySubclass(MyClass):
  pass

class MyInt(int):
  pass

class MyStr(str):
  pass
)";
  ASSERT_TRUE(runCode(py_src));

  Ref<PyTypeObject> my_class_pytype(getGlobal("MyClass"));
  ASSERT_NE(my_class_pytype, nullptr);
  Ref<PyTypeObject> my_subclass_pytype(getGlobal("MySubclass"));
  ASSERT_NE(my_subclass_pytype, nullptr);
  Ref<PyTypeObject> my_int_pytype(getGlobal("MyInt"));
  ASSERT_NE(my_int_pytype, nullptr);
  Ref<PyTypeObject> my_str_pytype(getGlobal("MyStr"));
  ASSERT_NE(my_str_pytype, nullptr);

  auto my_class = Type::fromType(my_class_pytype);
  auto my_class_exact = Type::fromTypeExact(my_class_pytype);
  auto my_subclass = Type::fromType(my_subclass_pytype);
  auto my_subclass_exact = Type::fromTypeExact(my_subclass_pytype);
  auto my_int = Type::fromType(my_int_pytype);
  auto my_int_exact = Type::fromTypeExact(my_int_pytype);
  auto my_str = Type::fromType(my_str_pytype);
  auto my_str_exact = Type::fromTypeExact(my_str_pytype);

  EXPECT_EQ(my_class.toString(), "User[MyClass]");
  EXPECT_EQ(my_class_exact.toString(), "ObjectUser[MyClass:Exact]");
  EXPECT_EQ(my_subclass.toString(), "User[MySubclass]");
  EXPECT_EQ(my_subclass_exact.toString(), "ObjectUser[MySubclass:Exact]");
  EXPECT_EQ(my_int.toString(), "LongUser[MyInt]");
  EXPECT_EQ(my_int_exact.toString(), "LongUser[MyInt:Exact]");
  EXPECT_EQ(my_str.toString(), "UnicodeUser[MyStr]");
  EXPECT_EQ(my_str_exact.toString(), "UnicodeUser[MyStr:Exact]");

  EXPECT_TRUE(my_class < TUser);
  EXPECT_TRUE(my_class_exact < my_class);
  EXPECT_FALSE(my_class < my_int);
  EXPECT_FALSE(my_class < my_str);

  EXPECT_TRUE(my_subclass < TUser);
  EXPECT_TRUE(my_subclass < my_class);
  EXPECT_TRUE(my_subclass_exact < my_class);
  EXPECT_FALSE(my_subclass_exact < my_class_exact);
  EXPECT_FALSE(my_subclass < my_int);
  EXPECT_FALSE(my_subclass < my_str);

  EXPECT_EQ(my_class_exact | my_subclass_exact, my_class & TObjectUser);
  EXPECT_EQ(my_class_exact | my_class_exact, my_class_exact);

  EXPECT_TRUE(my_int < TUser);
  EXPECT_TRUE(my_int < TLong);
  EXPECT_TRUE(my_int < TLongUser);
  EXPECT_FALSE(my_int < TUnicode);
  EXPECT_FALSE(my_int < my_class);
  EXPECT_FALSE(my_int < my_subclass);
  EXPECT_TRUE(my_int_exact < my_int);
  EXPECT_FALSE(my_int < my_str);

  EXPECT_TRUE(my_str < TUser);
  EXPECT_TRUE(my_str < TUnicode);
  EXPECT_TRUE(my_str < TUnicodeUser);
  EXPECT_FALSE(my_str < TLong);
  EXPECT_FALSE(my_str < my_class);
  EXPECT_FALSE(my_str < my_subclass);
  EXPECT_FALSE(my_str < my_int);
  EXPECT_TRUE(my_str_exact < my_str);

  EXPECT_EQ(my_class & my_class_exact, my_class_exact);
  EXPECT_EQ((my_class & my_int).toString(), "LongUser[MyClass]");
  EXPECT_EQ((my_int & my_class).toString(), "LongUser[MyClass]");
  EXPECT_EQ(my_int & my_str, TBottom);

  auto call_type = [](PyTypeObject* ty) {
    return PyObject_CallObject(reinterpret_cast<PyObject*>(ty), nullptr);
  };
  auto class_pyobj = Ref<>::steal(call_type(my_class_pytype));
  ASSERT_NE(class_pyobj, nullptr);
  auto class_pyobj2 = Ref<>::steal(call_type(my_class_pytype));
  ASSERT_NE(class_pyobj2, nullptr);
  auto subclass_pyobj = Ref<>::steal(call_type(my_subclass_pytype));
  ASSERT_NE(subclass_pyobj, nullptr);
  auto int_pyobj = Ref<>::steal(call_type(my_int_pytype));
  ASSERT_NE(int_pyobj, nullptr);
  auto int_pyobj2 = Ref<>::steal(call_type(my_int_pytype));
  ASSERT_NE(int_pyobj2, nullptr);
  auto str_pyobj = Ref<>::steal(call_type(my_str_pytype));
  ASSERT_NE(str_pyobj, nullptr);
  auto str_pyobj2 = Ref<>::steal(call_type(my_str_pytype));
  ASSERT_NE(str_pyobj2, nullptr);

  auto class_obj = Type::fromObject(class_pyobj);
  auto class_obj2 = Type::fromObject(class_pyobj2);
  auto subclass_obj = Type::fromObject(subclass_pyobj);
  auto int_obj = Type::fromObject(int_pyobj);
  auto int_obj2 = Type::fromObject(int_pyobj2);
  auto str_obj = Type::fromObject(str_pyobj);
  auto str_obj2 = Type::fromObject(str_pyobj2);

  EXPECT_TRUE(class_obj.hasValueSpec(TUser));
  EXPECT_TRUE(class_obj.hasValueSpec(my_class));
  EXPECT_FALSE(class_obj.hasValueSpec(TLong));
  EXPECT_FALSE(class_obj.hasValueSpec(my_subclass));
  EXPECT_TRUE(int_obj.hasValueSpec(TLong));

  // MyClass
  EXPECT_NE(class_obj, TBottom);
  EXPECT_NE(class_obj, my_class);
  EXPECT_TRUE(class_obj <= class_obj);
  EXPECT_TRUE(class_obj <= my_class);
  EXPECT_TRUE(class_obj < my_class);
  EXPECT_TRUE(class_obj < TObjectUser);
  EXPECT_FALSE(my_class <= class_obj);

  EXPECT_EQ(my_class & my_class, my_class);
  EXPECT_EQ(my_class & class_obj, class_obj);
  EXPECT_NE(class_obj, class_obj2);
  EXPECT_EQ(class_obj & class_obj, class_obj);
  EXPECT_EQ(class_obj & class_obj2, TBottom);
  EXPECT_EQ(class_obj | class_obj, class_obj);

  auto pure_class = my_class & TObjectUser;
  EXPECT_TRUE(pure_class.hasTypeSpec());
  EXPECT_EQ(pure_class.typeSpec(), my_class_pytype);
  EXPECT_FALSE(class_obj <= class_obj2);
  EXPECT_EQ(class_obj - class_obj2, class_obj);
  EXPECT_EQ(class_obj - my_subclass, class_obj);

  EXPECT_EQ(class_obj & TObject, class_obj);
  EXPECT_EQ(TObject & class_obj, class_obj);

  EXPECT_EQ(my_class | TLong, TUser | TLong);
  EXPECT_EQ(my_class | TObjectUser, TUser);
  EXPECT_EQ(class_obj | TUser, TUser);
  EXPECT_EQ(class_obj | TObjectUser, TObjectUser);
  EXPECT_EQ(class_obj | int_obj, TMortalObjectUser | TMortalLongUser);

  EXPECT_FALSE(my_class_exact < class_obj);
  EXPECT_TRUE(class_obj < my_class_exact);
  EXPECT_EQ(class_obj | my_class_exact, my_class_exact);

  auto bytes_class = my_class & TBytes;
  auto list_class = my_class & TList;
  EXPECT_EQ(bytes_class.toString(), "BytesUser[MyClass]");
  EXPECT_EQ(list_class.toString(), "ListUser[MyClass]");
  EXPECT_EQ(bytes_class & list_class, TBottom);
  EXPECT_TRUE(bytes_class < my_class);
  EXPECT_TRUE(list_class < my_class);
  EXPECT_FALSE(my_class <= bytes_class);
  EXPECT_FALSE(my_class <= list_class);
  EXPECT_FALSE(class_obj < bytes_class);
  EXPECT_FALSE(class_obj < list_class);

  auto both_class = bytes_class | list_class;
  EXPECT_TRUE(both_class.hasTypeSpec());
  EXPECT_EQ(both_class.typeSpec(), my_class_pytype);
  EXPECT_TRUE(bytes_class < both_class);
  EXPECT_TRUE(list_class < both_class);
  EXPECT_EQ(both_class - bytes_class, list_class);
  EXPECT_EQ(both_class - list_class, bytes_class);
  EXPECT_EQ(bytes_class - both_class, TBottom);

  // MySubclass
  EXPECT_EQ(my_class & my_subclass, my_subclass);
  EXPECT_EQ(class_obj & my_subclass, TBottom);
  EXPECT_EQ(subclass_obj & my_class, subclass_obj);
  EXPECT_EQ(subclass_obj & my_subclass, subclass_obj);
  EXPECT_EQ(subclass_obj | class_obj, TMortalObjectUser & my_class);
  EXPECT_EQ(class_obj | subclass_obj, my_class & TMortalObjectUser);
  EXPECT_EQ(subclass_obj | my_class_exact, my_class & TObjectUser);
  EXPECT_FALSE(subclass_obj < my_class_exact);

  // MyInt
  EXPECT_NE(int_obj, TBottom);
  EXPECT_NE(int_obj, my_int);
  EXPECT_TRUE(int_obj <= int_obj);
  EXPECT_TRUE(int_obj <= my_int);
  EXPECT_TRUE(int_obj < my_int);
  EXPECT_FALSE(my_int <= int_obj);
  EXPECT_TRUE(int_obj < TLongUser);

  EXPECT_EQ(my_int & my_int, my_int);
  EXPECT_EQ(my_int & int_obj, int_obj);
  EXPECT_NE(int_obj, int_obj2);
  EXPECT_EQ(int_obj & int_obj, int_obj);
  EXPECT_EQ(int_obj & int_obj2, TBottom);
  EXPECT_EQ(int_obj | int_obj, int_obj);

  // MyStr
  EXPECT_NE(str_obj, TBottom);
  EXPECT_NE(str_obj, my_str);
  EXPECT_TRUE(str_obj <= str_obj);
  EXPECT_TRUE(str_obj <= my_str);
  EXPECT_TRUE(str_obj < my_str);
  EXPECT_FALSE(my_str <= str_obj);
  EXPECT_TRUE(str_obj < TUnicodeUser);

  EXPECT_EQ(my_str & my_str, my_str);
  EXPECT_EQ(my_str & str_obj, str_obj);
  EXPECT_NE(str_obj, str_obj2);
  EXPECT_EQ(str_obj & str_obj, str_obj);
  EXPECT_EQ(str_obj & str_obj2, TBottom);
  EXPECT_EQ(str_obj | str_obj, str_obj);

  EXPECT_NE(class_obj, int_obj);
  EXPECT_NE(class_obj, str_obj);
  EXPECT_NE(int_obj, str_obj);

  // Primitive types
  auto five = Type::fromCInt(5, TCInt32);
  EXPECT_FALSE(five < my_class);
  EXPECT_FALSE(my_class < five);
  EXPECT_EQ(five & my_class, TBottom);
  EXPECT_EQ(five | my_class, TCInt32 | TUser);
  EXPECT_EQ(class_obj | five, TCInt32 | TMortalObjectUser);
}

TEST_F(HIRTypeTest, UserExceptionInheritance) {
  const char* py_src = R"(
class MyBaseException(BaseException): pass
class MySubBaseException(MyBaseException): pass
class MyException(Exception): pass
class MyBoth(MyException, MyBaseException): pass
)";
  ASSERT_TRUE(runCode(py_src));

  Ref<PyTypeObject> my_base_exc_pytype(getGlobal("MyBaseException"));
  ASSERT_NE(my_base_exc_pytype, nullptr);
  Ref<PyTypeObject> my_sub_base_exc_pytype(getGlobal("MySubBaseException"));
  ASSERT_NE(my_sub_base_exc_pytype, nullptr);
  Ref<PyTypeObject> my_exc_pytype(getGlobal("MyException"));
  ASSERT_NE(my_exc_pytype, nullptr);
  Ref<PyTypeObject> my_both_pytype(getGlobal("MyBoth"));
  ASSERT_NE(my_both_pytype, nullptr);

  auto my_base_exc = Type::fromType(my_base_exc_pytype);
  auto my_base_exc_exact = Type::fromTypeExact(my_base_exc_pytype);
  auto my_sub_base_exc = Type::fromType(my_sub_base_exc_pytype);
  auto my_exc = Type::fromType(my_exc_pytype);
  auto my_exc_exact = Type::fromTypeExact(my_exc_pytype);
  auto my_both = Type::fromType(my_both_pytype);
  auto my_both_exact = Type::fromTypeExact(my_both_pytype);

  EXPECT_EQ(my_base_exc.toString(), "BaseExceptionUser[MyBaseException]");
  EXPECT_EQ(
      my_base_exc_exact.toString(), "BaseExceptionUser[MyBaseException:Exact]");
  EXPECT_EQ(
      my_sub_base_exc.toString(), "BaseExceptionUser[MySubBaseException]");
  EXPECT_EQ(my_exc.toString(), "BaseExceptionUser[MyException]");
  EXPECT_EQ(my_exc_exact.toString(), "BaseExceptionUser[MyException:Exact]");
  EXPECT_EQ(my_both.toString(), "BaseExceptionUser[MyBoth]");
  EXPECT_EQ(my_both_exact.toString(), "BaseExceptionUser[MyBoth:Exact]");

  EXPECT_TRUE(my_base_exc < TBaseExceptionUser);
  EXPECT_FALSE(my_base_exc <= my_exc);
  EXPECT_FALSE(my_base_exc <= my_sub_base_exc);
  EXPECT_FALSE(my_base_exc <= my_both);
  EXPECT_TRUE(my_base_exc_exact < my_base_exc);

  EXPECT_TRUE(my_sub_base_exc < TBaseExceptionUser);
  EXPECT_TRUE(my_sub_base_exc < my_base_exc);
  EXPECT_FALSE(my_sub_base_exc <= my_exc);

  EXPECT_TRUE(my_exc < TBaseExceptionUser);
  EXPECT_FALSE(my_exc <= my_base_exc);
  EXPECT_TRUE(my_exc_exact < my_exc);

  EXPECT_TRUE(my_both < TBaseExceptionUser);
  EXPECT_TRUE(my_both < TBaseException);
  EXPECT_TRUE(my_both < my_base_exc);
  EXPECT_TRUE(my_both < my_exc);
  EXPECT_TRUE(my_both < (my_base_exc & my_exc));
  EXPECT_TRUE(my_both_exact < my_both);

  EXPECT_EQ(
      (my_exc & my_base_exc).toString(), "BaseExceptionUser[MyBaseException]");
  EXPECT_EQ(my_base_exc_exact & my_exc, TBottom);
  EXPECT_EQ(my_base_exc & my_exc_exact, TBottom);
  EXPECT_EQ(my_base_exc_exact & my_exc_exact, TBottom);
}

TEST_F(HIRTypeTest, BuiltinMultipleInheritance) {
  const char* py_src = R"(
class ObjectSub:
  pass

class IntSub(int):
  pass

class IntObjectSub(int, ObjectSub):
  pass

class IntSubObjectSub(IntSub, ObjectSub):
  pass

class IntSubObjectSub2(IntSub, ObjectSub):
  pass
)";
  ASSERT_TRUE(runCode(py_src));

  Ref<PyTypeObject> obj_sub_pytype(getGlobal("ObjectSub"));
  ASSERT_NE(obj_sub_pytype, nullptr);
  Ref<PyTypeObject> int_sub_pytype(getGlobal("IntSub"));
  ASSERT_NE(int_sub_pytype, nullptr);
  Ref<PyTypeObject> int_obj_sub_pytype(getGlobal("IntObjectSub"));
  ASSERT_NE(int_obj_sub_pytype, nullptr);
  Ref<PyTypeObject> int_sub_obj_sub_pytype(getGlobal("IntSubObjectSub"));
  ASSERT_NE(int_sub_obj_sub_pytype, nullptr);
  Ref<PyTypeObject> int_sub_obj_sub2_pytype(getGlobal("IntSubObjectSub2"));

  auto obj_sub = Type::fromType(obj_sub_pytype);
  auto int_sub = Type::fromType(int_sub_pytype);
  auto int_sub_exact = Type::fromTypeExact(int_sub_pytype);
  auto int_obj_sub = Type::fromType(int_obj_sub_pytype);
  auto int_sub_obj_sub = Type::fromType(int_sub_obj_sub_pytype);
  auto int_sub_obj_sub2 = Type::fromType(int_sub_obj_sub2_pytype);

  EXPECT_EQ(obj_sub.toString(), "User[ObjectSub]");
  EXPECT_EQ(int_sub.toString(), "LongUser[IntSub]");
  EXPECT_EQ(int_sub_exact.toString(), "LongUser[IntSub:Exact]");
  EXPECT_EQ(int_obj_sub.toString(), "LongUser[IntObjectSub]");
  EXPECT_EQ(int_sub_obj_sub.toString(), "LongUser[IntSubObjectSub]");
  EXPECT_EQ(int_sub_obj_sub2.toString(), "LongUser[IntSubObjectSub2]");

  EXPECT_TRUE(obj_sub < TObject);
  EXPECT_FALSE(obj_sub < TLong);
  EXPECT_FALSE(obj_sub < TLongUser);
  EXPECT_FALSE(obj_sub < int_sub);
  EXPECT_FALSE(obj_sub < int_obj_sub);
  EXPECT_FALSE(obj_sub < int_sub_obj_sub);

  EXPECT_TRUE(int_sub < TObject);
  EXPECT_TRUE(int_sub < TLong);
  EXPECT_TRUE(int_sub < TLongUser);
  EXPECT_FALSE(int_sub < obj_sub);
  EXPECT_FALSE(int_sub < int_sub_exact);
  EXPECT_FALSE(int_sub < int_obj_sub);
  EXPECT_FALSE(int_sub < int_sub_obj_sub);

  EXPECT_TRUE(int_sub_exact < TObject);
  EXPECT_TRUE(int_sub_exact < TLong);
  EXPECT_TRUE(int_sub_exact < TLongUser);
  EXPECT_FALSE(int_sub_exact < obj_sub);
  EXPECT_TRUE(int_sub_exact < int_sub);
  EXPECT_FALSE(int_sub_exact < int_obj_sub);
  EXPECT_FALSE(int_sub_exact < int_sub_obj_sub);

  EXPECT_TRUE(int_obj_sub < TObject);
  EXPECT_TRUE(int_obj_sub < TLong);
  EXPECT_TRUE(int_obj_sub < TLongUser);
  EXPECT_TRUE(int_obj_sub < obj_sub);
  EXPECT_FALSE(int_obj_sub < int_sub);
  EXPECT_FALSE(int_obj_sub < int_sub_exact);
  EXPECT_FALSE(int_obj_sub < int_sub_obj_sub);

  EXPECT_TRUE(int_sub_obj_sub < TObject);
  EXPECT_TRUE(int_sub_obj_sub < TLong);
  EXPECT_TRUE(int_sub_obj_sub < TLongUser);
  EXPECT_TRUE(int_sub_obj_sub < obj_sub);
  EXPECT_TRUE(int_sub_obj_sub < int_sub);
  EXPECT_FALSE(int_sub_obj_sub < int_sub_exact);
  EXPECT_FALSE(int_sub_obj_sub < int_obj_sub);
  EXPECT_FALSE(int_sub_obj_sub < int_sub_obj_sub2);

  EXPECT_EQ(int_sub & obj_sub, int_sub);
  EXPECT_EQ(obj_sub & int_sub, int_sub);
  EXPECT_TRUE(int_sub_obj_sub < (int_sub & obj_sub));

  EXPECT_TRUE(int_sub_obj_sub2 < TObject);
  EXPECT_TRUE(int_sub_obj_sub2 < TLong);
  EXPECT_TRUE(int_sub_obj_sub2 < TLongUser);
  EXPECT_TRUE(int_sub_obj_sub2 < obj_sub);
  EXPECT_TRUE(int_sub_obj_sub2 < int_sub);
  EXPECT_FALSE(int_sub_obj_sub2 < int_sub_exact);
  EXPECT_FALSE(int_sub_obj_sub2 < int_obj_sub);
  EXPECT_FALSE(int_sub_obj_sub2 < int_sub_obj_sub2);

  EXPECT_NE(int_sub_obj_sub, int_sub_obj_sub2);

  auto user_long_obj = obj_sub & TLong;
  EXPECT_EQ(user_long_obj.toString(), "LongUser[ObjectSub]");
  EXPECT_FALSE(int_sub < user_long_obj);
  EXPECT_TRUE(int_obj_sub < user_long_obj);
  EXPECT_TRUE(int_sub_obj_sub < user_long_obj);
  EXPECT_TRUE(int_sub_obj_sub2 < user_long_obj);
}

TEST_F(HIRTypeTest, ReflowSimpleTypes) {
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  auto b1 = func.cfg.AllocateBlock();
  auto b2 = func.cfg.AllocateBlock();
  auto b3 = func.cfg.AllocateBlock();

  auto v0 = func.env.AllocateRegister();
  auto v1 = func.env.AllocateRegister();
  auto v2 = func.env.AllocateRegister();
  // Types start as Top and are set appropriately by reflowTypes() later.
  ASSERT_EQ(v0->type(), TTop);
  ASSERT_EQ(v1->type(), TTop);
  ASSERT_EQ(v2->type(), TTop);

  b0->append<MakeDict>(v0, 0, FrameState{});
  b0->append<CondBranch>(v0, b1, b2);

  b1->append<Branch>(b3);

  b2->append<MakeList>(0, v1, FrameState{});
  b2->append<Branch>(b3);

  std::unordered_map<BasicBlock*, Register*> phi_inputs{{b1, v0}, {b2, v1}};
  b3->append<Phi>(v2, phi_inputs);
  b3->append<Return>(v2);

  ASSERT_TRUE(checkFunc(func, std::cerr));
  reflowTypes(func);

  EXPECT_EQ(v0->type(), TMortalDictExact);
  EXPECT_EQ(v1->type(), TMortalListExact);
  EXPECT_EQ(v2->type(), (TMortalDictExact | TMortalListExact));
}

TEST_F(HIRTypeTest, ReflowLoopTypes) {
  Function func;
  auto b0 = func.cfg.entry_block = func.cfg.AllocateBlock();
  auto b1 = func.cfg.AllocateBlock();
  auto b2 = func.cfg.AllocateBlock();

  auto v0 = func.env.AllocateRegister();
  auto v1 = func.env.AllocateRegister();
  auto v2 = func.env.AllocateRegister();

  b0->append<MakeTuple>(0, v0, FrameState{});
  b0->append<Branch>(b1);

  std::unordered_map<BasicBlock*, Register*> phi_inputs{{b0, v0}, {b1, v2}};
  b1->append<Phi>(v1, phi_inputs);
  b1->append<MakeDict>(v2, 0, FrameState{});
  b1->append<CondBranch>(v2, b1, b2);

  b2->append<Return>(v1);

  ASSERT_TRUE(checkFunc(func, std::cerr));
  reflowTypes(func);

  EXPECT_EQ(v0->type(), TMortalTupleExact);
  EXPECT_EQ(v1->type(), TMortalTupleExact | TMortalDictExact);
  EXPECT_EQ(v2->type(), TMortalDictExact);
}
