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
      stack_,
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
