// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_ANALYZER_H__
#define __STRICTM_ANALYZER_H__

#include <memory>
#include <optional>

#include "StrictModules/py_headers.h"

#include "StrictModules/Objects/base_object.h"
#include "StrictModules/Objects/dict_object.h"
#include "StrictModules/ast_visitor.h"
#include "StrictModules/caller_context.h"
#include "StrictModules/error_sink.h"
#include "StrictModules/scope.h"

namespace strictmod {
namespace compiler {
class ModuleLoader;
} // namespace compiler

typedef std::shared_ptr<objects::BaseStrictObject> AnalysisResult;
typedef ScopeStack<std::shared_ptr<BaseStrictObject>, std::nullptr_t> EnvT;

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
      std::shared_ptr<StrictModuleObject> caller,
      bool futureAnnotations = false);

  Analyzer(
      mod_ty root,
      compiler::ModuleLoader* loader,
      Symtable table,
      std::shared_ptr<DictType> toplevelNS,
      BaseErrorSink* errors,
      std::string filename,
      std::string scopeName,
      std::shared_ptr<StrictModuleObject> caller,
      bool futureAnnotations = false);

  // function analyzer, root will be nullptr
  Analyzer(
      compiler::ModuleLoader* loader,
      BaseErrorSink* errors,
      std::string filename,
      std::string scopeName,
      std::weak_ptr<StrictModuleObject> caller,
      int lineno,
      int col,
      const EnvT& closure,
      bool futureAnnotations = false);

  void analyze();
  void analyzeFunction(
      std::vector<stmt_ty> body,
      SymtableEntry entry,
      std::unique_ptr<objects::DictType> callArgs);
  // module level
  void visitStmtSeq(const asdl_seq* seq);
  void visitStmtSeq(std::vector<stmt_ty> seq);
  // statements
  void visitImport(const stmt_ty stmt);
  void visitAssign(const stmt_ty stmt);
  void visitExprStmt(const stmt_ty stmt);
  void visitFunctionDef(const stmt_ty stmt);
  void visitAsyncFunctionDef(const stmt_ty stmt);
  void visitReturn(const stmt_ty stmt);
  // expressions
  AnalysisResult visitConstant(const expr_ty expr);
  AnalysisResult visitName(const expr_ty expr);
  AnalysisResult visitAttribute(const expr_ty expr);
  AnalysisResult visitCall(const expr_ty expr);
  AnalysisResult visitSet(const expr_ty expr);
  AnalysisResult visitList(const expr_ty expr);
  AnalysisResult visitTuple(const expr_ty expr);
  AnalysisResult visitDict(const expr_ty expr);
  AnalysisResult visitBinOp(const expr_ty expr);
  AnalysisResult visitUnaryOp(const expr_ty expr);
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
  EnvT stack_;
  /* whether annotations are treated as strings */
  bool futureAnnotations_; // use in visit annotations

  std::vector<std::shared_ptr<BaseStrictObject>> visitListLikeHelper(
      asdl_seq* elts);

  objects::DictDataT visitDictUnpackHelper(expr_ty keyExpr);

  void visitFunctionDefHelper(
      identifier name,
      arguments_ty args,
      asdl_seq* body,
      asdl_seq* decoratorList,
      expr_ty returns,
      string typeComment,
      stmt_ty node,
      bool isAsync);

  void visitArgHelper(arg_ty arg, objects::DictDataT& annotations);
  void visitArgHelper(
      std::vector<std::string>& args,
      arg_ty arg,
      objects::DictDataT& annotations);

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
