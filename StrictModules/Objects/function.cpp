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
    const EnvT& closure,
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
    std::shared_ptr<BaseStrictObject> annotations,
    bool useFutureAnnotations,
    bool isCoroutine)
    : StrictInstance(std::move(type), std::move(creator)),
      funcName_(std::move(funcName)),
      qualName_(std::move(qualName)),
      lineno_(lineno),
      col_(col),
      body_(std::move(body)),
      closure_(EnvT(closure)),
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

  try {
    analyzer.analyzeFunction(
        func->getBody(), func->getSymtableEntry(), std::move(callArgs));
  } catch (FunctionReturnException& ret) {
    return ret.getVal();
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

} // namespace strictmod::objects
