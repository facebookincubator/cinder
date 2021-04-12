#ifndef __STRICTM_ANALYZER_H__
#define __STRICTM_ANALYZER_H__

#include <memory>
#include <optional>

#include "StrictModules/py_headers.h"

#include "StrictModules/Objects/base_object.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/ast_visitor.h"
#include "StrictModules/caller_context.h"
#include "StrictModules/error_sink.h"
#include "StrictModules/scope.h"

namespace strictmod {
namespace compiler {
class ModuleLoader;
} // namespace compiler

typedef std::shared_ptr<objects::BaseStrictObject> AnalysisResult;

class AnalysisContextManager {
 public:
  AnalysisContextManager(CallerContext& ctx, int newLine, int newCol);
  ~AnalysisContextManager();

 private:
  CallerContext* context_;
  int oldLine_;
  int oldCol_;
};

class Analyzer : public ASTVisitor<AnalysisResult, void, void, Analyzer> {
  using BaseStrictObject = objects::BaseStrictObject;
  typedef Scope<std::shared_ptr<BaseStrictObject>, std::nullptr_t> ScopeT;
  typedef std::unordered_map<std::string, std::shared_ptr<BaseStrictObject>>
      DictType;

 public:
  Analyzer(
      mod_ty root,
      compiler::ModuleLoader* loader,
      Symtable table,
      BaseErrorSink* errors,
      std::string filename,
      std::string scopeName,
      std::shared_ptr<StrictModuleObject> caller);

  Analyzer(
      mod_ty root,
      compiler::ModuleLoader* loader,
      Symtable table,
      std::shared_ptr<DictType> toplevelNS,
      BaseErrorSink* errors,
      std::string filename,
      std::string scopeName,
      std::shared_ptr<StrictModuleObject> caller);

  void analyze();
  // module level
  void visitStmtSeq(const asdl_seq* seq);
  // statements
  void visitImport(const stmt_ty stmt);
  void visitAssign(const stmt_ty stmt);
  void visitExprStmt(const stmt_ty stmt);
  // expressions
  AnalysisResult visitConstant(const expr_ty expr);
  AnalysisResult visitName(const expr_ty expr);
  AnalysisResult visitAttribute(const expr_ty expr);
  AnalysisResult visitCall(const expr_ty expr);
  AnalysisResult visitSet(const expr_ty expr);
  AnalysisResult visitList(const expr_ty expr);
  AnalysisResult visitTuple(const expr_ty expr);
  // defaults
  AnalysisResult defaultVisitExpr();
  void defaultVisitStmt();
  void defaultVisitMod();
  // context manager
  AnalysisContextManager updateContext(stmt_ty stmt);
  AnalysisContextManager updateContext(expr_ty expr);
  AnalysisContextManager updateContext(mod_ty mod);

  static std::unique_ptr<ScopeT> scopeFactory(
      SymtableEntry entry,
      std::shared_ptr<DictType> map) {
    return std::make_unique<ScopeT>(entry, std::move(map), nullptr);
  }

 private:
  /* C ast is allocated by the CPython parser into a PyArena.
     The ast visitor and the abstract objects do not own the AST.
  */
  mod_ty root_;
  /* non owning ptr to loader.
  one single loader is guaranteed to be alive during the entire run
   */
  compiler::ModuleLoader* loader_;
  /* caller context */
  CallerContext context_;
  /* scope stack managing the current analysis */
  ScopeStack<std::shared_ptr<BaseStrictObject>, std::nullptr_t> stack_;

  std::vector<std::shared_ptr<BaseStrictObject>> visitListLikeHelper(
      asdl_seq* elts);

  void assignToTarget(
      const expr_ty target,
      std::shared_ptr<BaseStrictObject> value);
  void assignToName(
      const expr_ty name,
      std::shared_ptr<BaseStrictObject> value);

  AnalysisContextManager updateContextHelper(int lineno, int col) {
    return AnalysisContextManager(context_, lineno, col);
  }

  template <typename T, typename... Args>
  void error(Args... args) {
    context_.error<T>(std::move(args)...);
  }

  void raiseUnimplemented() {
    error<StrictModuleNotImplementedException>();
  }

  void processUnhandledUserException(
      const StrictModuleUserException<BaseStrictObject>& exc);
};

} // namespace strictmod

#endif //__STRICTM_ANALYZER_H__
