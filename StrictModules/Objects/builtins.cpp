// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/builtins.h"

#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"

#include "StrictModules/Compiler/abstract_module_loader.h"
#include "StrictModules/Compiler/module_info.h"
namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> reprImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value) {
  auto reprFunc = iLoadAttrOnType(
      value,
      kDunderRepr,
      makeUnknown(caller, "{}.__repr__", value->getTypeRef().getName()),
      caller);
  return iCall(std::move(reprFunc), kEmptyArgs, kEmptyArgNames, caller);
}

std::shared_ptr<BaseStrictObject> recursiveIsinstanceHelper(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> clsInfo,
    const CallerContext& caller) {
  auto clsType = std::dynamic_pointer_cast<StrictType>(clsInfo);
  if (!clsType) {
    caller.raiseTypeError(
        "isinstance() arg 2 must be a type or tuple of types or union, not {} "
        "object",
        clsInfo->getTypeRef().getName());
  }
  // check mro using type(obj)
  if (obj->getTypeRef().isSubType(clsType)) {
    return StrictTrue();
  }
  // check mro using obj.__class__
  auto objClass = iLoadAttr(obj, kDunderClass, nullptr, caller);
  auto objClassType = std::dynamic_pointer_cast<StrictType>(objClass);
  if (objClassType && objClassType->isSubType(clsType)) {
    return StrictTrue();
  }
  return StrictFalse();
}

std::shared_ptr<BaseStrictObject> isinstanceImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> clsInfo) {
  // shortcut if type(obj) == clsInfo
  if (obj->getType() == clsInfo) {
    return StrictTrue();
  }
  // if clsInfo is a type, use the default instance check
  if (clsInfo->getType() == TypeType()) {
    return recursiveIsinstanceHelper(
        std::move(obj), std::move(clsInfo), caller);
  }
  // clsInfo is a tuple of candidates, check on each element
  auto clsTuple = std::dynamic_pointer_cast<StrictTuple>(clsInfo);
  if (clsTuple) {
    for (auto& cls : clsTuple->getData()) {
      if (isinstanceImpl(nullptr, caller, obj, cls) == StrictTrue()) {
        return StrictTrue();
      }
    }
    return StrictFalse();
  }
  // non-type clsInfo, use __instancecheck__ hook
  auto instanceCheckHook =
      iLoadAttr(clsInfo, "__instancecheck__", nullptr, caller);
  if (instanceCheckHook) {
    auto checkResult = iCall(instanceCheckHook, {obj}, kEmptyArgNames, caller);
    if (iGetTruthValue(checkResult, caller) == StrictTrue()) {
      return StrictTrue();
    }
    return StrictFalse();
  }
  // no __instancecheck__ hook, use default check
  return recursiveIsinstanceHelper(std::move(obj), std::move(clsInfo), caller);
}

bool issubclassBody(
    const CallerContext& caller,
    std::shared_ptr<StrictObjectType> cls,
    std::shared_ptr<BaseStrictObject> clsInfo) {
  // clsInfo is tuple
  auto clsTuple = std::dynamic_pointer_cast<StrictTuple>(clsInfo);
  if (clsTuple) {
    for (auto& c : clsTuple->getData()) {
      if (issubclassBody(caller, cls, c)) {
        return true;
      }
    }
    return false;
  }
  // use subclass hook, it should always exist since it's defined on type
  auto subclassCheckHook =
      iLoadAttr(clsInfo, "__subclasscheck__", nullptr, caller);
  if (subclassCheckHook) {
    auto checkResult = iCall(subclassCheckHook, {cls}, kEmptyArgNames, caller);
    if (iGetTruthValue(checkResult, caller) == StrictTrue()) {
      return true;
    }
    return false;
  }
  // error case
  caller.raiseTypeError(
      "issubclass() arg 2 must be a class, tuple of class or union, not {} "
      "object",
      clsInfo->getTypeRef().getName());
}

std::shared_ptr<BaseStrictObject> issubclassImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> clsInfo) {
  auto cls = std::dynamic_pointer_cast<StrictObjectType>(obj);
  if (!cls) {
    caller.raiseTypeError(
        "issubclass() arg 1 must be a class, not {} object",
        obj->getTypeRef().getName());
  }
  if (issubclassBody(caller, std::move(cls), std::move(clsInfo))) {
    return StrictTrue();
  }
  return StrictFalse();
}

std::shared_ptr<BaseStrictObject> lenImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  auto lenFunc = iLoadAttrOnType(arg, kDunderLen, nullptr, caller);
  if (lenFunc) {
    return iCall(std::move(lenFunc), kEmptyArgs, kEmptyArgNames, caller);
  }
  caller.raiseTypeError(
      "object of type '{}' has no len()", arg->getTypeRef().getName());
}

/** -------------------exec() implementation-------------------- */

void execArgHelper(
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller,
    std::string& codeOut,
    std::shared_ptr<StrictDict>& globalsOut,
    std::shared_ptr<StrictDict>& localsOut) {
  if (!namedArgs.empty()) {
    caller.raiseTypeError("keyword arguments on exec() is not supported");
  }
  if (args.size() < 1 || args.size() > 3) {
    caller.raiseTypeError(
        "exec() expects 1 to 3 arguments but got {}", args.size());
  }
  auto arg0 = args[0];
  auto arg0Str = std::dynamic_pointer_cast<StrictString>(arg0);
  if (arg0Str) {
    codeOut = arg0Str->getValue();
  } else {
    caller.raiseTypeError(
        "exec() first argument should be str, got {}",
        arg0->getTypeRef().getName());
  }
  if (args.size() < 2) {
    caller.raiseTypeError("calling exec() without globals is not supported");
  }
  auto arg1 = args[1];
  auto arg1Dict = std::dynamic_pointer_cast<StrictDict>(arg1);
  if (arg1Dict) {
    globalsOut = arg1Dict;
  } else {
    caller.raiseTypeError(
        "exec() second argument should be dict, got {}",
        arg1->getTypeRef().getName());
  }
  if (args.size() > 2) {
    auto arg2 = args[2];
    auto arg2Dict = std::dynamic_pointer_cast<StrictDict>(arg2);
    if (arg2Dict) {
      localsOut = std::move(arg2Dict);
    } else {
      caller.raiseTypeError(
          "exec() third argument should be dict, got {}",
          arg2->getTypeRef().getName());
    }
  } else {
    localsOut = arg1Dict;
  }
}

std::shared_ptr<BaseStrictObject> execImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  std::string code;
  std::shared_ptr<StrictDict> globals;
  std::shared_ptr<StrictDict> locals;
  execArgHelper(args, namedArgs, caller, code, globals, locals);
  if (caller.loader == nullptr) {
    caller.raiseTypeError("cannot call exec() from inside exec()");
  }
  std::unique_ptr<compiler::ModuleInfo> modinfo =
      caller.loader->findModuleFromSource(code, "<exec>", "<exec>");
  Symtable table(modinfo->passSymtable());
  Analyzer analyzer(
      modinfo->getAst(),
      nullptr,
      std::move(table),
      caller.errorSink,
      "<exec>",
      "<exec>",
      "",
      caller.caller);
  analyzer.analyzeExec(
      caller.lineno, caller.col, std::move(globals), std::move(locals));
  return NoneObject();
}
} // namespace strictmod::objects
