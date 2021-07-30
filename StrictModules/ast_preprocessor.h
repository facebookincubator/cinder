// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_AST_PREPROCESSOR_H__
#define __STRICTM_AST_PREPROCESSOR_H__

#include <memory>
#include <optional>

#include "StrictModules/Objects/base_object.h"
#include "StrictModules/py_headers.h"

#include "StrictModules/ast_visitor.h"

namespace strictmod {
using strictmod::objects::astToResultT;

class PreprocessorContextManager {};
class PreprocessorScope {
 public:
  PreprocessorScope(bool isSlot);
  bool isSlotifiedClass() const;

 private:
  bool isSlotifiedClass_;
};

class Preprocessor : public ASTVisitor<void, void, void, Preprocessor> {
 public:
  Preprocessor(mod_ty root, astToResultT* astMap, PyArena* arena);
  void preprocess();
  // module level
  void visitStmtSeq(const asdl_seq* seq);
  // statements
  void visitImport(const stmt_ty stmt);
  void visitImportFrom(const stmt_ty stmt);
  void visitAssign(const stmt_ty stmt);
  void visitExprStmt(const stmt_ty stmt);
  void visitFunctionDef(const stmt_ty stmt);
  void visitAsyncFunctionDef(const stmt_ty stmt);
  void visitReturn(const stmt_ty stmt);
  void visitClassDef(const stmt_ty stmt);
  void visitPass(const stmt_ty stmt);
  void visitDelete(const stmt_ty stmt);
  void visitAugAssign(const stmt_ty stmt);
  void visitAnnAssign(const stmt_ty stmt);
  void visitFor(const stmt_ty stmt);
  void visitWhile(const stmt_ty stmt);
  void visitIf(const stmt_ty stmt);
  void visitWith(const stmt_ty stmt);
  void visitRaise(const stmt_ty stmt);
  void visitTry(const stmt_ty stmt);
  void visitAssert(const stmt_ty stmt);
  void visitBreak(const stmt_ty stmt);
  void visitContinue(const stmt_ty stmt);
  void visitGlobal(const stmt_ty stmt);
  // expressions
  void visitConstant(const expr_ty expr);
  void visitName(const expr_ty expr);
  void visitAttribute(const expr_ty expr);
  void visitCall(const expr_ty expr);
  void visitSet(const expr_ty expr);
  void visitList(const expr_ty expr);
  void visitTuple(const expr_ty expr);
  void visitDict(const expr_ty expr);
  void visitBinOp(const expr_ty expr);
  void visitUnaryOp(const expr_ty expr);
  void visitCompare(const expr_ty expr);
  void visitBoolOp(const expr_ty expr);
  void visitNamedExpr(const expr_ty expr);
  void visitSubscript(const expr_ty expr);
  void visitStarred(const expr_ty expr);
  void visitLambda(const expr_ty expr);
  void visitIfExp(const expr_ty expr);
  void visitListComp(const expr_ty expr);
  void visitSetComp(const expr_ty expr);
  void visitDictComp(const expr_ty expr);
  void visitGeneratorExp(const expr_ty expr);
  void visitAwait(const expr_ty expr);
  void visitYield(const expr_ty expr);
  void visitYieldFrom(const expr_ty expr);
  void visitFormattedValue(const expr_ty expr);
  void visitJoinedStr(const expr_ty expr);
  // defaults
  void defaultVisitExpr();
  void defaultVisitStmt();
  void defaultVisitMod();
  // context
  PreprocessorContextManager updateContext(stmt_ty stmt);
  PreprocessorContextManager updateContext(expr_ty expr);
  PreprocessorContextManager updateContext(mod_ty mod);

 private:
  mod_ty root_;
  astToResultT* astMap_;
  std::vector<PreprocessorScope> scopes_;
  PyArena* arena_;

  void visitFunctionLikeHelper(void* node, asdl_seq* body, asdl_seq* decs);
  /** create a Name node using the given string */
  expr_ty makeName(const char* name);
  /** create a Call node with string arguments */
  expr_ty makeNameCall(const char* name, const std::vector<std::string>& args);
  /** create a Call node */
  expr_ty makeCall(const char* name, asdl_seq* args);
  /** Note that ownership of PyObject* in args are
   * transferred to the Call node
   */
  asdl_seq* makeCallArgs(const std::vector<PyObject*>& args);
  /** create a new decorator list containing those in `decs`
   * then in `newDecs`
   */
  asdl_seq* withNewDecorators(
      asdl_seq* decs,
      const std::vector<expr_ty>& newDecs);
};

} // namespace strictmod

#endif //__STRICTM_AST_PREPROCESSOR_H__
