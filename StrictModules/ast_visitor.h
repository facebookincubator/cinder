#ifndef __STRICTM_AST_VISITOR_H__
#define __STRICTM_AST_VISITOR_H__

#include "StrictModules/py_headers.h"

namespace strictmod {

template <typename ET, typename ST, typename MT, typename TAnalyzer>
class ASTVisitor {
  /**
  T is the return type of each analysis
  TAnalyzer is what actually implements the visit_ methods
  */
 protected:
  MT visitMod(const mod_ty mod) {
    auto context = static_cast<TAnalyzer*>(this)->updateContext(mod);
    switch (mod->kind) {
      case Module_kind:
        return static_cast<TAnalyzer*>(this)->visitStmtSeq(mod->v.Module.body);
      case Interactive_kind:
      case Expression_kind:
      case FunctionType_kind:
      case Suite_kind:
      default:
        return static_cast<TAnalyzer*>(this)->defaultVisitMod();
    }
  }

  ST visitStmt(const stmt_ty stmt) {
    auto context = static_cast<TAnalyzer*>(this)->updateContext(stmt);
    switch (stmt->kind) {
      case Import_kind:
        return static_cast<TAnalyzer*>(this)->visitImport(stmt);
      case FunctionDef_kind:
      case AsyncFunctionDef_kind:
      case ClassDef_kind:
      case Return_kind:
      case Delete_kind:
        break;
      case Assign_kind:
        return static_cast<TAnalyzer*>(this)->visitAssign(stmt);
      case AugAssign_kind:
      case AnnAssign_kind:
      case For_kind:
      case AsyncFor_kind:
      case While_kind:
      case If_kind:
      case With_kind:
      case AsyncWith_kind:
      case Raise_kind:
      case Try_kind:
      case Assert_kind:
      case ImportFrom_kind:
      case Global_kind:
      case Nonlocal_kind:
        break;
      case Expr_kind:
        return static_cast<TAnalyzer*>(this)->visitExprStmt(stmt);
      case Pass_kind:
      case Break_kind:
      case Continue_kind:
        break;
    }
    return static_cast<TAnalyzer*>(this)->defaultVisitStmt();
  }

  ET visitExpr(const expr_ty expr) {
    auto context = static_cast<TAnalyzer*>(this)->updateContext(expr);
    switch (expr->kind) {
      case BoolOp_kind:
      case NamedExpr_kind:
      case BinOp_kind:
      case UnaryOp_kind:
      case Lambda_kind:
      case IfExp_kind:
      case Dict_kind:
        break;
      case Set_kind:
        return static_cast<TAnalyzer*>(this)->visitSet(expr);
      case ListComp_kind:
      case SetComp_kind:
      case DictComp_kind:
      case GeneratorExp_kind:
      case Await_kind:
      case Yield_kind:
      case YieldFrom_kind:
      case Compare_kind:
        break;
      case Call_kind:
        return static_cast<TAnalyzer*>(this)->visitCall(expr);
      case FormattedValue_kind:
      case JoinedStr_kind:
        break;
      case Constant_kind:
        return static_cast<TAnalyzer*>(this)->visitConstant(expr);
      case Attribute_kind:
        return static_cast<TAnalyzer*>(this)->visitAttribute(expr);
      case Subscript_kind:
      case Starred_kind:
        break;
      case Name_kind:
        return static_cast<TAnalyzer*>(this)->visitName(expr);
      case List_kind:
        return static_cast<TAnalyzer*>(this)->visitList(expr);
      case Tuple_kind:
        return static_cast<TAnalyzer*>(this)->visitTuple(expr);
      default:
        break;
    }
    return static_cast<TAnalyzer*>(this)->defaultVisitExpr();
  }
};
} // namespace strictmod
#endif // __STRICTM_AST_VISITOR_H__
