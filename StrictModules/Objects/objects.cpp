// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/objects.h"

#include "StrictModules/Objects/callable_wrapper.h"

namespace strictmod::objects {

//--------------------------Object Factory----------------------------
template <typename T, typename... Args>
std::shared_ptr<StrictType> makeType(Args&&... args) {
  auto type = std::make_shared<T>(std::forward<Args>(args)...);
  type->addMethods();
  return type;
}

typedef std::vector<std::shared_ptr<BaseStrictObject>> TObjectPtrVec;

static std::shared_ptr<StrictType> kObjectType(
    new StrictObjectType("object", nullptr, {}, nullptr));

static std::shared_ptr<StrictType> kTypeType(
    new StrictTypeType("type", nullptr, {kObjectType}, nullptr));

static std::shared_ptr<StrictType> kModuleType(
    new StrictModuleType("module", nullptr, {kObjectType}, kTypeType));

static std::shared_ptr<StrictType> kBuiltinFunctionOrMethodType(
    new StrictBuiltinFunctionOrMethodType(
        "builtin_function_or_method",
        nullptr,
        {kObjectType},
        kTypeType));

static std::shared_ptr<StrictType> kMethodDescrType(new StrictMethodDescrType(
    "method_descriptor",
    nullptr,
    {kObjectType},
    kTypeType));

static std::shared_ptr<StrictType> kGetSetDescriptorType(
    new StrictGetSetDescriptorType(
        "getset_descr",
        nullptr,
        {kObjectType},
        kTypeType));

static std::shared_ptr<StrictModuleObject> kBuiltinsModule =
    StrictModuleObject::makeStrictModule(kModuleType, "builtins");

bool initializeBuiltinsModuleDict();

bool bootstrapBuiltins() {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;

    kTypeType->setType(kTypeType);
    kObjectType->setType(kTypeType);

    kObjectType->setCreator(kBuiltinsModule);
    kObjectType->setModuleName(kBuiltinsModule->getModuleName());

    kTypeType->setCreator(kBuiltinsModule);
    kTypeType->setModuleName(kBuiltinsModule->getModuleName());

    kModuleType->setCreator(kBuiltinsModule);
    kModuleType->setModuleName(kBuiltinsModule->getModuleName());

    kBuiltinFunctionOrMethodType->setCreator(kBuiltinsModule);
    kBuiltinFunctionOrMethodType->setModuleName(
        kBuiltinsModule->getModuleName());

    kMethodDescrType->setCreator(kBuiltinsModule);
    kMethodDescrType->setModuleName(kBuiltinsModule->getModuleName());

    kGetSetDescriptorType->setCreator(kBuiltinsModule);
    kGetSetDescriptorType->setModuleName(kBuiltinsModule->getModuleName());

    kTypeType->addMethods();
    kObjectType->addMethods();
    kModuleType->addMethods();
    kBuiltinFunctionOrMethodType->addMethods();
    kMethodDescrType->addMethods();
    kGetSetDescriptorType->addMethods();
  }
  return initialized;
}

std::shared_ptr<StrictModuleObject> BuiltinsModule() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  [[maybe_unused]] static bool initDict = initializeBuiltinsModuleDict();
  return kBuiltinsModule;
}

std::shared_ptr<StrictType> ObjectType() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  return kObjectType;
}
std::shared_ptr<StrictType> TypeType() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  return kTypeType;
}
std::shared_ptr<StrictType> ModuleType() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  return kModuleType;
}
std::shared_ptr<StrictType> BuiltinFunctionOrMethodType() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  return kBuiltinFunctionOrMethodType;
}

std::shared_ptr<StrictType> MethodDescrType() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  return kMethodDescrType;
}

std::shared_ptr<StrictType> GetSetDescriptorType() {
  [[maybe_unused]] static bool init = bootstrapBuiltins();
  return kGetSetDescriptorType;
}

TObjectPtrVec objectTypeVec() {
  static TObjectPtrVec v{ObjectType()};
  return v;
}

std::shared_ptr<StrictType> FunctionType() {
  static std::shared_ptr<StrictType> t = makeType<StrictFuncType>(
      "function", kBuiltinsModule, objectTypeVec(), TypeType(), false);
  return t;
}

std::shared_ptr<StrictType> CodeObjectType() {
  static std::shared_ptr<StrictType> t = makeType<StrictCodeObjectType>(
      "code", kBuiltinsModule, objectTypeVec(), TypeType(), false);
  return t;
}

std::shared_ptr<StrictType> MethodType() {
  static std::shared_ptr<StrictType> t = makeType<StrictMethodType>(
      "method", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> ClassMethodType() {
  static std::shared_ptr<StrictType> t = makeType<StrictClassMethodType>(
      "classmethod", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> StaticMethodType() {
  static std::shared_ptr<StrictType> t = makeType<StrictStaticMethodType>(
      "staticmethod", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> PropertyType() {
  static std::shared_ptr<StrictType> t = makeType<StrictPropertyType>(
      "property", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> UnknownType() {
  static std::shared_ptr<StrictType> t = makeType<UnknownObjectType>(
      "<unknown>", kBuiltinsModule, TObjectPtrVec{}, TypeType());
  return t;
}

std::shared_ptr<StrictType> NoneType() {
  static std::shared_ptr<StrictType> t = makeType<NoneType_>(
      "NoneType", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> EllipsisType() {
  static std::shared_ptr<StrictType> t = makeType<StrictEllipsisType>(
      "ellipsis", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> IntType() {
  static std::shared_ptr<StrictType> t = makeType<StrictIntType>(
      "int", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> BoolType() {
  static std::shared_ptr<StrictType> t = makeType<StrictBoolType>(
      "bool", kBuiltinsModule, TObjectPtrVec{IntType()}, TypeType());
  return t;
}

std::shared_ptr<StrictType> FloatType() {
  static std::shared_ptr<StrictType> t = makeType<StrictFloatType>(
      "float", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> ListType() {
  static std::shared_ptr<StrictType> t = makeType<StrictListType>(
      "list", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> TupleType() {
  static std::shared_ptr<StrictType> t = makeType<StrictTupleType>(
      "tuple", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> SetType() {
  static std::shared_ptr<StrictType> t = makeType<StrictSetType>(
      "set", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> SliceType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "slice", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> RangeType() {
  static std::shared_ptr<StrictType> t = makeType<StrictRangeType>(
      "range", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> StrType() {
  static std::shared_ptr<StrictType> t = makeType<StrictStringType>(
      "str", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> BytesType() {
  static std::shared_ptr<StrictType> t = makeType<StrictBytesType>(
      "bytes", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> ByteArrayType() {
  static std::shared_ptr<StrictType> t = makeType<StrictByteArrayType>(
      "bytearray", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> FrozensetType() {
  static std::shared_ptr<StrictType> t = makeType<StrictFrozenSetType>(
      "frozenset", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> DictObjectType() {
  static std::shared_ptr<StrictType> t = makeType<StrictDictType>(
      "dict", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> DictViewType() {
  static std::shared_ptr<StrictType> t = makeType<StrictDictViewType>(
      "dict_view", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> SequenceIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictSequenceIteratorType>(
      "sequence_iter", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> ReverseSequenceIteratorType() {
  static std::shared_ptr<StrictType> t =
      makeType<StrictReverseSequenceIteratorType>(
          "reverse_sequence_iter",
          kBuiltinsModule,
          objectTypeVec(),
          TypeType());
  return t;
}
std::shared_ptr<StrictType> VectorIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictVectorIteratorType>(
      "vector_iter", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> RangeIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictRangeIteratorType>(
      "range_iter", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> ZipIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictZipIteratorType>(
      "zip_iter", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> MapIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictMapIteratorType>(
      "map_iter", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> GeneratorExpType() {
  static std::shared_ptr<StrictType> t = makeType<StrictGeneratorExpType>(
      "generator", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> SetIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictSetIteratorType>(
      "set_iter", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}
std::shared_ptr<StrictType> CallableIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictCallableIteratorType>(
      "call_iterator", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> GenericObjectIteratorType() {
  static std::shared_ptr<StrictType> t =
      makeType<StrictGenericObjectIteratorType>(
          "obj_iterator", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> GeneratorFuncIteratorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictGeneratorFunctionType>(
      "generator_function", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> SuperType() {
  static std::shared_ptr<StrictType> t = makeType<StrictSuperType>(
      "super", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> UnionType() {
  static std::shared_ptr<StrictType> t = makeType<StrictUnionType>(
      "union", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> NotImplementedType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "NotImplementedType", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> ExceptionType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "Exception", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> TypeErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "TypeError", kBuiltinsModule, TObjectPtrVec{ExceptionType()}, TypeType());
  return t;
}

std::shared_ptr<StrictType> AttributeErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "AttributeError",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

std::shared_ptr<StrictType> ValueErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "ValueError",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

std::shared_ptr<StrictType> NameErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "NameError", kBuiltinsModule, TObjectPtrVec{ExceptionType()}, TypeType());
  return t;
}

std::shared_ptr<StrictType> StopIterationType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "StopIteration",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

std::shared_ptr<StrictType> KeyErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "KeyError", kBuiltinsModule, TObjectPtrVec{ExceptionType()}, TypeType());
  return t;
}

std::shared_ptr<StrictType> RuntimeErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "RuntimeError",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

std::shared_ptr<StrictType> DivisionByZeroType() {
  static std::shared_ptr<StrictType> t = makeType<StrictExceptionType>(
      "ZeroDivisionError",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

//--------------------Builtin Constant Declarations-----------------------

std::shared_ptr<BaseStrictObject> NoneObject() {
  static std::shared_ptr<BaseStrictObject> o(
      new NoneObject_(NoneType(), kBuiltinsModule));
  return o;
}

std::shared_ptr<BaseStrictObject> NotImplemented() {
  static std::shared_ptr<BaseStrictObject> o(
      new NotImplementedObject(NotImplementedType(), kBuiltinsModule));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictTrue() {
  static std::shared_ptr<BaseStrictObject> o(
      new StrictBool(BoolType(), kBuiltinsModule, true));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictFalse() {
  static std::shared_ptr<BaseStrictObject> o(
      new StrictBool(BoolType(), kBuiltinsModule, false));
  return o;
}

std::shared_ptr<BaseStrictObject> EllipsisObject() {
  static std::shared_ptr<BaseStrictObject> o(
      new StrictEllipsisObject(EllipsisType(), kBuiltinsModule));
  return o;
}

//--------------------Builtin Function Declarations-----------------------
std::shared_ptr<BaseStrictObject> StrictRepr() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(reprImpl, "repr"), nullptr, "repr"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictIsinstance() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(isinstanceImpl, "isinstance"),
      nullptr,
      "isinstance"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictIssubclass() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(issubclassImpl, "issubclass"),
      nullptr,
      "issubclass"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictLen() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(lenImpl, "len"), nullptr, "len"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictExec() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, execImpl, nullptr, "exec"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictIter() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(iterImpl, "iter", std::shared_ptr<BaseStrictObject>()),
      nullptr,
      "iter"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictNext() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(nextImpl, "next", std::shared_ptr<BaseStrictObject>()),
      nullptr,
      "next"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictReversed() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(reversedImpl, "reversed"),
      nullptr,
      "reversed"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictEnumerate() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(enumerateImpl, "enumerate"),
      nullptr,
      "enumerate"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictZip() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, StarCallableWrapper(zipImpl, "zip"), nullptr, "zip"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictMap() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, StarCallableWrapper(mapImpl, "map"), nullptr, "map"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictHash() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(hashImpl, "hash"), nullptr, "hash"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictAbs() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(absImpl, "abs"), nullptr, "abs"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictRound() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(roundImpl, "round"), nullptr, "round"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictDivmod() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(divmodImpl, "divmod"),
      nullptr,
      "divmod"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictChr() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(chrImpl, "chr"), nullptr, "chr"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictOrd() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(ordImpl, "ord"), nullptr, "ord"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictGetattr() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(
          getattrImpl, "getattr", std::shared_ptr<BaseStrictObject>()),
      nullptr,
      "getattr"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictSetattr() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(setattrImpl, "setattr"),
      nullptr,
      "setattr"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictHasattr() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(hasattrImpl, "hasattr"),
      nullptr,
      "hasattr"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictIsCallable() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule,
      CallableWrapper(isCallableImpl, "callable"),
      nullptr,
      "callable"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictPrint() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, printImpl, nullptr, "print"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictMax() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, StarCallableWrapper(maxImpl, "max"), nullptr, "max"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictMin() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, StarCallableWrapper(minImpl, "min"), nullptr, "min"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictAny() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(anyImpl, "any"), nullptr, "any"));
  return o;
}

std::shared_ptr<BaseStrictObject> StrictAll() {
  static std::shared_ptr<BaseStrictObject> o(new StrictBuiltinFunctionOrMethod(
      kBuiltinsModule, CallableWrapper(allImpl, "all"), nullptr, "all"));
  return o;
}

bool initializeBuiltinsModuleDict() {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    DictType builtinsDict({
        {"object", ObjectType()},
        {"type", TypeType()},
        {"int", IntType()},
        {"bool", BoolType()},
        {"float", FloatType()},
        {"str", StrType()},
        {"bytes", BytesType()},
        {"bytearray", ByteArrayType()},
        {"list", ListType()},
        {"tuple", TupleType()},
        {"set", SetType()},
        {"frozenset", SetType()},
        {"range", RangeType()},
        {"dict", DictObjectType()},
        {"super", SuperType()},
        {"classmethod", ClassMethodType()},
        {"staticmethod", StaticMethodType()},
        {"property", PropertyType()},
        {"Exception", ExceptionType()},
        {"TypeError", TypeErrorType()},
        {"AttributeError", AttributeErrorType()},
        {"ValueError", ValueErrorType()},
        {"NameError", NameErrorType()},
        {"StopIteration", StopIterationType()},
        {"KeyError", KeyErrorType()},
        {"RuntimeError", RuntimeErrorType()},
        {"ZeroDivisionError", DivisionByZeroType()},
        {"None", NoneObject()},
        {"NotImplemented", NotImplemented()},
        {"True", StrictTrue()},
        {"False", StrictFalse()},
        {"repr", StrictRepr()},
        {"issubclass", StrictIssubclass()},
        {"isinstance", StrictIsinstance()},
        {"len", StrictLen()},
        {"exec", StrictExec()},
        {"iter", StrictIter()},
        {"next", StrictNext()},
        {"reversed", StrictReversed()},
        {"enumerate", StrictEnumerate()},
        {"zip", StrictZip()},
        {"map", StrictMap()},
        {"hash", StrictHash()},
        {"abs", StrictAbs()},
        {"round", StrictRound()},
        {"divmod", StrictDivmod()},
        {"chr", StrictChr()},
        {"ord", StrictOrd()},
        {"getattr", StrictGetattr()},
        {"setattr", StrictSetattr()},
        {"hasattr", StrictHasattr()},
        {"callable", StrictIsCallable()},
        {"print", StrictPrint()},
        {"max", StrictMax()},
        {"min", StrictMin()},
        {"any", StrictAny()},
        {"all", StrictAll()},
    });
    kBuiltinsModule->getDict().insert(builtinsDict.begin(), builtinsDict.end());
  }
  return initialized;
}

std::shared_ptr<StrictType> getExceptionFromString(
    const std::string& excName,
    std::shared_ptr<StrictType> def) {
  static std::unordered_map<std::string, std::shared_ptr<StrictType>> dict({
      {"Exception", ExceptionType()},
      {"TypeError", TypeErrorType()},
      {"AttributeError", AttributeErrorType()},
      {"ValueError", ValueErrorType()},
      {"NameError", NameErrorType()},
      {"StopIteration", StopIterationType()},
      {"KeyError", KeyErrorType()},
      {"RuntimeError", RuntimeErrorType()},
      {"ZeroDivisionError", DivisionByZeroType()},
  });
  auto it = dict.find(excName);
  if (it == dict.end()) {
    return def;
  }
  return it->second;
}
} // namespace strictmod::objects
