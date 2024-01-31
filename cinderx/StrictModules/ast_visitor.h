// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/py_headers.h"

namespace strictmod {

template <typename ET, typename ST, typename MT, typename TAnalyzer>
class ASTVisitor {
  /**
  T is the return type of each analysis
  TAnalyzer is what actually implements the visit_ methods
  */
 protected:
  MT visitMod(const mod_ty mod) {
    [[maybe_unused]] auto context =
        static_cast<TAnalyzer*>(this)->updateContext(mod);
    switch (mod->kind) {
      case Module_kind:
        return static_cast<TAnalyzer*>(this)->visitStmtSeq(mod->v.Module.body);
      case Interactive_kind:
      case Expression_kind:
      case FunctionType_kind:
      default:
        return static_cast<TAnalyzer*>(this)->defaultVisitMod();
    }
  }

  ST visitStmt(const stmt_ty stmt) {
    [[maybe_unused]] auto context =
        static_cast<TAnalyzer*>(this)->updateContext(stmt);
    switch (stmt->kind) {
      case Import_kind:
        return static_cast<TAnalyzer*>(this)->visitImport(stmt);
      case FunctionDef_kind:
        return static_cast<TAnalyzer*>(this)->visitFunctionDef(stmt);
      case AsyncFunctionDef_kind:
        return static_cast<TAnalyzer*>(this)->visitAsyncFunctionDef(stmt);
      case ClassDef_kind:
        return static_cast<TAnalyzer*>(this)->visitClassDef(stmt);
      case Return_kind:
        return static_cast<TAnalyzer*>(this)->visitReturn(stmt);
      case Delete_kind:
        return static_cast<TAnalyzer*>(this)->visitDelete(stmt);
      case Assign_kind:
        return static_cast<TAnalyzer*>(this)->visitAssign(stmt);
      case AugAssign_kind:
        return static_cast<TAnalyzer*>(this)->visitAugAssign(stmt);
      case AnnAssign_kind:
        return static_cast<TAnalyzer*>(this)->visitAnnAssign(stmt);
      case For_kind:
        return static_cast<TAnalyzer*>(this)->visitFor(stmt);
      case AsyncFor_kind:
        break;
      case While_kind:
        return static_cast<TAnalyzer*>(this)->visitWhile(stmt);
      case If_kind:
        return static_cast<TAnalyzer*>(this)->visitIf(stmt);
      case With_kind:
        return static_cast<TAnalyzer*>(this)->visitWith(stmt);
      case AsyncWith_kind:
        break;
      case Raise_kind:
        return static_cast<TAnalyzer*>(this)->visitRaise(stmt);
      case Try_kind:
        return static_cast<TAnalyzer*>(this)->visitTry(stmt);
      case Assert_kind:
        return static_cast<TAnalyzer*>(this)->visitAssert(stmt);
      case ImportFrom_kind:
        return static_cast<TAnalyzer*>(this)->visitImportFrom(stmt);
      case Global_kind:
      case Nonlocal_kind:
        return static_cast<TAnalyzer*>(this)->visitGlobal(stmt);
      case Expr_kind:
        return static_cast<TAnalyzer*>(this)->visitExprStmt(stmt);
      case Pass_kind:
        return static_cast<TAnalyzer*>(this)->visitPass(stmt);
      case Break_kind:
        return static_cast<TAnalyzer*>(this)->visitBreak(stmt);
      case Continue_kind:
        return static_cast<TAnalyzer*>(this)->visitContinue(stmt);
      case Match_kind:
        return static_cast<TAnalyzer*>(this)->visitMatch(stmt);
    }
    return static_cast<TAnalyzer*>(this)->defaultVisitStmt();
  }

  ET visitExpr(const expr_ty expr) {
    [[maybe_unused]] auto context =
        static_cast<TAnalyzer*>(this)->updateContext(expr);
    switch (expr->kind) {
      case BoolOp_kind:
        return static_cast<TAnalyzer*>(this)->visitBoolOp(expr);
      case NamedExpr_kind:
        return static_cast<TAnalyzer*>(this)->visitNamedExpr(expr);
      case BinOp_kind:
        return static_cast<TAnalyzer*>(this)->visitBinOp(expr);
      case UnaryOp_kind:
        return static_cast<TAnalyzer*>(this)->visitUnaryOp(expr);
      case Lambda_kind:
        return static_cast<TAnalyzer*>(this)->visitLambda(expr);
      case IfExp_kind:
        return static_cast<TAnalyzer*>(this)->visitIfExp(expr);
      case Dict_kind:
        return static_cast<TAnalyzer*>(this)->visitDict(expr);
      case Set_kind:
        return static_cast<TAnalyzer*>(this)->visitSet(expr);
      case ListComp_kind:
        return static_cast<TAnalyzer*>(this)->visitListComp(expr);
      case SetComp_kind:
        return static_cast<TAnalyzer*>(this)->visitSetComp(expr);
      case DictComp_kind:
        return static_cast<TAnalyzer*>(this)->visitDictComp(expr);
      case GeneratorExp_kind:
        return static_cast<TAnalyzer*>(this)->visitGeneratorExp(expr);
      case Await_kind:
        return static_cast<TAnalyzer*>(this)->visitAwait(expr);
      case Yield_kind:
        return static_cast<TAnalyzer*>(this)->visitYield(expr);
      case YieldFrom_kind:
        return static_cast<TAnalyzer*>(this)->visitYieldFrom(expr);
      case Compare_kind:
        return static_cast<TAnalyzer*>(this)->visitCompare(expr);
      case Call_kind:
        return static_cast<TAnalyzer*>(this)->visitCall(expr);
      case FormattedValue_kind:
        return static_cast<TAnalyzer*>(this)->visitFormattedValue(expr);
      case JoinedStr_kind:
        return static_cast<TAnalyzer*>(this)->visitJoinedStr(expr);
      case Constant_kind:
        return static_cast<TAnalyzer*>(this)->visitConstant(expr);
      case Attribute_kind:
        return static_cast<TAnalyzer*>(this)->visitAttribute(expr);
      case Subscript_kind:
        return static_cast<TAnalyzer*>(this)->visitSubscript(expr);
      case Starred_kind:
        return static_cast<TAnalyzer*>(this)->visitStarred(expr);
      case Name_kind:
        return static_cast<TAnalyzer*>(this)->visitName(expr);
      case List_kind:
        return static_cast<TAnalyzer*>(this)->visitList(expr);
      case Tuple_kind:
        return static_cast<TAnalyzer*>(this)->visitTuple(expr);
      case Slice_kind:
        return static_cast<TAnalyzer*>(this)->visitSlice(expr);
      default:
        break;
    }
    return static_cast<TAnalyzer*>(this)->defaultVisitExpr();
  }
};
} // namespace strictmod
