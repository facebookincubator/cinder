// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/function.h"

#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

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
      modName_(std::move(modName)),
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
          kwDefaults_) {}

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

std::string StrictFunction::getDisplayName() const {
  return qualName_;
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
        kwDefaultsDict[caller.makeStr(self->kwonlyArgs_[i])] =
            self->kwDefaults_[i];
      }
      self->kwDefaultsObj_ = std::make_shared<StrictDict>(
          DictObjectType(), caller.caller, std::move(kwDefaultsDict));
    }
  }
  return self->kwDefaultsObj_;
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
    // TODO create async call
    throw std::runtime_error("unsupported async func");
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
  } catch (const StrictModuleUserException<BaseStrictObject>&) {
    // user exceptions should be propagated
    throw;
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

void StrictFuncType::addMethods() {
  addGetSetDescriptor("__dict__", getDunderDictAllowed, setDunderDict, nullptr);
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
}

} // namespace strictmod::objects
