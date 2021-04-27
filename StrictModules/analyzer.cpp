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
    std::shared_ptr<StrictModuleObject> caller)
    : Analyzer(
          root,
          loader,
          table,
          std::make_shared<DictType>(),
          errors,
          std::move(filename),
          std::move(scopeName),
          caller) {}

Analyzer::Analyzer(
    mod_ty root,
    compiler::ModuleLoader* loader,
    Symtable table,
    std::shared_ptr<DictType> toplevelNS,
    BaseErrorSink* errors,
    std::string filename,
    std::string scopeName,
    std::shared_ptr<StrictModuleObject> caller)
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
          scopeFactory(table.entryFromAst(root_), std::move(toplevelNS))) {}

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
    for (int _i = 0; _i < asdl_seq_LEN(assignStmt.targets); ++_i) {
      expr_ty target =
          reinterpret_cast<expr_ty>(asdl_seq_GET(assignStmt.targets, _i));
      assignToTarget(target, value);
    }
  }
}

void Analyzer::visitExprStmt(const stmt_ty stmt) {
  expr_ty expr = stmt->v.Expr.value;
  visitExpr(expr);
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

  for (int _i = 0; _i < argsLen; ++_i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(argsSeq, _i));
    args.push_back(visitExpr(argExpr));
  }
  for (int _i = 0; _i < kwargsLen; ++_i) {
    keyword_ty kw = reinterpret_cast<keyword_ty>(asdl_seq_GET(kwargsSeq, _i));
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
  for (int _i = 0; _i < eltsLen; ++_i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, _i));
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

void Analyzer::visitStmtSeq(const asdl_seq* seq) {
  for (int _i = 0; _i < asdl_seq_LEN(seq); _i++) {
    stmt_ty elt = reinterpret_cast<stmt_ty>(asdl_seq_GET(seq, _i));
    visitStmt(elt);
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
