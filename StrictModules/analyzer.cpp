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
using objects::BaseStrictObject;
// AnalysisContextManager

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
      futureAnnotations_(futureAnnotations) {}

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
      futureAnnotations_(futureAnnotations) {}

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
  std::shared_ptr<BaseStrictObject> value = visitExpr(assignStmt.value);
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

void Analyzer::visitArgHelper(arg_ty arg, objects::DictDataT& annotations) {
  if (arg->annotation == nullptr) {
    return;
  }
  AnalysisResult key = std::make_shared<objects::StrictString>(
      objects::StrType(), context_.caller, arg->arg);
  if (futureAnnotations_) {
    PyObject* annotationStr = _PyAST_ExprAsUnicode(arg->annotation);
    annotations[std::move(key)] = std::make_shared<objects::StrictString>(
        objects::StrType(), context_.caller, annotationStr);
    Py_DECREF(annotationStr);
  } else {
    annotations[std::move(key)] = visitExpr(arg->annotation);
  }
}

void Analyzer::visitArgHelper(
    std::vector<std::string>& args,
    arg_ty arg,
    objects::DictDataT& annotations) {
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
  for (int _i = 0; _i < bodySize; ++_i) {
    bodyVec.push_back(reinterpret_cast<stmt_ty>(asdl_seq_GET(body, _i)));
  }
  // arguments
  std::vector<std::string> posonlyArgs;
  std::vector<std::string> posArgs;
  std::vector<std::string> kwonlyArgs;
  std::optional<std::string> varArg;
  std::optional<std::string> kwVarArg;
  objects::DictDataT annotations;

  int posonlySize = asdl_seq_LEN(args->posonlyargs);
  posonlyArgs.reserve(posonlySize);
  for (int _i = 0; _i < posonlySize; ++_i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->posonlyargs, _i));
    visitArgHelper(posonlyArgs, a, annotations);
  }

  int posSize = asdl_seq_LEN(args->args);
  posArgs.reserve(posSize);
  for (int _i = 0; _i < posSize; ++_i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->args, _i));
    visitArgHelper(posArgs, a, annotations);
  }

  if (args->vararg != nullptr) {
    varArg.emplace(PyUnicode_AsUTF8(args->vararg->arg));
    visitArgHelper(args->vararg, annotations);
  }

  int kwSize = asdl_seq_LEN(args->kwonlyargs);
  kwonlyArgs.reserve(kwSize);
  for (int _i = 0; _i < kwSize; ++_i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->kwonlyargs, _i));
    visitArgHelper(kwonlyArgs, a, annotations);
  }

  if (args->kwarg != nullptr) {
    kwVarArg.emplace(PyUnicode_AsUTF8(args->kwarg->arg));
    visitArgHelper(args->kwarg, annotations);
  }
  // argument defaults
  std::vector<std::shared_ptr<BaseStrictObject>> posDefaults;
  std::vector<std::shared_ptr<BaseStrictObject>> kwDefaults;

  int kwDefaultSize = asdl_seq_LEN(args->kw_defaults);
  kwDefaults.reserve(kwDefaultSize);
  for (int _i = 0; _i < kwDefaultSize; ++_i) {
    expr_ty d = reinterpret_cast<expr_ty>(asdl_seq_GET(args->kw_defaults, _i));
    if (d == nullptr) {
      kwDefaults.push_back(nullptr);
    } else {
      kwDefaults.push_back(visitExpr(d));
    }
  }

  int posDefaultSize = asdl_seq_LEN(args->defaults);
  posDefaults.reserve(posDefaultSize);
  for (int _i = 0; _i < posDefaultSize; ++_i) {
    expr_ty d = reinterpret_cast<expr_ty>(asdl_seq_GET(args->defaults, _i));
    posDefaults.push_back(visitExpr(d));
  }

  // annotations object
  if (returns != nullptr) {
    annotations[context_.makeStr("return")] = visitExpr(returns);
  }

  std::shared_ptr<objects::StrictDict> annotationsObj =
      std::make_shared<objects::StrictDict>(
          objects::DictObjectType(), context_.caller, std::move(annotations));

  std::shared_ptr<BaseStrictObject> func(new objects::StrictFunction(
      objects::FunctionType(),
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
  for (int _i = decoratorSize - 1; _i >= 0; --_i) {
    expr_ty dec = reinterpret_cast<expr_ty>(asdl_seq_GET(decoratorList, _i));
    AnalysisResult decObj = visitExpr(dec);
    // call decorators, fix lineno
    {
      auto contextManager = updateContextHelper(dec->lineno, dec->col_offset);
      std::shared_ptr<BaseStrictObject> obj =
          objects::iCall(decObj, {func}, objects::kEmptyArgNames, context_);
      func.swap(obj);
    }
  }

  // put function in scope
  stack_[std::move(funcName)] = std::move(func);
}

void Analyzer::visitReturn(const stmt_ty stmt) {
  expr_ty returnV = stmt->v.Return.value;
  if (returnV == nullptr) {
    throw(objects::FunctionReturnException(objects::NoneObject()));
  }
  AnalysisResult returnVal = visitExpr(stmt->v.Return.value);
  throw(objects::FunctionReturnException(std::move(returnVal)));
}

static std::shared_ptr<objects::DictType> prepareToDictHelper(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  auto dictObj = std::dynamic_pointer_cast<objects::StrictDict>(obj);
  if (dictObj == nullptr) {
    caller.raiseTypeError(
        "__prepare__ must return a dict, not {} object",
        obj->getTypeRef().getName());
  }
  std::shared_ptr<objects::DictType> resultPtr =
      std::make_shared<objects::DictType>();
  objects::DictType result = *resultPtr;
  result.reserve(dictObj->getData().size());
  for (auto& item : dictObj->getData()) {
    auto strObjKey =
        std::dynamic_pointer_cast<objects::StrictString>(item.first);
    if (strObjKey != nullptr) {
      result[strObjKey->getValue()] = item.second;
    }
  }
  return resultPtr;
}

static std::shared_ptr<BaseStrictObject> strDictToObjHelper(
    std::shared_ptr<objects::DictType> dict,
    const CallerContext& caller) {
  objects::DictDataT dictObj;
  dictObj.reserve(dict->size());
  for (auto& item : *dict) {
    dictObj[caller.makeStr(item.first)] = item.second;
  }
  return std::make_shared<objects::StrictDict>(
      objects::DictObjectType(), caller.caller, std::move(dictObj));
}

void Analyzer::visitClassDef(const stmt_ty stmt) {
  auto classDef = stmt->v.ClassDef;
  // Step 1, identify metaclass
  std::shared_ptr<objects::StrictType> metaclass;
  std::vector<std::shared_ptr<BaseStrictObject>> bases =
      visitListLikeHelper(classDef.bases);
  // register metaclass if found in keyword args
  // find if any base class has metaclass
  int kwSize = asdl_seq_LEN(classDef.keywords);
  std::vector<std::string> kwargKeys;
  std::vector<std::shared_ptr<BaseStrictObject>> kwargValues;
  kwargKeys.reserve(kwSize);
  kwargValues.reserve(kwSize);
  for (int i = 0; i < kwSize; ++i) {
    keyword_ty kw =
        reinterpret_cast<keyword_ty>(asdl_seq_GET(classDef.keywords, i));
    AnalysisResult kwVal = visitExpr(kw->value);
    if (PyUnicode_CompareWithASCIIString(kw->arg, "metaclass") == 0) {
      // TODO: do we need to worry about metaclass that not subclass of type?
      auto kwValTyp =
          objects::assertStaticCast<objects::StrictType>(std::move(kwVal));
      metaclass = std::move(kwValTyp);
    } else {
      kwargKeys.push_back(PyUnicode_AsUTF8(kw->arg));
      kwargValues.push_back(std::move(kwVal));
    }
  }

  bool replacedBases = false;
  std::shared_ptr<BaseStrictObject> origBases;

  if (metaclass == nullptr && !bases.empty()) {
    // check if __mro_entries__ is defined for any bases
    std::vector<std::shared_ptr<BaseStrictObject>> newBases;
    newBases.reserve(bases.size());
    auto baseTuple = std::make_shared<objects::StrictTuple>(
        objects::TupleType(), context_.caller, bases);

    for (auto& base : bases) {
      auto baseType = base->getType();
      if (baseType == objects::UnknownType() ||
          std::dynamic_pointer_cast<objects::StrictType>(base) != nullptr) {
        newBases.push_back(base);
        continue;
      }
      auto mroEntriesFunc = baseType->typeLookup("__mro_entries__", context_);
      if (mroEntriesFunc != nullptr) {
        auto newBaseEntries = objects::iCall(
            mroEntriesFunc,
            {base, baseTuple},
            objects::kEmptyArgNames,
            context_);
        if (newBaseEntries != nullptr) {
          auto newBaseVec =
              objects::iGetElementsVec(std::move(newBaseEntries), context_);
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
      auto baseTyp = std::dynamic_pointer_cast<objects::StrictType>(base);
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
    metaclass = objects::TypeType();
  }

  // Step 2, run __prepare__ if exists, creating namespace ns
  std::shared_ptr<DictType> ns = std::make_shared<DictType>();
  std::string className = PyUnicode_AsUTF8(classDef.name);
  auto classNameObj = context_.makeStr(className);
  auto baseTupleObj = std::make_shared<objects::StrictTuple>(
      objects::TupleType(), context_.caller, bases);
  if (metaclass->getType() == objects::UnknownType()) {
    context_.error<UnknownValueCallException>(metaclass->getDisplayName());
  } else {
    auto prepareFunc =
        objects::iLoadAttr(metaclass, "__prepare__", nullptr, context_);
    if (prepareFunc != nullptr) {
      auto nsObj = objects::iCall(
          prepareFunc,
          {classNameObj, baseTupleObj},
          objects::kEmptyArgNames,
          context_);
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
  std::vector<std::shared_ptr<BaseStrictObject>> classCallArg{
      std::move(classNameObj), std::move(baseTupleObj), classDict};
  classCallArg.insert(
      classCallArg.end(),
      std::move_iterator(kwargValues.begin()),
      std::move_iterator(kwargValues.end()));

  auto classObj = objects::iCall(
      metaclass, std::move(classCallArg), std::move(kwargKeys), context_);
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
      std::shared_ptr<BaseStrictObject> tempClass =
          objects::iCall(decObj, {classObj}, objects::kEmptyArgNames, context_);
      std::swap(classObj, tempClass);
    }
  }
  // Step 8, populate __class__ in hidden scope defined in step 3
  // TODO
  stack_[std::move(className)] = std::move(classObj);
}

void Analyzer::visitPass(const stmt_ty) {}

// Expressions
AnalysisResult Analyzer::visitConstant(const expr_ty expr) {
  auto constant = expr->v.Constant;
  if (PyLong_CheckExact(constant.value)) {
    auto value = std::make_shared<objects::StrictInt>(
        objects::IntType(), context_.caller, constant.value);
    return value;
  }
  if (PyUnicode_CheckExact(constant.value)) {
    auto value = std::make_shared<objects::StrictString>(
        objects::StrType(), context_.caller, constant.value);
    return value;
  }
  if (PyFloat_CheckExact(constant.value)) {
    auto value = std::make_shared<objects::StrictFloat>(
        objects::FloatType(), context_.caller, constant.value);
    return value;
  }
  if (constant.value == Py_None) {
    return objects::NoneObject();
  }
  return defaultVisitExpr();
}

AnalysisResult Analyzer::visitName(const expr_ty expr) {
  auto name = expr->v.Name;
  assert(name.ctx != Store && name.ctx != Del && name.ctx != AugStore);
  const char* nameStr = PyUnicode_AsUTF8(name.id);
  auto value = stack_.at(nameStr);
  if (!value) {
    // decide whether to raise NameError or UnboundLocalError base on
    // declaration
    context_.raiseExceptionStr(
        objects::NameErrorType(), "name {} is not defined", nameStr);
  }
  return *value;
}

AnalysisResult Analyzer::visitAttribute(const expr_ty expr) {
  auto attribute = expr->v.Attribute;
  AnalysisResult value = visitExpr(attribute.value);
  assert(value != nullptr);
  const char* attrName = PyUnicode_AsUTF8(attribute.attr);
  if (attribute.ctx == Del) {
    objects::iDelAttr(value, attrName, context_);
    return objects::NoneObject();
  } else {
    auto result = objects::iLoadAttr(value, attrName, nullptr, context_);
    if (!result) {
      context_.raiseExceptionStr(
          objects::AttributeErrorType(),
          "{} object has no attribute {}",
          value->getTypeRef().getName(),
          attrName);
    }
    return result;
  }
}

AnalysisResult Analyzer::visitCall(const expr_ty expr) {
  auto call = expr->v.Call;
  AnalysisResult func = visitExpr(call.func);
  assert(func != nullptr);

  auto argsSeq = call.args;
  auto kwargsSeq = call.keywords;
  int argsLen = asdl_seq_LEN(argsSeq);
  int kwargsLen = asdl_seq_LEN(kwargsSeq);

  std::vector<std::shared_ptr<BaseStrictObject>> args;
  std::vector<std::string> argNames;
  args.reserve(argsLen + kwargsLen);
  argNames.reserve(kwargsLen);

  for (int i = 0; i < argsLen; ++i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(argsSeq, i));
    args.push_back(visitExpr(argExpr));
  }
  for (int i = 0; i < kwargsLen; ++i) {
    keyword_ty kw = reinterpret_cast<keyword_ty>(asdl_seq_GET(kwargsSeq, i));
    args.push_back(visitExpr(kw->value));
    argNames.emplace_back(PyUnicode_AsUTF8(kw->arg));
  }
  return objects::iCall(func, std::move(args), std::move(argNames), context_);
}

std::vector<std::shared_ptr<BaseStrictObject>> Analyzer::visitListLikeHelper(
    asdl_seq* elts) {
  int eltsLen = asdl_seq_LEN(elts);
  std::vector<std::shared_ptr<BaseStrictObject>> data;
  data.reserve(eltsLen);
  for (int i = 0; i < eltsLen; ++i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
    if (argExpr->kind == Starred_kind) {
      // TODO
    } else {
      data.push_back(visitExpr(argExpr));
    }
  }
  return data;
}

AnalysisResult Analyzer::visitSet(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.Set.elts);
  objects::SetDataT data(
      std::move_iterator(v.begin()), std::move_iterator(v.end()));
  std::shared_ptr<BaseStrictObject> obj = std::make_shared<objects::StrictSet>(
      objects::SetType(), context_.caller, std::move(data));
  return obj;
}

AnalysisResult Analyzer::visitList(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.List.elts);
  std::shared_ptr<BaseStrictObject> obj = std::make_shared<objects::StrictList>(
      objects::ListType(), context_.caller, std::move(v));
  return obj;
}

AnalysisResult Analyzer::visitTuple(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.Tuple.elts);
  std::shared_ptr<BaseStrictObject> obj =
      std::make_shared<objects::StrictTuple>(
          objects::TupleType(), context_.caller, std::move(v));
  return obj;
}

objects::DictDataT Analyzer::visitDictUnpackHelper(expr_ty valueExpr) {
  AnalysisResult unpacked = visitExpr(valueExpr);
  auto keys = objects::iGetElementsVec(unpacked, context_);

  objects::DictDataT map(keys.size());
  for (auto& k : keys) {
    auto value = objects::iGetElement(unpacked, k, context_);
    map[k] = std::move(value);
  }
  return map;
}

AnalysisResult Analyzer::visitDict(const expr_ty expr) {
  auto dict = expr->v.Dict;
  int keysLen = asdl_seq_LEN(dict.keys);
  assert(keysLen == asdl_seq_LEN(dict.values));

  objects::DictDataT map(keysLen);

  for (int i = 0; i < keysLen; ++i) {
    expr_ty keyExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(dict.keys, i));
    expr_ty valueExpr =
        reinterpret_cast<expr_ty>(asdl_seq_GET(dict.values, i));
    if (keyExpr == nullptr) {
      // handle unpacking
      objects::DictDataT unpackedMap = visitDictUnpackHelper(valueExpr);
      map.insert(
          std::move_iterator(unpackedMap.begin()),
          std::move_iterator(unpackedMap.end()));
    } else {
      AnalysisResult kResult = visitExpr(keyExpr);
      AnalysisResult vResult = visitExpr(valueExpr);
      map[kResult] = vResult;
    }
  }
  return std::make_shared<objects::StrictDict>(
      objects::DictObjectType(), context_.caller, std::move(map));
}

AnalysisResult Analyzer::visitBinOp(const expr_ty expr) {
  AnalysisResult left = visitExpr(expr->v.BinOp.left);
  AnalysisResult right = visitExpr(expr->v.BinOp.right);
  return objects::iDoBinOp(
      std::move(left), std::move(right), expr->v.BinOp.op, context_);
}

AnalysisResult Analyzer::visitUnaryOp(const expr_ty expr) {
  AnalysisResult value = visitExpr(expr->v.UnaryOp.operand);
  unaryop_ty op = expr->v.UnaryOp.op;
  if (op == Not) {
    auto result = objects::iGetTruthValue(std::move(value), context_);
    if (result->getType() == objects::UnknownType()) {
      return result;
    }
    return context_.makeBool(result == objects::StrictFalse());
  }
  return objects::iUnaryOp(std::move(value), op, context_);
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
    compareValue = objects::iBinCmpOp(leftObj, rightObj, op, context_);
    AnalysisResult compareBool =
        objects::iGetTruthValue(compareValue, context_);
    if (compareBool == objects::StrictFalse()) {
      // short circuit
      return compareValue;
    }
    if (compareBool->getType() == objects::UnknownType()) {
      // unknown result
      return compareValue;
    }
    leftObj = std::move(rightObj);
  }
  assert(compareValue != nullptr);
  return compareValue;
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
  return objects::makeUnknown(context_, "<unimplemented expr>");
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

void Analyzer::assignToTarget(
    const expr_ty target,
    std::shared_ptr<BaseStrictObject> value) {
  switch (target->kind) {
    case Name_kind:
      assignToName(target, value);
    default:
      return;
  }
}

void Analyzer::assignToName(
    const expr_ty target,
    std::shared_ptr<BaseStrictObject> value) {
  PyObject* id = target->v.Name.id;
  const char* name = PyUnicode_AsUTF8(id);
  stack_[name] = value;
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
    std::unique_ptr<objects::DictType> callArgs) {
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
} // namespace strictmod
