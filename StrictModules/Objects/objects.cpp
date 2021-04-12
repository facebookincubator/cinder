#include "StrictModules/Objects/objects.h"

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


    kTypeType->addMethods();
    kObjectType->addMethods();
    kModuleType->addMethods();
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

TObjectPtrVec objectTypeVec() {
  static TObjectPtrVec v{ObjectType()};
  return v;
}

std::shared_ptr<StrictType> BuiltinFunctionOrMethodType() {
  static std::shared_ptr<StrictType> t =
      makeType<StrictBuiltinFunctionOrMethodType>(
          "builtin_function_or_method",
          kBuiltinsModule,
          objectTypeVec(),
          TypeType());
  return t;
}

std::shared_ptr<StrictType> MethodDescrType() {
  static std::shared_ptr<StrictType> t = makeType<StrictMethodDescrType>(
      "method_descriptor", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> MethodType() {
  static std::shared_ptr<StrictType> t = makeType<StrictMethodType>(
      "method", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> UnknownType() {
  static std::shared_ptr<StrictType> t = makeType<UnknownObjectType>(
      "<unknown>", kBuiltinsModule, TObjectPtrVec{}, TypeType());
  return t;
}

std::shared_ptr<StrictType> NoneType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "NoneType", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> IntType() {
  static std::shared_ptr<StrictType> t = makeType<StrictIntType>(
      "int", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> BoolType() {
  static std::shared_ptr<StrictType> t = makeType<StrictBoolType>(
      "bool", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

// std::shared_ptr<StrictType> StrType =
//     makeType<StrictStringType>("str", BuiltinsModule, objectTypeVec,
//     TypeType);

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

std::shared_ptr<StrictType> StrType() {
  static std::shared_ptr<StrictType> t = makeType<StrictStringType>(
      "str", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> FrozensetType() {
  static std::shared_ptr<StrictType> t = makeType<StrictFrozenSetType>(
      "frozenset", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> NotImplementedType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "NotImplementedType", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> ExceptionType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "Exception", kBuiltinsModule, objectTypeVec(), TypeType());
  return t;
}

std::shared_ptr<StrictType> TypeErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "TypeError", kBuiltinsModule, TObjectPtrVec{ExceptionType()}, TypeType());
  return t;
}

std::shared_ptr<StrictType> AttributeErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "AttributeError",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

std::shared_ptr<StrictType> ValueErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "ValueError",
      kBuiltinsModule,
      TObjectPtrVec{ExceptionType()},
      TypeType());
  return t;
}

std::shared_ptr<StrictType> NameErrorType() {
  static std::shared_ptr<StrictType> t = makeType<StrictObjectType>(
      "NameError",
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

bool initializeBuiltinsModuleDict() {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    DictType builtinsDict({
        {"object", ObjectType()},
        {"type", TypeType()},
        {"int", IntType()},
        {"bool", BoolType()},
        {"str", StrType()},
        {"list", ListType()},
        {"tuple", TupleType()},
        {"set", SetType()},
        {"Exception", ExceptionType()},
        {"TypeError", TypeErrorType()},
        {"AttributeError", AttributeErrorType()},
        {"ValueError", ValueErrorType()},
        {"None", NoneObject()},
        {"NotImplemented", NotImplemented()},
        {"True", StrictTrue()},
        {"False", StrictFalse()},
    });
    kBuiltinsModule->getDict().insert(builtinsDict.begin(), builtinsDict.end());
  }
  return initialized;
}
} // namespace strictmod::objects
