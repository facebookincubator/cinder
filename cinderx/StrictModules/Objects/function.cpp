// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/function.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"

namespace strictmod::objects {
FunctionReturnException::FunctionReturnException(
    std::shared_ptr<BaseStrictObject> val)
    : val_(std::move(val)) {}

StrictFunction::StrictFunction(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::string funcName,
    std::string qualName,
    int lineno,
    int col,
    std::vector<stmt_ty> body,
    EnvT closure,
    SymtableEntry symbols,
    std::vector<std::string> posonlyArgs,
    std::vector<std::string> posArgs,
    std::vector<std::string> kwonlyArgs,
    std::optional<std::string> varArg,
    std::optional<std::string> kwVarArg,
    std::vector<std::shared_ptr<BaseStrictObject>> posDefaults,
    std::vector<std::shared_ptr<BaseStrictObject>> kwDefaults,
    compiler::ModuleLoader* loader,
    std::string fileName,
    std::string modName,
    std::optional<std::string> doc,
    std::shared_ptr<BaseStrictObject> annotations,
    bool useFutureAnnotations,
    bool isCoroutine)
    : StrictInstance(std::move(type), std::move(creator)),
      funcName_(std::move(funcName)),
      qualName_(std::move(qualName)),
      lineno_(lineno),
      col_(col),
      body_(std::move(body)),
      closure_(std::move(closure)),
      symbols_(std::move(symbols)),
      posonlyArgs_(std::move(posonlyArgs)),
      posArgs_(std::move(posArgs)),
      kwonlyArgs_(std::move(kwonlyArgs)),
      varArg_(std::move(varArg)),
      kwVarArg_(std::move(kwVarArg)),
      posDefaults_(std::move(posDefaults)),
      kwDefaults_(std::move(kwDefaults)),
      loader_(loader),
      fileName_(std::move(fileName)),
      modName_(modName),
      doc_(std::move(doc)),
      modNameField_(std::move(modName)),
      annotations_(std::move(annotations)),
      useFutureAnnotations_(useFutureAnnotations),
      isCoroutine_(isCoroutine),
      signature_(
          funcName_,
          posonlyArgs_,
          posArgs_,
          kwonlyArgs_,
          varArg_,
          kwVarArg_,
          posDefaults_,
          kwDefaults_),
      codeObj_() {}

Analyzer StrictFunction::getFuncAnalyzer(
    const CallerContext& caller,
    BaseErrorSink* errorSink) {
  return Analyzer(
      loader_,
      errorSink,
      fileName_,
      modName_,
      qualName_, // scope name
      caller.caller,
      lineno_,
      col_,
      closure_,
      useFutureAnnotations_);
}

void StrictFunction::cleanContent(const StrictModuleObject* owner) {
  if ((!creator_.expired() && owner != creator_.lock().get())) {
    return;
  }
  closure_.clear();
  StrictInstance::cleanContent(owner);
}

std::string StrictFunction::getDisplayName() const {
  return qualName_;
}

std::shared_ptr<BaseStrictObject> StrictFunction::copy(const CallerContext&) {
  // similar to python copy.deepcopy, functions and types are
  // not actually copied
  return shared_from_this();
}

// wrapped methods
std::shared_ptr<BaseStrictObject> StrictFunction::function__annotations__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto self = assertStaticCast<StrictFunction>(std::move(inst));
  return self->annotations_;
}

void StrictFunction::function__annotations__setter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  checkExternalModification(inst, caller);
  auto self = assertStaticCast<StrictFunction>(std::move(inst));
  auto newDict = std::dynamic_pointer_cast<StrictDict>(value);
  if (newDict == nullptr) {
    caller.raiseTypeError(
        "{}.__annotations__ must be assigned to dict, not {}",
        self->funcName_,
        value->getTypeRef().getName());
  }
  self->annotations_ = std::move(newDict);
}

std::shared_ptr<BaseStrictObject> StrictFunction::function__defaults__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictFunction>(std::move(inst));
  if (self->posDefaults_.empty()) {
    return NoneObject();
  }
  return std::make_shared<StrictTuple>(
      TupleType(), caller.caller, self->posDefaults_);
}

void StrictFunction::function__defaults__setter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  checkExternalModification(inst, caller);
  auto self = assertStaticCast<StrictFunction>(std::move(inst));
  auto newDefaults = std::dynamic_pointer_cast<StrictTuple>(value);
  if (newDefaults == nullptr) {
    caller.raiseTypeError(
        "{}.__defaults__ must be assigned to tuple, not {}",
        self->funcName_,
        value->getTypeRef().getName());
  }

  const auto& newDefaultVec = newDefaults->getData();
  self->posDefaults_.clear();
  self->posDefaults_.insert(
      self->posDefaults_.end(), newDefaultVec.begin(), newDefaultVec.end());
}

std::shared_ptr<BaseStrictObject> StrictFunction::function__kwdefaults__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictFunction>(std::move(inst));
  if (self->kwDefaultsObj_ == nullptr) {
    if (self->kwDefaults_.empty()) {
      self->kwDefaultsObj_ = NoneObject();
    } else {
      DictDataT kwDefaultsDict;
      for (std::size_t i = 0; i < self->kwDefaults_.size(); ++i) {
        if (self->kwDefaults_[i]) {
          kwDefaultsDict[caller.makeStr(self->kwonlyArgs_[i])] =
              self->kwDefaults_[i];
        }
      }
      if (kwDefaultsDict.empty()) {
        self->kwDefaultsObj_ = NoneObject();
      } else {
        self->kwDefaultsObj_ = std::make_shared<StrictDict>(
            DictObjectType(), caller.caller, std::move(kwDefaultsDict));
      }
    }
  }
  return self->kwDefaultsObj_;
}

std::shared_ptr<BaseStrictObject> StrictFunction::function__code__getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto self = assertStaticCast<StrictFunction>(std::move(inst));
  if (self->codeObj_ == nullptr) {
    self->makeCodeObjHelper(caller);
  }
  return self->codeObj_;
}

void StrictFunction::makeCodeObjHelper(const CallerContext&) {
  int posOnlyArgCount = posonlyArgs_.size();
  auto posOnlyArgCountInt =
      std::make_shared<StrictInt>(IntType(), creator_, posOnlyArgCount);
  int argCount = posArgs_.size() + posOnlyArgCount;
  auto argCountInt = std::make_shared<StrictInt>(IntType(), creator_, argCount);

  std::vector<PyObject*> varnames = symbols_.getFunctionVarNames();
  std::vector<std::shared_ptr<BaseStrictObject>> varnamesVec;

  for (PyObject* name : varnames) {
    auto nameStr = std::make_shared<StrictString>(
        StrType(), creator_, Ref<>::create(name));
    varnamesVec.push_back(std::move(nameStr));
  }
  auto varnamesTuple = std::make_shared<StrictTuple>(
      TupleType(), creator_, std::move(varnamesVec));

  int kwOnlyArgCount = kwonlyArgs_.size();
  auto kwOnlyArgCountInt =
      std::make_shared<StrictInt>(IntType(), creator_, kwOnlyArgCount);

  auto funcNameStr =
      std::make_shared<StrictString>(StrType(), creator_, funcName_);

  int flags = symbols_.getFunctionCodeFlag();
  auto flagsInt = std::make_shared<StrictInt>(IntType(), creator_, flags);
  codeObj_ = std::make_shared<StrictCodeObject>(
      creator_,
      std::move(funcNameStr),
      std::move(argCountInt),
      std::move(posOnlyArgCountInt),
      std::move(kwOnlyArgCountInt),
      std::move(flagsInt),
      std::move(varnamesTuple));
}

// Function Type
std::shared_ptr<BaseStrictObject> StrictFuncType::getDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  if (inst == nullptr) {
    return obj;
  }
  return std::make_shared<StrictMethod>(
      caller.caller, std::move(obj), std::move(inst));
}

std::shared_ptr<BaseStrictObject> StrictFuncType::call(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller) {
  std::shared_ptr<StrictFunction> func =
      assertStaticCast<StrictFunction>(std::move(obj));
  if (func->isCoroutine()) {
    return std::make_shared<StrictAsyncCall>(
        caller.caller, func->getFuncName());
  }

  std::unique_ptr<BaseErrorSink> errorSink = caller.errorSink->getNestedSink();

  Analyzer analyzer = func->getFuncAnalyzer(caller, errorSink.get());

  FuncSignature funcSig = func->getSignature();
  std::unique_ptr<DictType> callArgs = funcSig.bind(args, argNames, caller);
  std::shared_ptr<BaseStrictObject> firstArg;
  if (args.size() > argNames.size()) {
    firstArg = args[0];
  }

  try {
    analyzer.analyzeFunction(
        func->getBody(),
        func->getSymtableEntry(),
        std::move(callArgs),
        std::move(firstArg));
  } catch (FunctionReturnException& ret) {
    return ret.getVal();
  } catch (const YieldReachedException&) {
    // calling a coroutine function return a generator function object
    return std::make_shared<StrictGeneratorFunction>(
        GeneratorFuncIteratorType(), caller.caller, func);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    // user exceptions should be propagated
    auto propagated = StrictModuleUserException<BaseStrictObject>(e);
    propagated.setlineInfo(caller.lineno, caller.col);
    propagated.setFilename(caller.filename);
    propagated.setScopeName(caller.scopeName);
    propagated.setCause(e.clone());
    throw propagated;
  } catch (const StrictModuleException& exc) {
    // function call is unsafe
    caller.error<UnsafeCallException>(
        std::shared_ptr<const StrictModuleException>(exc.clone()),
        func->getFuncName());
    return makeUnknown(
        caller, "{}({})", func->getFuncName(), formatArgs(args, argNames));
  }
  return NoneObject();
}

std::shared_ptr<StrictType> StrictFuncType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictFuncType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictFuncType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictFuncType));
  return baseVec;
}

bool StrictFuncType::isCallable(const CallerContext&) {
  return true;
}

void StrictFuncType::addMethods() {
  addGetSetDescriptor(
      kDunderDict, getDunderDictAllowed, setDunderDict, nullptr);
  addGetSetDescriptor(
      kDunderAnnotations,
      StrictFunction::function__annotations__getter,
      StrictFunction::function__annotations__setter,
      nullptr);
  addGetSetDescriptor(
      "__defaults__",
      StrictFunction::function__defaults__getter,
      StrictFunction::function__defaults__setter,
      nullptr);
  addGetSetDescriptor(
      "__kwdefaults__",
      StrictFunction::function__kwdefaults__getter,
      nullptr,
      nullptr);
  addGetSetDescriptor(
      "__code__", StrictFunction::function__code__getter, nullptr, nullptr);
  addStringMemberDescriptor<StrictFunction, &StrictFunction::funcName_>(
      "__name__");
  addStringMemberDescriptor<StrictFunction, &StrictFunction::qualName_>(
      "__qualname__");
  addStringOptionalMemberDescriptor<
      StrictFunction,
      &StrictFunction::modNameField_>("__module__");
  addStringOptionalMemberDescriptor<StrictFunction, &StrictFunction::doc_>(
      "__doc__");
}

// async call

StrictAsyncCall::StrictAsyncCall(
    std::weak_ptr<StrictModuleObject> creator,
    std::string funcName)
    : StrictInstance(AsyncCallType(), std::move(creator)),
      funcName_(std::move(funcName)) {}

std::string StrictAsyncCall::getDisplayName() const {
  return funcName_;
}

// wrapped method
std::shared_ptr<BaseStrictObject> StrictAsyncCall::asyncCallClose(
    std::shared_ptr<StrictAsyncCall>,
    const CallerContext&) {
  return NoneObject();
}

std::shared_ptr<StrictType> StrictAsyncCallType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictAsyncCallType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

bool StrictAsyncCallType::isBaseType() const {
  return false;
}

std::vector<std::type_index> StrictAsyncCallType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictAsyncCallType));
  return baseVec;
}

void StrictAsyncCallType::addMethods() {
  addMethod("close", StrictAsyncCall::asyncCallClose);
}

} // namespace strictmod::objects
