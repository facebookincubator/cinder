// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/ast_preprocessor.h"

#include "StrictModules/pycore_dependencies.h"
#include "StrictModules/pystrictmodule.h"

namespace strictmod {
// Scope
PreprocessorScope::PreprocessorScope(bool isSlot) : isSlotifiedClass_(isSlot) {}
bool PreprocessorScope::isSlotifiedClass() const {
  return isSlotifiedClass_;
}

// Preprocessor
Preprocessor::Preprocessor(mod_ty root, astToResultT* astMap, PyArena* arena)
    : root_(root), astMap_(astMap), scopes_(), arena_(arena) {}

void Preprocessor::preprocess() {
  visitMod(root_);
}

// module level
void Preprocessor::visitStmtSeq(const asdl_stmt_seq* seq) {
  for (int i = 0; i < asdl_seq_LEN(seq); i++) {
    stmt_ty elt = reinterpret_cast<stmt_ty>(asdl_seq_GET(seq, i));
    visitStmt(elt);
  }
}

void Preprocessor::visitClassDef(const stmt_ty stmt) {
  auto it = astMap_->find(stmt);
  bool hasAttrs = it != astMap_->end() && it->second->hasRewritterAttrs();
  bool hasSlots = hasAttrs && !it->second->getRewriterAttrs().isSlotDisabled();
  scopes_.emplace_back(hasSlots);
  visitStmtSeq(stmt->v.ClassDef.body);
  scopes_.pop_back();
  if (!hasAttrs) {
    return;
  }
  std::vector<expr_ty> newDecorators;
  auto& attr = it->second->getRewriterAttrs();
  if (attr.isMutable()) {
    newDecorators.emplace_back(makeName(Ci_MUTABLE_DECORATOR));
  }
  const auto& extra_slots = attr.getExtraSlots();
  if (!extra_slots.empty()) {
    newDecorators.emplace_back(
        makeNameCall(Ci_EXTRA_SLOTS_DECORATOR, extra_slots));
  }
  if (attr.isLooseSlots()) {
    newDecorators.emplace_back(makeName(Ci_LOOSE_SLOTS_DECORATOR));
  }
  if (hasSlots) {
    newDecorators.emplace_back(makeName(Ci_ENABLE_SLOTS_DECORATOR));
  }
  auto& classDef = stmt->v.ClassDef;
  asdl_expr_seq* decorators = classDef.decorator_list;
  classDef.decorator_list = withNewDecorators(decorators, newDecorators);
}

void Preprocessor::visitFunctionLikeHelper(
    void* node,
    asdl_stmt_seq* body,
    asdl_expr_seq* decs) {
  scopes_.emplace_back(false);
  visitStmtSeq(body);
  scopes_.pop_back();
  if (scopes_.empty() || !scopes_.back().isSlotifiedClass()) {
    return;
  }
  auto it = astMap_->find(node);
  if (it == astMap_->end() || !it->second->hasRewritterAttrs()) {
    return;
  }
  RewriterAttrs attr = it->second->getRewriterAttrs();
  if (attr.hasCachedProperty()) {
    size_t len = asdl_seq_LEN(decs);
    int toRemove = -1; // remove the original cached prop decorator
    bool isAsync = false;
    for (size_t i = 0; i < len; ++i) {
      expr_ty dec = reinterpret_cast<expr_ty>(asdl_seq_GET(decs, i));
      auto decIt = astMap_->find(dec);
      if (decIt == astMap_->end() || !decIt->second->hasRewritterAttrs()) {
        continue;
      }
      RewriterAttrs decAttr = decIt->second->getRewriterAttrs();
      CachedPropertyKind propKind = decAttr.getCachedPropKind();
      if (propKind != CachedPropertyKind::kNone) {
        toRemove = i;
        isAsync = propKind == CachedPropertyKind::kCachedAsync;
        break;
      }
    }
    if (toRemove >= 0) {
      // We need to remove the original decorator and
      // add a new one. Just replace the old one with new one
      auto isAsyncObj = Ref<>::create(isAsync ? Py_True : Py_False);
      asdl_expr_seq* args = makeCallArgs({isAsyncObj.release()});
      expr_ty call = makeCall(Ci_CACHED_PROP_DECORATOR, args);
      asdl_seq_SET(decs, toRemove, call);
    }
  }
}

void Preprocessor::visitFunctionDef(const stmt_ty stmt) {
  auto func = stmt->v.FunctionDef;
  visitFunctionLikeHelper(stmt, func.body, func.decorator_list);
}

void Preprocessor::visitAsyncFunctionDef(const stmt_ty stmt) {
  auto func = stmt->v.AsyncFunctionDef;
  visitFunctionLikeHelper(stmt, func.body, func.decorator_list);
}

// helpers
/** obj2ast_object copied from Python-ast.c */
static int obj2ast_object(PyObject* obj, PyObject** out, PyArena* arena) {
  if (obj == Py_None)
    obj = NULL;
  if (obj) {
    if (_PyArena_AddPyObject(arena, obj) < 0) {
      *out = NULL;
      return -1;
    }
    Py_INCREF(obj);
  }
  *out = obj;
  return 0;
}

expr_ty Preprocessor::makeName(const char* name) {
  PyObject* nameObj = PyUnicode_FromString(name);
  // need to transfer ownership to the arena
  PyObject* nameAst;
  int res = obj2ast_object(nameObj, &nameAst, arena_);
  if (res != 0) {
    Py_CLEAR(nameObj);
    throw std::runtime_error("error obtaining AST from node");
  }
  Py_CLEAR(nameObj);
  return _PyAST_Name(nameAst, Load, 0, 0, 0, 0, arena_);
}

expr_ty Preprocessor::makeNameCall(
    const char* name,
    const std::vector<std::string>& args) {
  size_t size = args.size();
  std::vector<PyObject*> argObjs;
  argObjs.reserve(size);
  for (auto& a : args) {
    argObjs.emplace_back(PyUnicode_FromString(a.c_str()));
  }
  asdl_expr_seq* argsSeq = makeCallArgs(argObjs);
  return makeCall(name, argsSeq);
}

expr_ty Preprocessor::makeCall(const char* name, asdl_expr_seq* args) {
  expr_ty nameNode = makeName(name);
  return _PyAST_Call(
      nameNode, args, _Py_asdl_keyword_seq_new(0, arena_), 0, 0, 0, 0, arena_);
}

asdl_expr_seq* Preprocessor::makeCallArgs(const std::vector<PyObject*>& args) {
  size_t size = args.size();
  auto argsSeq = _Py_asdl_expr_seq_new(size, arena_);
  for (size_t i = 0; i < size; ++i) {
    PyObject* argAst;
    int res = obj2ast_object(args[i], &argAst, arena_);
    if (res != 0) {
      Py_XDECREF(args[i]);
      throw std::runtime_error("error obtaining AST from node");
    }
    Py_XDECREF(args[i]);
    expr_ty arg = _PyAST_Constant(argAst, NULL, 0, 0, 0, 0, arena_);
    asdl_seq_SET(argsSeq, i, arg);
  }
  return argsSeq;
}

asdl_expr_seq* Preprocessor::withNewDecorators(
    asdl_expr_seq* decs,
    const std::vector<expr_ty>& newDecs) {
  size_t oldSize = asdl_seq_LEN(decs);
  size_t addSize = newDecs.size();
  auto newDecsSeq = _Py_asdl_expr_seq_new(oldSize + addSize, arena_);
  for (size_t i = 0; i < oldSize; ++i) {
    asdl_seq_SET(newDecsSeq, i, asdl_seq_GET(decs, i));
  }
  for (size_t i = 0; i < addSize; ++i) {
    asdl_seq_SET(newDecsSeq, i + oldSize, newDecs[i]);
  }
  return newDecsSeq;
}

// the rest are just boilerplate
// statements
void Preprocessor::visitImport(const stmt_ty) {}
void Preprocessor::visitImportFrom(const stmt_ty) {}
void Preprocessor::visitAssign(const stmt_ty) {}
void Preprocessor::visitExprStmt(const stmt_ty) {}
void Preprocessor::visitReturn(const stmt_ty) {}
void Preprocessor::visitPass(const stmt_ty) {}
void Preprocessor::visitDelete(const stmt_ty) {}
void Preprocessor::visitAugAssign(const stmt_ty) {}
void Preprocessor::visitAnnAssign(const stmt_ty) {}
void Preprocessor::visitFor(const stmt_ty) {}
void Preprocessor::visitWhile(const stmt_ty) {}
void Preprocessor::visitIf(const stmt_ty) {}
void Preprocessor::visitWith(const stmt_ty) {}
void Preprocessor::visitRaise(const stmt_ty) {}
void Preprocessor::visitTry(const stmt_ty) {}
void Preprocessor::visitAssert(const stmt_ty) {}
void Preprocessor::visitBreak(const stmt_ty) {}
void Preprocessor::visitContinue(const stmt_ty) {}
void Preprocessor::visitGlobal(const stmt_ty) {}
void Preprocessor::visitMatch(const stmt_ty) {}
// expressions
void Preprocessor::visitConstant(const expr_ty) {}
void Preprocessor::visitName(const expr_ty) {}
void Preprocessor::visitAttribute(const expr_ty) {}
void Preprocessor::visitCall(const expr_ty) {}
void Preprocessor::visitSet(const expr_ty) {}
void Preprocessor::visitList(const expr_ty) {}
void Preprocessor::visitTuple(const expr_ty) {}
void Preprocessor::visitDict(const expr_ty) {}
void Preprocessor::visitBinOp(const expr_ty) {}
void Preprocessor::visitUnaryOp(const expr_ty) {}
void Preprocessor::visitCompare(const expr_ty) {}
void Preprocessor::visitBoolOp(const expr_ty) {}
void Preprocessor::visitNamedExpr(const expr_ty) {}
void Preprocessor::visitSubscript(const expr_ty) {}
void Preprocessor::visitStarred(const expr_ty) {}
void Preprocessor::visitLambda(const expr_ty) {}
void Preprocessor::visitIfExp(const expr_ty) {}
void Preprocessor::visitListComp(const expr_ty) {}
void Preprocessor::visitSetComp(const expr_ty) {}
void Preprocessor::visitDictComp(const expr_ty) {}
void Preprocessor::visitGeneratorExp(const expr_ty) {}
void Preprocessor::visitAwait(const expr_ty) {}
void Preprocessor::visitYield(const expr_ty) {}
void Preprocessor::visitYieldFrom(const expr_ty) {}
void Preprocessor::visitFormattedValue(const expr_ty) {}
void Preprocessor::visitJoinedStr(const expr_ty) {}
// defaults
void Preprocessor::defaultVisitExpr() {}
void Preprocessor::defaultVisitStmt() {}
void Preprocessor::defaultVisitMod() {}
// context
PreprocessorContextManager Preprocessor::updateContext(stmt_ty) {
  return {};
}
PreprocessorContextManager Preprocessor::updateContext(expr_ty) {
  return {};
}
PreprocessorContextManager Preprocessor::updateContext(mod_ty) {
  return {};
}
} // namespace strictmod
