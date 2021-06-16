// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/analyzer.h"

#include "StrictModules/Compiler/abstract_module_loader.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/exceptions.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"
#include "asdl.h"

namespace strictmod {
using namespace objects;

// AnalysisContextManager
static const std::string kDunderAnnotations = "__annotations__";
class LoopContinueException {};
class LoopBreakException {};

AnalysisContextManager::AnalysisContextManager(
    CallerContext& ctx,
    int newLine,
    int newCol)
    : context_(&ctx), oldLine_(ctx.lineno), oldCol_(ctx.col) {
  context_->lineno = newLine;
  context_->col = newCol;
}

AnalysisContextManager::~AnalysisContextManager() {
  context_->lineno = oldLine_;
  context_->col = oldCol_;
}

// Analyzer
Analyzer::Analyzer(
    mod_ty root,
    compiler::ModuleLoader* loader,
    Symtable table,
    BaseErrorSink* errors,
    std::string filename,
    std::string scopeName,
    std::shared_ptr<StrictModuleObject> caller,
    bool futureAnnotations)
    : Analyzer(
          root,
          loader,
          std::move(table),
          std::make_shared<DictType>(),
          errors,
          std::move(filename),
          std::move(scopeName),
          caller,
          futureAnnotations) {}

Analyzer::Analyzer(
    mod_ty root,
    compiler::ModuleLoader* loader,
    Symtable table,
    std::shared_ptr<DictType> toplevelNS,
    BaseErrorSink* errors,
    std::string filename,
    std::string scopeName,
    std::shared_ptr<StrictModuleObject> caller,
    bool futureAnnotations)
    : root_(root),
      loader_(loader),
      context_(
          std::move(caller),
          std::move(filename),
          std::move(scopeName),
          0,
          0,
          errors),
      stack_(
          table,
          scopeFactory,
          scopeFactory(table.entryFromAst(root_), std::move(toplevelNS))),
      futureAnnotations_(futureAnnotations),
      currentExceptionContext_() {}

Analyzer::Analyzer(
    compiler::ModuleLoader* loader,
    BaseErrorSink* errors,
    std::string filename,
    std::string scopeName,
    std::weak_ptr<StrictModuleObject> caller,
    int lineno,
    int col,
    const EnvT& closure,
    bool futureAnnotations)
    : root_(nullptr),
      loader_(loader),
      context_(
          std::move(caller),
          std::move(filename),
          std::move(scopeName),
          lineno,
          col,
          errors),
      stack_(EnvT(closure)),
      futureAnnotations_(futureAnnotations),
      currentExceptionContext_() {}

void Analyzer::visitImport(const stmt_ty stmt) {
  auto importNames = stmt->v.Import.names;
  Py_ssize_t n = asdl_seq_LEN(importNames);
  // TODO: actually handle imports
  // For now, just implement single import to demonstrate
  // using the module loader
  if (n == 1 && loader_ != nullptr) {
    alias_ty alias = reinterpret_cast<alias_ty>(asdl_seq_GET(importNames, 0));
    auto aliasName = reinterpret_cast<PyObject*>(alias->name);
    const char* aliasNameStr = PyUnicode_AsUTF8(aliasName);
    loader_->loadModule(aliasNameStr);
  } else if (n > 0) {
    raiseUnimplemented();
  }
}

void Analyzer::visitAssign(const stmt_ty stmt) {
  auto assignStmt = stmt->v.Assign;
  AnalysisResult value = visitExpr(assignStmt.value);
  if (value) {
    for (int i = 0; i < asdl_seq_LEN(assignStmt.targets); ++i) {
      expr_ty target =
          reinterpret_cast<expr_ty>(asdl_seq_GET(assignStmt.targets, i));
      assignToTarget(target, value);
    }
  }
}

void Analyzer::visitExprStmt(const stmt_ty stmt) {
  expr_ty expr = stmt->v.Expr.value;
  visitExpr(expr);
}

void Analyzer::visitFunctionDef(const stmt_ty stmt) {
  auto f = stmt->v.FunctionDef;
  return visitFunctionDefHelper(
      f.name,
      f.args,
      f.body,
      f.decorator_list,
      f.returns,
      f.type_comment,
      stmt,
      false);
}
void Analyzer::visitAsyncFunctionDef(const stmt_ty stmt) {
  auto f = stmt->v.AsyncFunctionDef;
  return visitFunctionDefHelper(
      f.name,
      f.args,
      f.body,
      f.decorator_list,
      f.returns,
      f.type_comment,
      stmt,
      true);
}

AnalysisResult Analyzer::visitAnnotationHelper(expr_ty annotation) {
  if (futureAnnotations_) {
    PyObject* annotationStr = _PyAST_ExprAsUnicode(annotation);
    return std::make_shared<StrictString>(
        StrType(), context_.caller, annotationStr);
    Py_DECREF(annotationStr);
  } else {
    return visitExpr(annotation);
  }
}

void Analyzer::addToDunderAnnotationsHelper(
    expr_ty target,
    AnalysisResult value) {
  if (!stack_.localContains(kDunderAnnotations)) {
    auto annotationsDict =
        std::make_shared<StrictDict>(DictObjectType(), context_.caller);
    stack_.localSet(kDunderAnnotations, std::move(annotationsDict));
  }

  auto key = std::make_shared<StrictString>(
      StrType(), context_.caller, target->v.Name.id);
  iSetElement(
      stack_[kDunderAnnotations], std::move(key), std::move(value), context_);
}

void Analyzer::visitArgHelper(arg_ty arg, DictDataT& annotations) {
  if (arg->annotation == nullptr) {
    return;
  }
  AnalysisResult key =
      std::make_shared<StrictString>(StrType(), context_.caller, arg->arg);
  annotations[std::move(key)] = visitAnnotationHelper(arg->annotation);
}

void Analyzer::visitArgHelper(
    std::vector<std::string>& args,
    arg_ty arg,
    DictDataT& annotations) {
  args.emplace_back(PyUnicode_AsUTF8(arg->arg));
  if (arg->annotation != nullptr) {
    visitArgHelper(arg, annotations);
  }
}

void Analyzer::visitFunctionDefHelper(
    identifier name,
    arguments_ty args,
    asdl_seq* body,
    asdl_seq* decoratorList,
    expr_ty returns,
    string, // type_comment
    stmt_ty node,
    bool isAsync) {
  // symbol table for function body
  SymtableEntry symbols = stack_.getSymtable().entryFromAst(node);
  // function name and qualname
  std::string funcName = PyUnicode_AsUTF8(name);
  std::string qualName = stack_.getQualifiedScopeName();
  if (qualName.empty()) {
    qualName = funcName;
  } else {
    qualName.append(".");
    qualName.append(funcName);
  }
  // function body
  int bodySize = asdl_seq_LEN(body);
  std::vector<stmt_ty> bodyVec;
  bodyVec.reserve(bodySize);
  for (int i = 0; i < bodySize; ++i) {
    bodyVec.push_back(reinterpret_cast<stmt_ty>(asdl_seq_GET(body, i)));
  }
  // arguments
  std::vector<std::string> posonlyArgs;
  std::vector<std::string> posArgs;
  std::vector<std::string> kwonlyArgs;
  std::optional<std::string> varArg;
  std::optional<std::string> kwVarArg;
  DictDataT annotations;

  int posonlySize = asdl_seq_LEN(args->posonlyargs);
  posonlyArgs.reserve(posonlySize);
  for (int i = 0; i < posonlySize; ++i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->posonlyargs, i));
    visitArgHelper(posonlyArgs, a, annotations);
  }

  int posSize = asdl_seq_LEN(args->args);
  posArgs.reserve(posSize);
  for (int i = 0; i < posSize; ++i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->args, i));
    visitArgHelper(posArgs, a, annotations);
  }

  if (args->vararg != nullptr) {
    varArg.emplace(PyUnicode_AsUTF8(args->vararg->arg));
    visitArgHelper(args->vararg, annotations);
  }

  int kwSize = asdl_seq_LEN(args->kwonlyargs);
  kwonlyArgs.reserve(kwSize);
  for (int i = 0; i < kwSize; ++i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->kwonlyargs, i));
    visitArgHelper(kwonlyArgs, a, annotations);
  }

  if (args->kwarg != nullptr) {
    kwVarArg.emplace(PyUnicode_AsUTF8(args->kwarg->arg));
    visitArgHelper(args->kwarg, annotations);
  }
  // argument defaults
  std::vector<AnalysisResult> posDefaults;
  std::vector<AnalysisResult> kwDefaults;

  int kwDefaultSize = asdl_seq_LEN(args->kw_defaults);
  kwDefaults.reserve(kwDefaultSize);
  for (int i = 0; i < kwDefaultSize; ++i) {
    expr_ty d = reinterpret_cast<expr_ty>(asdl_seq_GET(args->kw_defaults, i));
    if (d == nullptr) {
      kwDefaults.push_back(nullptr);
    } else {
      kwDefaults.push_back(visitExpr(d));
    }
  }

  int posDefaultSize = asdl_seq_LEN(args->defaults);
  posDefaults.reserve(posDefaultSize);
  for (int i = 0; i < posDefaultSize; ++i) {
    expr_ty d = reinterpret_cast<expr_ty>(asdl_seq_GET(args->defaults, i));
    posDefaults.push_back(visitExpr(d));
  }

  // annotations object
  if (returns != nullptr) {
    annotations[context_.makeStr("return")] = visitExpr(returns);
  }

  std::shared_ptr<StrictDict> annotationsObj = std::make_shared<StrictDict>(
      DictObjectType(), context_.caller, std::move(annotations));

  AnalysisResult func(new StrictFunction(
      FunctionType(),
      context_.caller,
      funcName,
      std::move(qualName),
      node->lineno,
      node->col_offset,
      std::move(bodyVec),
      stack_.getFunctionScope(),
      std::move(symbols),
      std::move(posonlyArgs),
      std::move(posArgs),
      std::move(kwonlyArgs),
      std::move(varArg),
      std::move(kwVarArg),
      std::move(posDefaults),
      std::move(kwDefaults),
      loader_,
      context_.filename,
      std::move(annotationsObj),
      futureAnnotations_,
      isAsync));

  // decorators
  int decoratorSize = asdl_seq_LEN(decoratorList);
  // decorators should be applied in reverse order
  for (int i = decoratorSize - 1; i >= 0; --i) {
    expr_ty dec = reinterpret_cast<expr_ty>(asdl_seq_GET(decoratorList, i));
    AnalysisResult decObj = visitExpr(dec);
    // call decorators, fix lineno
    {
      auto contextManager = updateContextHelper(dec->lineno, dec->col_offset);
      AnalysisResult obj = iCall(decObj, {func}, kEmptyArgNames, context_);
      func.swap(obj);
    }
  }

  // put function in scope
  stack_[std::move(funcName)] = std::move(func);
}

void Analyzer::visitReturn(const stmt_ty stmt) {
  expr_ty returnV = stmt->v.Return.value;
  if (returnV == nullptr) {
    throw(FunctionReturnException(NoneObject()));
  }
  AnalysisResult returnVal = visitExpr(stmt->v.Return.value);
  throw(FunctionReturnException(std::move(returnVal)));
}

static std::shared_ptr<DictType> prepareToDictHelper(
    AnalysisResult obj,
    const CallerContext& caller) {
  auto dictObj = std::dynamic_pointer_cast<StrictDict>(obj);
  if (dictObj == nullptr) {
    caller.raiseTypeError(
        "__prepare__ must return a dict, not {} object",
        obj->getTypeRef().getName());
  }
  std::shared_ptr<DictType> resultPtr = std::make_shared<DictType>();
  DictType result = *resultPtr;
  result.reserve(dictObj->getData().size());
  for (auto& item : dictObj->getData()) {
    auto strObjKey = std::dynamic_pointer_cast<StrictString>(item.first);
    if (strObjKey != nullptr) {
      result[strObjKey->getValue()] = item.second;
    }
  }
  return resultPtr;
}

static AnalysisResult strDictToObjHelper(
    std::shared_ptr<DictType> dict,
    const CallerContext& caller) {
  DictDataT dictObj;
  dictObj.reserve(dict->size());
  for (auto& item : *dict) {
    dictObj[caller.makeStr(item.first)] = item.second;
  }
  return std::make_shared<StrictDict>(
      DictObjectType(), caller.caller, std::move(dictObj));
}

void Analyzer::visitClassDef(const stmt_ty stmt) {
  auto classDef = stmt->v.ClassDef;
  // Step 1, identify metaclass
  std::shared_ptr<StrictType> metaclass;
  std::vector<AnalysisResult> bases = visitListLikeHelper(classDef.bases);
  // register metaclass if found in keyword args
  // find if any base class has metaclass
  int kwSize = asdl_seq_LEN(classDef.keywords);
  std::vector<std::string> kwargKeys;
  std::vector<AnalysisResult> kwargValues;
  kwargKeys.reserve(kwSize);
  kwargValues.reserve(kwSize);
  for (int i = 0; i < kwSize; ++i) {
    keyword_ty kw =
        reinterpret_cast<keyword_ty>(asdl_seq_GET(classDef.keywords, i));
    AnalysisResult kwVal = visitExpr(kw->value);
    if (PyUnicode_CompareWithASCIIString(kw->arg, "metaclass") == 0) {
      // TODO: do we need to worry about metaclass that not subclass of type?
      auto kwValTyp = assertStaticCast<StrictType>(std::move(kwVal));
      metaclass = std::move(kwValTyp);
    } else {
      kwargKeys.push_back(PyUnicode_AsUTF8(kw->arg));
      kwargValues.push_back(std::move(kwVal));
    }
  }

  bool replacedBases = false;
  AnalysisResult origBases;

  if (metaclass == nullptr && !bases.empty()) {
    // check if __mro_entries__ is defined for any bases
    std::vector<AnalysisResult> newBases;
    newBases.reserve(bases.size());
    auto baseTuple =
        std::make_shared<StrictTuple>(TupleType(), context_.caller, bases);

    for (auto& base : bases) {
      auto baseType = base->getType();
      if (baseType == UnknownType() ||
          std::dynamic_pointer_cast<StrictType>(base) != nullptr) {
        newBases.push_back(base);
        continue;
      }
      auto mroEntriesFunc = baseType->typeLookup("__mro_entries__", context_);
      if (mroEntriesFunc != nullptr) {
        auto newBaseEntries =
            iCall(mroEntriesFunc, {base, baseTuple}, kEmptyArgNames, context_);
        if (newBaseEntries != nullptr) {
          auto newBaseVec =
              iGetElementsVec(std::move(newBaseEntries), context_);
          newBases.insert(
              newBases.end(),
              std::move_iterator(newBaseVec.begin()),
              std::move_iterator(newBaseVec.end()));
          replacedBases = true;
        }
      }
    }

    if (replacedBases) {
      origBases = baseTuple;
    }
    std::swap(bases, newBases);

    // look for most common metaclass for all bases, skipping over
    // unknowns. Identify metaclass conflict
    for (auto& base : bases) {
      auto baseTyp = std::dynamic_pointer_cast<StrictType>(base);
      if (baseTyp == nullptr) {
        continue;
      }
      auto baseTypMeta = baseTyp->getType();
      if (metaclass == nullptr) {
        metaclass = std::move(baseTypMeta);
      } else if (metaclass->isSubType(baseTypMeta)) {
        continue;
      } else if (baseTypMeta->isSubType(metaclass)) {
        metaclass = std::move(baseTypMeta);
        continue;
      } else {
        context_.raiseTypeError("metaclass conflict");
      }
    }
  }
  if (metaclass == nullptr) {
    metaclass = TypeType();
  }

  // Step 2, run __prepare__ if exists, creating namespace ns
  std::shared_ptr<DictType> ns = std::make_shared<DictType>();
  std::string className = PyUnicode_AsUTF8(classDef.name);
  auto classNameObj = context_.makeStr(className);
  auto baseTupleObj =
      std::make_shared<StrictTuple>(TupleType(), context_.caller, bases);
  if (metaclass->getType() == UnknownType()) {
    context_.error<UnknownValueCallException>(metaclass->getDisplayName());
  } else {
    auto prepareFunc = iLoadAttr(metaclass, "__prepare__", nullptr, context_);
    if (prepareFunc != nullptr) {
      auto nsObj = iCall(
          prepareFunc, {classNameObj, baseTupleObj}, kEmptyArgNames, context_);
      ns = prepareToDictHelper(nsObj, context_); // TODO
    }
  }

  // Step 3, create a hidden scope containing __class__
  // TODO

  // Step 4, visit statements in class body with __class__ scope
  // Then ns scope
  {
    auto classContextManager = stack_.enterScopeByAst(stmt, ns);
    visitStmtSeq(classDef.body);
  }
  // Step 5, extract the resulting ns scope, add __orig_bases__
  // if mro entries is used in step 1
  if (origBases != nullptr) {
    (*ns)["__orig_bases__"] = std::move(origBases);
  }
  auto classDict = strDictToObjHelper(ns, context_);

  // Step 6, call metaclass with class name, bases, ns, and kwargs
  std::vector<AnalysisResult> classCallArg{
      std::move(classNameObj), std::move(baseTupleObj), classDict};
  classCallArg.insert(
      classCallArg.end(),
      std::move_iterator(kwargValues.begin()),
      std::move_iterator(kwargValues.end()));

  auto classObj =
      iCall(metaclass, std::move(classCallArg), std::move(kwargKeys), context_);
  // Step 7, apply decorators
  int decoratorSize = asdl_seq_LEN(classDef.decorator_list);
  // decorators should be applied in reverse order
  for (int i = decoratorSize - 1; i >= 0; --i) {
    expr_ty dec =
        reinterpret_cast<expr_ty>(asdl_seq_GET(classDef.decorator_list, i));
    AnalysisResult decObj = visitExpr(dec);
    // call decorators, fix lineno
    {
      auto contextManager = updateContextHelper(dec->lineno, dec->col_offset);
      AnalysisResult tempClass =
          iCall(decObj, {classObj}, kEmptyArgNames, context_);
      std::swap(classObj, tempClass);
    }
  }
  // Step 8, populate __class__ in hidden scope defined in step 3
  // TODO
  stack_[std::move(className)] = std::move(classObj);
}

void Analyzer::visitPass(const stmt_ty) {}

void Analyzer::visitDelete(const stmt_ty stmt) {
  auto delStmt = stmt->v.Delete;
  for (int i = 0; i < asdl_seq_LEN(delStmt.targets); ++i) {
    expr_ty target =
        reinterpret_cast<expr_ty>(asdl_seq_GET(delStmt.targets, i));
    assignToTarget(target, nullptr);
  }
}

void Analyzer::visitAugAssign(const stmt_ty stmt) {
  // TODO: this is not quite accurate, the correct dunder methods
  // to use are __i<op>__ methods. But we have been using normal
  // bin ops just fine. Add __i<op>__ methods if needed
  auto augAssign = stmt->v.AugAssign;
  AnalysisResult leftV = visitExpr(augAssign.target);
  AnalysisResult rightV = visitExpr(augAssign.value);
  AnalysisResult result =
      iDoBinOp(std::move(leftV), std::move(rightV), augAssign.op, context_);
  assignToTarget(augAssign.target, std::move(result));
}
void Analyzer::visitAnnAssign(const stmt_ty stmt) {
  auto annAssign = stmt->v.AnnAssign;
  expr_ty target = annAssign.target;

  if (annAssign.value != nullptr) {
    assignToTarget(target, visitExpr(annAssign.value));
  }
  bool classScope = stack_.isClassScope();
  bool evalAnnotations = classScope || stack_.isGlobalScope();
  if (target->kind == Name_kind && evalAnnotations) {
    AnalysisResult type = visitAnnotationHelper(annAssign.annotation);
    if (classScope) {
      addToDunderAnnotationsHelper(target, std::move(type));
    }
  }
}

void Analyzer::visitFor(const stmt_ty stmt) {
  auto forExpr = stmt->v.For;
  AnalysisResult iterObj = visitExpr(forExpr.iter);
  bool hasBreak = false;

  for (auto& element : iGetElementsVec(iterObj, context_)) {
    assignToTarget(forExpr.target, element);
    try {
      visitStmtSeq(forExpr.body);
    } catch (const LoopContinueException&) {
    } catch (const LoopBreakException&) {
      hasBreak = true;
      break;
    }
  }

  if (!hasBreak) {
    visitStmtSeq(forExpr.orelse);
  }
}

void Analyzer::visitWhile(const stmt_ty stmt) {
  int iterCount = 0;
  bool hasBreak = false;
  auto whileStmt = stmt->v.While;

  while (true) {
    // check too many iterations
    if (++iterCount > kIterationLimit) {
      context_.error<StrictModuleTooManyIterationsException>();
      break;
    }
    // check guard
    AnalysisResult test = iGetTruthValue(visitExpr(whileStmt.test), context_);
    if (test != StrictTrue()) {
      break;
    }
    // execute body
    try {
      visitStmtSeq(whileStmt.body);
    } catch (const LoopContinueException&) {
    } catch (const LoopBreakException&) {
      hasBreak = true;
      break;
    }
  }
  // orelse if no break
  if (!hasBreak) {
    visitStmtSeq(whileStmt.orelse);
  }
}

void Analyzer::visitIf(const stmt_ty stmt) {
  auto ifStmt = stmt->v.If;
  AnalysisResult testBool = iGetTruthValue(visitExpr(ifStmt.test), context_);
  if (testBool == StrictTrue()) {
    visitStmtSeq(ifStmt.body);
  } else if (testBool == StrictFalse()) {
    visitStmtSeq(ifStmt.orelse);
  }
}

void Analyzer::visitWith(const stmt_ty stmt) {
  auto withStmt = stmt->v.With;
  int withItemSize = asdl_seq_LEN(withStmt.items);
  std::vector<AnalysisResult> contexts;
  contexts.reserve(withItemSize);
  // enter and record context
  for (int i = 0; i < withItemSize; ++i) {
    withitem_ty item =
        reinterpret_cast<withitem_ty>(asdl_seq_GET(withStmt.items, i));
    AnalysisResult context = visitExpr(item->context_expr);
    auto enterFunc = iLoadAttrOnType(
        context,
        "__enter__",
        makeUnknown(context_, "{}.__enter__", context),
        context_);
    auto enterItem =
        iCall(std::move(enterFunc), kEmptyArgs, kEmptyArgNames, context_);
    if (item->optional_vars != nullptr) {
      assignToTarget(item->optional_vars, std::move(enterItem));
    }
    contexts.push_back(std::move(context));
  }
  // body
  AnalysisResult returnVal;
  std::optional<StrictModuleUserException<BaseStrictObject>> userExc;
  AnalysisResult excType = NoneObject();
  AnalysisResult excVal = NoneObject();
  AnalysisResult excTb = NoneObject();

  try {
    visitStmtSeq(withStmt.body);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    userExc = e;
    excType = e.getWrapped()->getType();
    excVal = e.getWrapped();
    excTb = makeUnknown(context_, "traceback of {}", excVal);
  } catch (FunctionReturnException& r) {
    returnVal = r.getVal();
  }

  // exit contexts, in reverse order
  bool suppressExc = false;
  for (auto i = contexts.rbegin(); i != contexts.rend(); ++i) {
    AnalysisResult context = *i;
    auto exitFunc = iLoadAttrOnType(
        context,
        "__exit__",
        makeUnknown(context_, "{}.__exit__", context),
        context_);
    auto exitResult = iCall(
        std::move(exitFunc),
        {excType, excVal, excTb},
        kEmptyArgNames,
        context_);
    auto exitResultBool = iGetTruthValue(std::move(exitResult), context_);
    suppressExc |= exitResultBool == StrictTrue();
  }

  if (!suppressExc && userExc.has_value()) {
    throw userExc.value();
  }
  if (returnVal != nullptr) {
    throw FunctionReturnException(std::move(returnVal));
  }
}

void Analyzer::visitRaise(const stmt_ty stmt) {
  auto raiseStmt = stmt->v.Raise;
  expr_ty excExpr = raiseStmt.exc;
  AnalysisResult exc;
  if (excExpr != nullptr) {
    exc = visitExpr(excExpr);
    if (std::dynamic_pointer_cast<StrictType>(exc)) {
      // call the exception type
      exc = iCall(std::move(exc), kEmptyArgs, kEmptyArgNames, context_);
    }
  } else {
    exc = currentExceptionContext_;
    if (exc == nullptr) {
      context_.raiseExceptionStr(RuntimeErrorType(), "no active exceptions");
    }
  }
  expr_ty excCause = raiseStmt.cause;
  if (excCause != nullptr) {
    visitExpr(excCause);
  }
  context_.raiseExceptionFromObj(std::move(exc));
}

bool Analyzer::visitExceptionHandlerHelper(
    asdl_seq* handlers,
    AnalysisResult exc) {
  int handlersSize = asdl_seq_LEN(handlers);
  for (int i = 0; i < handlersSize; ++i) {
    excepthandler_ty handler =
        reinterpret_cast<excepthandler_ty>(asdl_seq_GET(handlers, i));
    {
      // set the correct lineno
      auto _ = AnalysisContextManager(
          context_, handler->lineno, handler->col_offset);

      auto handlerV = handler->v.ExceptHandler;
      bool matched = false;
      if (handlerV.type != nullptr) {
        auto handledType =
            assertStaticCast<StrictType>(visitExpr(handlerV.type));
        // TODO: use the implementation of isinstance here
        matched = exc->getTypeRef().isSubType(handledType);
      } else {
        // match against everything
        matched = true;
      }
      if (matched) {
        currentExceptionContext_ = exc;
        std::string excName;
        if (handlerV.name != nullptr) {
          excName = PyUnicode_AsUTF8(handlerV.name);
          stack_[excName] = exc;
          visitStmtSeq(handlerV.body);
          stack_.erase(excName);
        } else {
          visitStmtSeq(handlerV.body);
        }
        currentExceptionContext_ = nullptr;
        return true;
      }
    }
  }
  return false;
}

void Analyzer::visitTry(const stmt_ty stmt) {
  auto tryStmt = stmt->v.Try;
  bool caughtException = false;
  auto _ = TryFinallyManager(*this, tryStmt.finalbody);
  try {
    visitStmtSeq(tryStmt.body);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    caughtException = true;
    bool handledException =
        visitExceptionHandlerHelper(tryStmt.handlers, e.getWrapped());
    if (!handledException) {
      throw;
    }
  }
  if (!caughtException) {
    visitStmtSeq(tryStmt.orelse);
  }
}

void Analyzer::visitAssert(const stmt_ty) {}

void Analyzer::visitBreak(const stmt_ty) {
  throw LoopBreakException();
}

void Analyzer::visitContinue(const stmt_ty) {
  throw LoopContinueException();
}

// Expressions
AnalysisResult Analyzer::visitConstant(const expr_ty expr) {
  auto constant = expr->v.Constant;
  if (PyLong_CheckExact(constant.value)) {
    auto value =
        std::make_shared<StrictInt>(IntType(), context_.caller, constant.value);
    return value;
  }
  if (PyUnicode_CheckExact(constant.value)) {
    auto value = std::make_shared<StrictString>(
        StrType(), context_.caller, constant.value);
    return value;
  }
  if (PyFloat_CheckExact(constant.value)) {
    auto value = std::make_shared<StrictFloat>(
        FloatType(), context_.caller, constant.value);
    return value;
  }
  if (constant.value == Py_None) {
    return NoneObject();
  }
  if (constant.value == Py_True) {
    return StrictTrue();
  }
  if (constant.value == Py_False) {
    return StrictFalse();
  }
  return defaultVisitExpr();
}

AnalysisResult Analyzer::visitName(const expr_ty expr) {
  auto name = expr->v.Name;
  assert(name.ctx != Store && name.ctx != Del && name.ctx != AugStore);
  const char* nameStr = PyUnicode_AsUTF8(name.id);
  auto value = stack_.at(nameStr);
  if (!value) {
    // TODO? decide whether to raise NameError or UnboundLocalError base on
    // declaration
    context_.raiseExceptionStr(
        NameErrorType(), "name {} is not defined", nameStr);
  }
  return *value;
}

AnalysisResult Analyzer::visitAttribute(const expr_ty expr) {
  auto attribute = expr->v.Attribute;
  AnalysisResult value = visitExpr(attribute.value);
  assert(value != nullptr);
  const char* attrName = PyUnicode_AsUTF8(attribute.attr);
  assert(attribute.ctx != Del && attribute.ctx != Store);
  auto result = iLoadAttr(value, attrName, nullptr, context_);
  if (!result) {
    context_.raiseExceptionStr(
        AttributeErrorType(),
        "{} object has no attribute {}",
        value->getTypeRef().getName(),
        attrName);
  }
  return result;
}

AnalysisResult Analyzer::visitCall(const expr_ty expr) {
  auto call = expr->v.Call;
  AnalysisResult func = visitExpr(call.func);
  assert(func != nullptr);

  auto argsSeq = call.args;
  auto kwargsSeq = call.keywords;
  int argsLen = asdl_seq_LEN(argsSeq);
  int kwargsLen = asdl_seq_LEN(kwargsSeq);

  std::vector<AnalysisResult> args;
  std::vector<std::string> argNames;
  args.reserve(argsLen + kwargsLen);
  argNames.reserve(kwargsLen);

  for (int i = 0; i < argsLen; ++i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(argsSeq, i));
    if (argExpr->kind == Starred_kind) {
      auto starredElts = visitExpr(argExpr);
      auto starredEltsVec = iGetElementsVec(std::move(starredElts), context_);
      args.insert(
          args.end(),
          std::move_iterator(starredEltsVec.begin()),
          std::move_iterator(starredEltsVec.end()));
    } else {
      args.push_back(visitExpr(argExpr));
    }
  }
  for (int i = 0; i < kwargsLen; ++i) {
    keyword_ty kw = reinterpret_cast<keyword_ty>(asdl_seq_GET(kwargsSeq, i));
    args.push_back(visitExpr(kw->value));
    argNames.emplace_back(PyUnicode_AsUTF8(kw->arg));
  }
  return iCall(func, std::move(args), std::move(argNames), context_);
}

std::vector<AnalysisResult> Analyzer::visitListLikeHelper(asdl_seq* elts) {
  int eltsLen = asdl_seq_LEN(elts);
  std::vector<AnalysisResult> data;
  data.reserve(eltsLen);
  for (int i = 0; i < eltsLen; ++i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
    if (argExpr->kind == Starred_kind) {
      AnalysisResult starredVal = visitStarred(argExpr);
      std::vector<AnalysisResult> starredVec =
          iGetElementsVec(starredVal, context_);
      data.insert(
          data.end(),
          std::move_iterator(starredVec.begin()),
          std::move_iterator(starredVec.end()));
    } else {
      data.push_back(visitExpr(argExpr));
    }
  }
  return data;
}

AnalysisResult Analyzer::visitSet(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.Set.elts);
  SetDataT data(std::move_iterator(v.begin()), std::move_iterator(v.end()));
  AnalysisResult obj =
      std::make_shared<StrictSet>(SetType(), context_.caller, std::move(data));
  return obj;
}

AnalysisResult Analyzer::visitList(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.List.elts);
  AnalysisResult obj =
      std::make_shared<StrictList>(ListType(), context_.caller, std::move(v));
  return obj;
}

AnalysisResult Analyzer::visitTuple(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.Tuple.elts);
  AnalysisResult obj =
      std::make_shared<StrictTuple>(TupleType(), context_.caller, std::move(v));
  return obj;
}

DictDataT Analyzer::visitDictUnpackHelper(expr_ty valueExpr) {
  AnalysisResult unpacked = visitExpr(valueExpr);
  auto keys = iGetElementsVec(unpacked, context_);

  DictDataT map;
  map.reserve(keys.size());
  for (auto& k : keys) {
    auto value = iGetElement(unpacked, k, context_);
    map[k] = std::move(value);
  }
  return map;
}

AnalysisResult Analyzer::visitDict(const expr_ty expr) {
  auto dict = expr->v.Dict;
  int keysLen = asdl_seq_LEN(dict.keys);
  assert(keysLen == asdl_seq_LEN(dict.values));

  DictDataT map;
  map.reserve(keysLen);

  for (int i = 0; i < keysLen; ++i) {
    expr_ty keyExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(dict.keys, i));
    expr_ty valueExpr =
        reinterpret_cast<expr_ty>(asdl_seq_GET(dict.values, i));
    if (keyExpr == nullptr) {
      // handle unpacking
      DictDataT unpackedMap = visitDictUnpackHelper(valueExpr);
      map.insert(
          std::move_iterator(unpackedMap.begin()),
          std::move_iterator(unpackedMap.end()));
    } else {
      AnalysisResult kResult = visitExpr(keyExpr);
      AnalysisResult vResult = visitExpr(valueExpr);
      map[kResult] = vResult;
    }
  }
  return std::make_shared<StrictDict>(
      DictObjectType(), context_.caller, std::move(map));
}

AnalysisResult Analyzer::visitBinOp(const expr_ty expr) {
  AnalysisResult left = visitExpr(expr->v.BinOp.left);
  AnalysisResult right = visitExpr(expr->v.BinOp.right);
  return iDoBinOp(
      std::move(left), std::move(right), expr->v.BinOp.op, context_);
}

AnalysisResult Analyzer::visitUnaryOp(const expr_ty expr) {
  AnalysisResult value = visitExpr(expr->v.UnaryOp.operand);
  unaryop_ty op = expr->v.UnaryOp.op;
  if (op == Not) {
    auto result = iGetTruthValue(std::move(value), context_);
    if (result->getType() == UnknownType()) {
      return result;
    }
    return context_.makeBool(result == StrictFalse());
  }
  return iUnaryOp(std::move(value), op, context_);
}

AnalysisResult Analyzer::visitCompare(const expr_ty expr) {
  auto compare = expr->v.Compare;
  AnalysisResult leftObj = visitExpr(compare.left);
  int cmpSize = asdl_seq_LEN(compare.ops);
  AnalysisResult compareValue;
  for (int i = 0; i < cmpSize; ++i) {
    cmpop_ty op = static_cast<cmpop_ty>(asdl_seq_GET(compare.ops, i));
    expr_ty rightExpr =
        reinterpret_cast<expr_ty>(asdl_seq_GET(compare.comparators, i));
    AnalysisResult rightObj = visitExpr(rightExpr);
    compareValue = iBinCmpOp(leftObj, rightObj, op, context_);
    AnalysisResult compareBool = iGetTruthValue(compareValue, context_);
    if (compareBool == StrictFalse()) {
      // short circuit
      return compareValue;
    }
    if (compareBool->getType() == UnknownType()) {
      // unknown result
      return compareValue;
    }
    leftObj = std::move(rightObj);
  }
  assert(compareValue != nullptr);
  return compareValue;
}

AnalysisResult Analyzer::visitBoolOp(const expr_ty expr) {
  auto boolOpExpr = expr->v.BoolOp;
  AnalysisResult result;
  int valuesSize = asdl_seq_LEN(boolOpExpr.values);
  for (int i = 0; i < valuesSize; ++i) {
    expr_ty e = reinterpret_cast<expr_ty>(asdl_seq_GET(boolOpExpr.values, i));
    result = visitExpr(e);
    auto resultBool = iGetTruthValue(result, context_);
    if (boolOpExpr.op == And && resultBool == StrictFalse()) {
      break;
    } else if (boolOpExpr.op == Or && resultBool == StrictTrue()) {
      break;
    } else if (resultBool->getType() == UnknownType()) {
      break;
    }
  }
  return result;
}

AnalysisResult Analyzer::visitNamedExpr(const expr_ty expr) {
  auto namedExpr = expr->v.NamedExpr;
  AnalysisResult value = visitExpr(namedExpr.value);
  assignToTarget(namedExpr.target, value);
  return value;
}

AnalysisResult Analyzer::visitSubscript(const expr_ty expr) {
  auto subscrExpr = expr->v.Subscript;
  AnalysisResult base = visitExpr(subscrExpr.value);
  AnalysisResult idx = visitSliceHelper(subscrExpr.slice);
  return iGetElement(std::move(base), std::move(idx), context_);
}

AnalysisResult Analyzer::visitStarred(const expr_ty expr) {
  return visitExpr(expr->v.Starred.value);
}

AnalysisResult Analyzer::visitSliceHelper(slice_ty slice) {
  switch (slice->kind) {
    case Slice_kind: {
      auto sliceExp = slice->v.Slice;
      AnalysisResult start =
          sliceExp.lower ? visitExpr(sliceExp.lower) : NoneObject();
      AnalysisResult stop =
          sliceExp.upper ? visitExpr(sliceExp.upper) : NoneObject();
      AnalysisResult step =
          sliceExp.step ? visitExpr(sliceExp.step) : NoneObject();
      return std::make_shared<StrictSlice>(
          SliceType(),
          context_.caller,
          std::move(start),
          std::move(stop),
          std::move(step));
    }
    case ExtSlice_kind: {
      auto extSliceExp = slice->v.ExtSlice;
      int dimSize = asdl_seq_LEN(extSliceExp.dims);
      std::vector<AnalysisResult> extTuple;
      extTuple.reserve(dimSize);
      for (int i = 0; i < dimSize; ++i) {
        slice_ty dim =
            reinterpret_cast<slice_ty>(asdl_seq_GET(extSliceExp.dims, i));
        extTuple.push_back(visitSliceHelper(dim));
      }
      return std::make_shared<StrictTuple>(
          TupleType(), context_.caller, std::move(extTuple));
    }
    case Index_kind:
      return visitExpr(slice->v.Index.value);
  }
}

void Analyzer::visitStmtSeq(const asdl_seq* seq) {
  for (int i = 0; i < asdl_seq_LEN(seq); i++) {
    stmt_ty elt = reinterpret_cast<stmt_ty>(asdl_seq_GET(seq, i));
    visitStmt(elt);
  }
}

void Analyzer::visitStmtSeq(std::vector<stmt_ty> seq) {
  for (const stmt_ty s : seq) {
    visitStmt(s);
  }
}

void Analyzer::defaultVisitMod() {
  raiseUnimplemented();
}

void Analyzer::defaultVisitStmt() {
  raiseUnimplemented();
}

AnalysisResult Analyzer::defaultVisitExpr() {
  raiseUnimplemented();
  return makeUnknown(context_, "<unimplemented expr>");
}

AnalysisContextManager Analyzer::updateContext(stmt_ty stmt) {
  return updateContextHelper(stmt->lineno, stmt->col_offset);
}
AnalysisContextManager Analyzer::updateContext(expr_ty expr) {
  return updateContextHelper(expr->lineno, expr->col_offset);
}
AnalysisContextManager Analyzer::updateContext(mod_ty) {
  return updateContextHelper(0, 0);
}

void Analyzer::assignToTarget(const expr_ty target, AnalysisResult value) {
  switch (target->kind) {
    case Name_kind:
      assignToName(target, std::move(value));
      break;
    case Tuple_kind:
      assignToListLike(target->v.Tuple.elts, std::move(value));
      break;
    case List_kind:
      assignToListLike(target->v.List.elts, std::move(value));
      break;
    case Attribute_kind:
      assignToAttribute(target, std::move(value));
      break;
    case Subscript_kind:
      assignToSubscript(target, std::move(value));
      break;
    case Starred_kind:
      assignToStarred(target, std::move(value));
      break;
    default:
      return;
  }
}

void Analyzer::assignToName(const expr_ty target, AnalysisResult value) {
  PyObject* id = target->v.Name.id;
  const char* name = PyUnicode_AsUTF8(id);
  if (value == nullptr) {
    stack_.erase(name);
  } else {
    stack_[name] = value;
  }
}

void Analyzer::assignToListLike(asdl_seq* elts, AnalysisResult value) {
  int eltsSize = asdl_seq_LEN(elts);

  // delete case
  if (value == nullptr) {
    for (int i = 0; i < eltsSize; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, nullptr);
    }
    return;
  }

  std::vector<AnalysisResult> rData =
      iGetElementsVec(std::move(value), context_);
  // check of there is star
  int starIdx = -1;
  for (int i = 0; i < eltsSize; ++i) {
    expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
    if (elt->kind == Starred_kind) {
      starIdx = i;
      break;
    }
  }

  // assignment with star on the lhs, starred exp cannot be used in delete
  if (starIdx >= 0 && eltsSize <= int(rData.size() + 1)) {
    // process part before star
    for (int i = 0; i < starIdx; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, rData[i]);
    }
    // process the star part
    int restSize = eltsSize - starIdx - 1;
    int tailBound = rData.size() - restSize;
    std::vector<AnalysisResult> starData(
        rData.begin() + starIdx, rData.begin() + tailBound);
    AnalysisResult starList = std::make_shared<StrictList>(
        ListType(), context_.caller, std::move(starData));
    expr_ty starElt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, starIdx));
    assignToTarget(starElt, std::move(starList));
    // process the part after star
    for (int i = 0; i < restSize; ++i) {
      expr_ty elt =
          reinterpret_cast<expr_ty>(asdl_seq_GET(elts, starIdx + i + 1));
      assignToTarget(elt, rData[tailBound + i]);
    }
  }
  // normal assign
  else if (eltsSize == int(rData.size())) {
    for (int i = 0; i < eltsSize; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, rData[i]);
    }
  } else {
    // failed to unpack
    context_.error<FailedToUnpackException>(std::to_string(eltsSize));
    for (int i = 0; i < eltsSize; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, makeUnknown(context_, "<failed unpack>"));
    }
  }
}

void Analyzer::assignToAttribute(const expr_ty attr, AnalysisResult value) {
  auto attrExpr = attr->v.Attribute;
  AnalysisResult base = visitExpr(attrExpr.value);
  std::string name = PyUnicode_AsUTF8(attrExpr.attr);
  if (value == nullptr) {
    iDelAttr(std::move(base), name, context_);
  } else {
    iStoreAttr(std::move(base), name, std::move(value), context_);
  }
}

void Analyzer::assignToSubscript(const expr_ty subscr, AnalysisResult value) {
  auto subExpr = subscr->v.Subscript;
  AnalysisResult base = visitExpr(subExpr.value);
  AnalysisResult slice = visitSliceHelper(subExpr.slice);
  if (value == nullptr) {
    iDelElement(std::move(base), std::move(slice), context_);
  } else {
    iSetElement(std::move(base), std::move(slice), std::move(value), context_);
  }
}

void Analyzer::assignToStarred(const expr_ty starred, AnalysisResult value) {
  assignToTarget(starred->v.Starred.value, std::move(value));
}

void Analyzer::analyze() {
  try {
    visitMod(root_);
  } catch (const StrictModuleUserException<BaseStrictObject>& exc) {
    processUnhandledUserException(exc);
  }
}

void Analyzer::analyzeFunction(
    std::vector<stmt_ty> body,
    SymtableEntry entry,
    std::unique_ptr<DictType> callArgs) {
  auto scope = Analyzer::scopeFactory(std::move(entry), std::move(callArgs));
  // enter function body scope
  auto scopeManager = stack_.enterScope(std::move(scope));
  visitStmtSeq(std::move(body));
}

void Analyzer::processUnhandledUserException(
    const StrictModuleUserException<BaseStrictObject>& exc) {
  std::shared_ptr<const BaseStrictObject> wrappedObject =
      std::static_pointer_cast<const BaseStrictObject>(exc.getWrapped());

  // TODO: get args from the "args" field of the wrapped object
  context_.errorSink->error<StrictModuleUnhandledException>(
      exc.getLineno(),
      exc.getCol(),
      exc.getFilename(),
      exc.getScopeName(),
      wrappedObject->getTypeRef().getDisplayName(),
      std::vector<std::string>(),
      exc.getCause());
}
// TryFinallyManager
TryFinallyManager::TryFinallyManager(Analyzer& analyzer, asdl_seq* finalbody)
    : analyzer_(analyzer), finalbody_(finalbody) {}

TryFinallyManager::~TryFinallyManager() {
  analyzer_.visitStmtSeq(finalbody_);
}

} // namespace strictmod
