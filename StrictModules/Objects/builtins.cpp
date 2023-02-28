// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/builtins.h"

#include "StrictModules/Compiler/abstract_module_loader.h"
#include "StrictModules/Compiler/module_info.h"
#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"
#include "StrictModules/sequence_map.h"

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
      iLoadAttrOnType(clsInfo, "__subclasscheck__", nullptr, caller);
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
        "issubclass() arg 1 must be a class, not {} {} object",
        obj->getTypeRef().getName(),
        obj->getDisplayName());
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
  if (!arg->isUnknown()) {
    caller.raiseTypeError(
        "object of type '{}' has no len()", arg->getTypeRef().getName());
  }
  return makeUnknown(caller, "len({})", arg);
}

/** -------------------exec/eval() implementation-------------------- */

void execEvalArgHelper(
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const std::string& funcName,
    const CallerContext& caller,
    std::string& codeOut,
    std::shared_ptr<StrictDict>& globalsOut,
    std::shared_ptr<StrictDict>& localsOut) {
  if (!namedArgs.empty()) {
    caller.raiseTypeError(
        "keyword arguments on {}() is not supported", funcName);
  }
  if (args.size() < 1 || args.size() > 3) {
    caller.raiseTypeError(
        "{}() expects 1 to 3 arguments but got {}", funcName, args.size());
  }
  auto arg0 = args[0];
  auto arg0Str = std::dynamic_pointer_cast<StrictString>(arg0);
  if (arg0Str) {
    codeOut = arg0Str->getValue();
  } else {
    caller.raiseTypeError(
        "{}() first argument should be str (code object not supported), got {}",
        funcName,
        arg0->getTypeRef().getName());
  }
  if (args.size() < 2) {
    caller.raiseTypeError(
        "calling {}() without globals is not supported", funcName);
  }
  auto arg1 = args[1];
  auto arg1Dict = std::dynamic_pointer_cast<StrictDict>(arg1);
  if (arg1Dict) {
    globalsOut = arg1Dict;
  } else {
    caller.raiseTypeError(
        "{}() second argument should be dict, got {}",
        funcName,
        arg1->getTypeRef().getName());
  }
  if (args.size() > 2) {
    auto arg2 = args[2];
    auto arg2Dict = std::dynamic_pointer_cast<StrictDict>(arg2);
    if (arg2Dict) {
      localsOut = std::move(arg2Dict);
    } else {
      caller.raiseTypeError(
          "{}() third argument should be dict, got {}",
          funcName,
          arg2->getTypeRef().getName());
    }
  } else {
    localsOut = arg1Dict;
  }
}

std::shared_ptr<BaseStrictObject> execOrEvalImpl(
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    int mode,
    const CallerContext& caller) {
  std::string code;
  std::shared_ptr<StrictDict> globals;
  std::shared_ptr<StrictDict> locals;
  std::string funcName = "";
  std::string modName = "";
  if (mode == Py_file_input) {
    funcName = "exec";
    modName = "<exec>";
  } else if (mode == Py_eval_input) {
    funcName = "eval";
    modName = "<eval>";
  }
  execEvalArgHelper(args, namedArgs, funcName, caller, code, globals, locals);
  std::unique_ptr<compiler::ModuleInfo> modinfo =
      caller.loader->findModuleFromSource(code, modName, "<string>", mode);
  if (modinfo == nullptr) {
    caller.raiseCurrentPyException();
  }
  Symtable table(modinfo->getSymtable());
  Analyzer analyzer(
      modinfo->getAst(),
      caller.loader,
      std::move(table),
      caller.errorSink,
      "<string>",
      modName,
      "",
      caller.caller.lock());
  return analyzer.analyzeExecOrEval(
      caller.lineno, caller.col, std::move(globals), std::move(locals));
}

std::shared_ptr<BaseStrictObject> execImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  return execOrEvalImpl(args, namedArgs, Py_file_input, caller);
}

std::shared_ptr<BaseStrictObject> evalImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  return execOrEvalImpl(args, namedArgs, Py_eval_input, caller);
}

// --------------------end of exec/eval() implementation------------------

std::shared_ptr<BaseStrictObject> iterImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg,
    std::shared_ptr<BaseStrictObject> sentinel) {
  std::shared_ptr<BaseStrictObject> result;
  if (sentinel == nullptr) {
    auto iterFunc = iLoadAttrOnType(arg, kDunderIter, nullptr, caller);
    if (iterFunc) {
      result = iCall(std::move(iterFunc), kEmptyArgs, kEmptyArgNames, caller);
    }
  } else {
    // iter with sentinel has a completely different meaning:
    // arg should be called until the return value == sentinel
    // This is expressed using a call iterator
    result = std::make_shared<StrictCallableIterator>(
        CallableIteratorType(), caller.caller, arg, std::move(sentinel));
  }
  if (!result) {
    caller.raiseTypeError(
        "{} object is not iterable", arg->getTypeRef().getName());
  }
  return result;
}

std::shared_ptr<BaseStrictObject> nextImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterator,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  auto nextFunc = iLoadAttrOnType(iterator, kDunderNext, nullptr, caller);
  if (!nextFunc) {
    caller.raiseTypeError(
        "{} object is not an iterator", iterator->getTypeRef().getName());
  }
  try {
    return iCall(std::move(nextFunc), kEmptyArgs, kEmptyArgNames, caller);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    auto wrapped = e.getWrapped();
    if (defaultValue != nullptr &&
        (wrapped == StopIterationType() ||
         wrapped->getType() == StopIterationType())) {
      return defaultValue;
    }
    throw;
  }
}

std::shared_ptr<BaseStrictObject> reversedImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  std::shared_ptr<BaseStrictObject> result;
  auto iterFunc = iLoadAttrOnType(arg, "__reversed__", nullptr, caller);
  if (iterFunc) {
    return iCall(std::move(iterFunc), kEmptyArgs, kEmptyArgNames, caller);
  }

  else {
    caller.raiseTypeError(
        "{} object is not reversible", arg->getTypeRef().getName());
  }
  return result;
}

std::shared_ptr<BaseStrictObject> enumerateImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  auto elements = iGetElementsVec(arg, caller);
  std::vector<std::shared_ptr<BaseStrictObject>> resultVec;
  resultVec.reserve(elements.size());
  int idx = 0;
  for (auto& e : elements) {
    auto idxObj = caller.makeInt(idx++);
    resultVec.push_back(caller.makePair(std::move(idxObj), e));
  }
  return std::make_shared<StrictVectorIterator>(
      VectorIteratorType(), caller.caller, std::move(resultVec));
}

std::shared_ptr<BaseStrictObject> zipImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>>) {
  std::vector<std::shared_ptr<BaseStrictObject>> iterators;
  iterators.reserve(args.size());
  for (auto a : args) {
    auto it = iterImpl(nullptr, caller, std::move(a));
    iterators.push_back(std::move(it));
  }
  return std::make_shared<StrictZipIterator>(
      ZipIteratorType(), caller.caller, std::move(iterators));
}

std::shared_ptr<BaseStrictObject> mapImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>>,
    std::shared_ptr<BaseStrictObject> func) {
  std::vector<std::shared_ptr<BaseStrictObject>> iterators;
  iterators.reserve(args.size());
  for (auto a : args) {
    auto it = iterImpl(nullptr, caller, std::move(a));
    iterators.push_back(std::move(it));
  }
  return std::make_shared<StrictMapIterator>(
      MapIteratorType(), caller.caller, std::move(iterators), std::move(func));
}

std::shared_ptr<BaseStrictObject> hashImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  auto func = iLoadAttr(arg, "__hash__", nullptr, caller);
  if (!func) {
    caller.raiseTypeError(
        "{} object is not hashable", arg->getTypeRef().getName());
  }
  return iCall(std::move(func), kEmptyArgs, kEmptyArgNames, caller);
}

std::shared_ptr<BaseStrictObject> absImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  auto func = iLoadAttr(arg, "__abs__", nullptr, caller);
  if (!func) {
    caller.raiseTypeError(
        "bad operand type for abs(): {}", arg->getTypeRef().getName());
  }
  return iCall(std::move(func), kEmptyArgs, kEmptyArgNames, caller);
}

std::shared_ptr<BaseStrictObject> roundImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg) {
  auto func = iLoadAttr(arg, "__round__", nullptr, caller);
  if (!func) {
    caller.raiseTypeError(
        "bad operand type for abs(): {}", arg->getTypeRef().getName());
  }
  return iCall(std::move(func), kEmptyArgs, kEmptyArgNames, caller);
}

std::shared_ptr<BaseStrictObject> divmodImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs,
    std::shared_ptr<BaseStrictObject> rhs) {
  bool triedRight = false;
  auto lType = lhs->getType();
  auto rType = rhs->getType();
  if (lType != rType && rType->isSubType(lType)) {
    // do reverse op first
    auto rfunc = iLoadAttr(rhs, "__rdivmod__", nullptr, caller);
    if (rfunc) {
      auto result = iCall(std::move(rfunc), {lhs}, kEmptyArgNames, caller);
      if (result != NotImplemented()) {
        return result;
      }
    }
    triedRight = true;
  }

  auto func = iLoadAttr(lhs, "__divmod__", nullptr, caller);
  if (func) {
    auto result = iCall(std::move(func), {rhs}, kEmptyArgNames, caller);
    if (result != NotImplemented()) {
      return result;
    }
  }

  if (!triedRight) {
    auto rfunc = iLoadAttr(rhs, "__rdivmod__", nullptr, caller);
    if (rfunc) {
      auto result = iCall(std::move(rfunc), {lhs}, kEmptyArgNames, caller);
      if (result != NotImplemented()) {
        return result;
      }
    }
  }

  caller.raiseTypeError(
      "bad operand type for divmod(): {} and {}",
      lType->getName(),
      rType->getName());
}

std::shared_ptr<BaseStrictObject> chrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> i) {
  auto iInt = std::dynamic_pointer_cast<StrictInt>(i);
  if (!iInt) {
    caller.raiseTypeError(
        "bad operand type for chr(): {}", i->getTypeRef().getName());
  }
  auto v = iInt->getValue();
  if (!v.has_value() || v < 0 || v > 0x10ffff) {
    caller.raiseExceptionStr(
        ValueErrorType(), "chr arg {} not in range", iInt->getDisplayName());
  }
  Ref<> resPyObj = Ref<>::steal(PyUnicode_FromOrdinal(*v));
  return StrictString::strFromPyObj(std::move(resPyObj), caller);
}

std::shared_ptr<BaseStrictObject> ordImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> c) {
  auto cStr = std::dynamic_pointer_cast<StrictString>(c);
  if (!cStr) {
    caller.raiseTypeError(
        "bad operand type for ord(): {}", c->getTypeRef().getName());
  }
  const std::string& v = cStr->getValue();
  if (v.size() == 1) {
    long result = long(PyUnicode_READ_CHAR(cStr->getPyObject().get(), 0));
    return caller.makeInt(result);
  }
  caller.raiseTypeError(
      "ord() expects a character, but got string of size {}", v.size());
}

std::shared_ptr<BaseStrictObject> getattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name,
    std::shared_ptr<BaseStrictObject> defaultValue) {
  auto nameStr = std::dynamic_pointer_cast<StrictString>(name);
  if (!nameStr) {
    caller.raiseTypeError("getattr() attribute name must be string");
  }
  std::shared_ptr<BaseStrictObject> result;
  try {
    result =
        iLoadAttr(std::move(obj), nameStr->getValue(), defaultValue, caller);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    auto exc = e.getWrapped();
    if (exc == AttributeErrorType() || exc->getType() == AttributeErrorType()) {
      result = defaultValue;
    } else {
      throw;
    }
  }
  if (result == nullptr) {
    caller.raiseExceptionStr(AttributeErrorType(), "");
  }
  return result;
}

std::shared_ptr<BaseStrictObject> setattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name,
    std::shared_ptr<BaseStrictObject> value) {
  auto nameStr = std::dynamic_pointer_cast<StrictString>(name);
  if (!nameStr) {
    caller.raiseTypeError("setattr() attribute name must be string");
  }
  iStoreAttr(std::move(obj), nameStr->getValue(), std::move(value), caller);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> delattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name) {
  auto nameStr = std::dynamic_pointer_cast<StrictString>(name);
  if (!nameStr) {
    caller.raiseTypeError("delattr() attribute name must be string");
  }
  iDelAttr(std::move(obj), nameStr->getValue(), caller);
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> hasattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name) {
  auto nameStr = std::dynamic_pointer_cast<StrictString>(name);
  if (!nameStr) {
    caller.raiseTypeError("hasattr() attribute name must be string");
  }
  std::shared_ptr<BaseStrictObject> result;
  try {
    result = iLoadAttr(std::move(obj), nameStr->getValue(), nullptr, caller);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    auto exc = e.getWrapped();
    if (exc == AttributeErrorType() || exc->getType() == AttributeErrorType()) {
      return StrictFalse();
    } else {
      throw;
    }
  }
  if (result == nullptr) {
    return StrictFalse();
  }
  return StrictTrue();
}

std::shared_ptr<BaseStrictObject> isCallableImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj) {
  return caller.makeBool(obj->getTypeRef().isCallable(caller));
}

std::shared_ptr<BaseStrictObject> printImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext&) {
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> inputImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>&,
    const std::vector<std::string>&,
    const CallerContext& caller) {
  return caller.makeStr("");
}

static std::shared_ptr<BaseStrictObject> minmaxMultiArgHelper(
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> elements,
    std::shared_ptr<BaseStrictObject> keyFunc,
    std::shared_ptr<BaseStrictObject> defaultValue,
    cmpop_ty op) {
  std::shared_ptr<BaseStrictObject> maxVal;
  std::shared_ptr<BaseStrictObject> maxItem;

  for (auto e : elements) {
    std::shared_ptr<BaseStrictObject> current = e;
    if (keyFunc) {
      current = iCall(keyFunc, {std::move(current)}, kEmptyArgNames, caller);
    }
    if (!maxVal) {
      maxVal = current;
      maxItem = e;
    } else {
      auto cmpResult = iBinCmpOp(current, maxVal, op, caller);
      cmpResult = iGetTruthValue(std::move(cmpResult), caller);

      if (cmpResult == StrictTrue()) {
        maxVal = current;
        maxItem = e;
      } else if (cmpResult->isUnknown()) {
        return nullptr;
      }
    }
  }
  if (elements.empty()) {
    if (defaultValue) {
      return defaultValue;
    }
    caller.raiseExceptionStr(ValueErrorType(), "min/max got an empty sequence");
  }
  return maxItem;
}

static std::shared_ptr<BaseStrictObject> minmaxMultiArgHelper(
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> arg1,
    cmpop_ty op) {
  auto keyIt = kwargs.find("key");
  std::shared_ptr<BaseStrictObject> k;
  if (keyIt != kwargs.map_end()) {
    k = keyIt->second.first;
  }

  args.insert(args.begin(), std::move(arg1));
  return minmaxMultiArgHelper(
      caller, std::move(args), std::move(k), nullptr, op);
}

static std::shared_ptr<BaseStrictObject> minmaxSingleArgHelper(
    const CallerContext& caller,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> iterable,
    cmpop_ty op) {
  auto keyIt = kwargs.find("key");
  std::shared_ptr<BaseStrictObject> k;
  if (keyIt != kwargs.map_end()) {
    k = keyIt->second.first;
  }
  auto defaultIt = kwargs.find("default");
  std::shared_ptr<BaseStrictObject> d;
  if (defaultIt != kwargs.map_end()) {
    d = defaultIt->second.first;
  }

  auto elements = iGetElementsVec(std::move(iterable), caller);
  return minmaxMultiArgHelper(
      caller, std::move(elements), std::move(k), std::move(d), op);
}

static std::shared_ptr<BaseStrictObject> minmaxHelper(
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> arg1,
    cmpop_ty op) {
  if (args.size() > 0) {
    return minmaxMultiArgHelper(
        caller, std::move(args), std::move(kwargs), std::move(arg1), op);
  }
  return minmaxSingleArgHelper(caller, std::move(kwargs), std::move(arg1), op);
}

std::shared_ptr<BaseStrictObject> maxImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> arg1) {
  auto result = minmaxHelper(
      caller, std::move(args), std::move(kwargs), std::move(arg1), Gt);
  if (result == nullptr) {
    return makeUnknown(caller, "max()");
  }
  return result;
}

std::shared_ptr<BaseStrictObject> minImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> arg1) {
  auto result = minmaxHelper(
      caller, std::move(args), std::move(kwargs), std::move(arg1), Lt);
  if (result == nullptr) {
    return makeUnknown(caller, "min()");
  }
  return result;
}

std::shared_ptr<BaseStrictObject> anyImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable) {
  auto it = iGetElementsIter(std::move(iterable), caller);
  while (true) {
    auto nextValue = it->next(caller);
    if (it->isEnd()) {
      break;
    }
    auto truthValue = iGetTruthValue(std::move(nextValue), caller);
    if (truthValue == StrictTrue()) {
      return StrictTrue();
    }
  }
  return StrictFalse();
}

std::shared_ptr<BaseStrictObject> allImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable) {
  {
    auto it = iGetElementsIter(std::move(iterable), caller);
    while (true) {
      auto nextValue = it->next(caller);
      if (it->isEnd()) {
        break;
      }
      auto truthValue = iGetTruthValue(std::move(nextValue), caller);
      if (truthValue == StrictFalse()) {
        return StrictFalse();
      }
    }
    return StrictTrue();
  }
}

std::shared_ptr<BaseStrictObject> looseIsinstance(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> clsInfo) {
  if (inst->isUnknown()) {
    return StrictFalse();
  }

  return isinstanceImpl(nullptr, caller, std::move(inst), std::move(clsInfo));
}

std::shared_ptr<BaseStrictObject> strictCopy(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst) {
  return inst->copy(caller);
}

std::shared_ptr<BaseStrictObject> strictTryImport(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext&,
    std::shared_ptr<BaseStrictObject>) {
  return NoneObject();
}

std::shared_ptr<BaseStrictObject> strictKnownUnknownObj(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> name) {
  return makeUnknown(caller, "{}", name);
}

std::shared_ptr<BaseStrictObject> strictKnownUnknownCallable(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  if (args.empty()) {
    return makeUnknown(caller, "<unknown>");
  }
  std::vector<std::shared_ptr<BaseStrictObject>> restArg(
      std::next(args.begin()), args.end());
  return makeUnknown(caller, "{}({})", args[0], formatArgs(restArg, namedArgs));
}
} // namespace strictmod::objects
