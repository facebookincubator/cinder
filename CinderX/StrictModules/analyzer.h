// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "StrictModules/Objects/base_object.h"
#include "StrictModules/Objects/dict_object.h"
#include "StrictModules/ast_visitor.h"
#include "StrictModules/caller_context.h"
#include "StrictModules/error_sink.h"
#include "StrictModules/py_headers.h"
#include "StrictModules/scope.h"
#include "StrictModules/sequence_map.h"

#include <memory>
#include <optional>

namespace strictmod {
namespace compiler {
class ModuleLoader;
} // namespace compiler

class AnalysisScopeData;
typedef std::shared_ptr<objects::BaseStrictObject> AnalysisResult;
typedef ScopeStack<std::shared_ptr<BaseStrictObject>, AnalysisScopeData> EnvT;

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
  using astToResultT = strictmod::objects::astToResultT;
  typedef Scope<std::shared_ptr<BaseStrictObject>, AnalysisScopeData> ScopeT;
  typedef sequence_map<std::string, std::shared_ptr<BaseStrictObject>> DictType;

 public:
  Analyzer(
      mod_ty root,
      compiler::ModuleLoader* loader,
      Symtable table,
      BaseErrorSink* errors,
      std::string filename,
      std::string modName,
      std::string scopeName,
      std::shared_ptr<StrictModuleObject> caller,
      bool futureAnnotations = false);

  Analyzer(
      mod_ty root,
      compiler::ModuleLoader* loader,
      Symtable table,
      BaseErrorSink* errors,
      std::string filename,
      std::string modName,
      std::string scopeName,
      std::weak_ptr<StrictModuleObject> caller,
      bool futureAnnotations = false);

  Analyzer(
      mod_ty root,
      compiler::ModuleLoader* loader,
      Symtable table,
      std::shared_ptr<DictType> toplevelNS,
      BaseErrorSink* errors,
      std::string filename,
      std::string modName,
      std::string scopeName,
      std::shared_ptr<StrictModuleObject> caller,
      bool futureAnnotations = false);

  // function analyzer, root will be nullptr
  Analyzer(
      compiler::ModuleLoader* loader,
      BaseErrorSink* errors,
      std::string filename,
      std::string modName,
      std::string scopeName,
      std::weak_ptr<StrictModuleObject> caller,
      int lineno,
      int col,
      const EnvT& closure,
      bool futureAnnotations = false);

  void analyze();
  void log();
  void analyzeFunction(
      std::vector<stmt_ty> body,
      SymtableEntry entry,
      std::unique_ptr<objects::DictType> callArgs,
      AnalysisResult firstArg);
  AnalysisResult analyzeExecOrEval(
      int callerLino,
      int callerCol,
      std::shared_ptr<objects::StrictDict> globals,
      std::shared_ptr<objects::StrictDict> locals);
  // module level
  void visitStmtSeq(const asdl_stmt_seq* seq);
  void visitStmtSeq(std::vector<stmt_ty> seq);
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
  void visitMatch(const stmt_ty stmt);
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
  AnalysisResult visitCompare(const expr_ty expr);
  AnalysisResult visitBoolOp(const expr_ty expr);
  AnalysisResult visitNamedExpr(const expr_ty expr);
  AnalysisResult visitSubscript(const expr_ty expr);
  AnalysisResult visitSlice(const expr_ty expr);
  AnalysisResult visitStarred(const expr_ty expr);
  AnalysisResult visitLambda(const expr_ty expr);
  AnalysisResult visitIfExp(const expr_ty expr);
  AnalysisResult visitListComp(const expr_ty expr);
  AnalysisResult visitSetComp(const expr_ty expr);
  AnalysisResult visitDictComp(const expr_ty expr);
  AnalysisResult visitGeneratorExp(const expr_ty expr);
  AnalysisResult visitAwait(const expr_ty expr);
  AnalysisResult visitYield(const expr_ty expr);
  AnalysisResult visitYieldFrom(const expr_ty expr);
  AnalysisResult visitFormattedValue(const expr_ty expr);
  AnalysisResult visitJoinedStr(const expr_ty expr);
  // defaults
  AnalysisResult defaultVisitExpr();
  void defaultVisitStmt();
  void defaultVisitMod();
  // context manager
  AnalysisContextManager updateContext(stmt_ty stmt);
  AnalysisContextManager updateContext(expr_ty expr);
  AnalysisContextManager updateContext(mod_ty mod);

  std::unique_ptr<astToResultT> passAstToResultsMap() {
    return std::move(astToResults_);
  }

  static std::unique_ptr<ScopeT> scopeFactory(
      SymtableEntry entry,
      std::shared_ptr<DictType> map);

  template <typename... Args>
  void log(const char* fmt, Args... args);

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
  /* global context for currently pending exceptions */
  AnalysisResult currentExceptionContext_;
  /* name of the current module
   * For function bodies, this is where the function is defined
   */
  std::string modName_;
  // map ast nodes to analysis result
  std::unique_ptr<astToResultT> astToResults_;

  std::shared_ptr<BaseStrictObject> handleFromListHelper(
      std::shared_ptr<BaseStrictObject> fromMod,
      const std::string& name);

  std::vector<std::shared_ptr<BaseStrictObject>> visitListLikeHelper(
      asdl_expr_seq* elts);

  objects::DictDataT visitDictUnpackHelper(expr_ty keyExpr);

  AnalysisResult visitFunctionDefHelper(
      std::string name,
      arguments_ty args,
      asdl_stmt_seq* body,
      asdl_expr_seq* decoratorList,
      expr_ty returns,
      string typeComment,
      int lineno,
      int col_offset,
      void* node,
      bool isAsync);

  AnalysisResult visitAnnotationHelper(expr_ty annotation);

  void addToDunderAnnotationsHelper(expr_ty target, AnalysisResult value);
  void visitArgHelper(arg_ty arg, objects::DictDataT& annotations);
  void visitArgHelper(
      std::vector<std::string>& args,
      arg_ty arg,
      objects::DictDataT& annotations);

  bool visitExceptionHandlerHelper(
      asdl_excepthandler_seq* handlers,
      AnalysisResult exc);

  template <typename CB, typename... Args>
  void visitGeneratorHelper(
      expr_ty node,
      asdl_comprehension_seq* generators,
      CB callback,
      Args... targets);

  template <typename CB, typename... Args>
  void visitGeneratorHelperInner(
      AnalysisResult iter,
      expr_ty iterTarget,
      asdl_expr_seq* ifs,
      const std::vector<comprehension_ty>& comps,
      std::size_t idx,
      CB callback,
      Args... targets);

  bool checkGeneratorIfHelper(asdl_expr_seq* ifs);

  AnalysisResult callMagicalSuperHelper(AnalysisResult func);

  std::optional<AnalysisResult> getFromScope(const std::string& name);

  void assignToTarget(
      const expr_ty target,
      std::shared_ptr<BaseStrictObject> value);
  void assignToName(
      const expr_ty name,
      std::shared_ptr<BaseStrictObject> value);
  void assignToListLike(
      asdl_expr_seq* elts,
      std::shared_ptr<BaseStrictObject> value);
  void assignToAttribute(
      const expr_ty attr,
      std::shared_ptr<BaseStrictObject> value);
  void assignToSubscript(
      const expr_ty subscr,
      std::shared_ptr<BaseStrictObject> value);
  void assignToStarred(
      const expr_ty starred,
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
      StrictModuleUserException<BaseStrictObject>& exc);
};

class TryFinallyManager {
 public:
  TryFinallyManager(Analyzer& analyzer, asdl_stmt_seq* finalbody);
  ~TryFinallyManager();

 private:
  Analyzer& analyzer_;
  asdl_stmt_seq* finalbody_;
};

// Scope Data for strict module analysis
class AnalysisScopeData {
 public:
  AnalysisScopeData(
      const CallerContext& caller,
      AnalysisResult callFirstArg = nullptr,
      AnalysisResult alternateDict = nullptr)
      : caller_(caller),
        callFirstArg_(std::move(callFirstArg)),
        prepareDict_(std::move(alternateDict)) {}

  AnalysisScopeData(
      AnalysisResult callFirstArg = nullptr,
      AnalysisResult alternateDict = nullptr)
      : caller_(),
        callFirstArg_(std::move(callFirstArg)),
        prepareDict_(std::move(alternateDict)) {}

  const AnalysisResult& getCallFirstArg() const;
  void set(const std::string& key, AnalysisResult value);
  AnalysisResult at(const std::string& key);
  bool erase(const std::string& key);
  bool contains(const std::string& key) const;
  bool hasAlternativeDict() const {
    return prepareDict_ != nullptr;
  }

 private:
  std::optional<CallerContext> caller_;
  AnalysisResult callFirstArg_;
  AnalysisResult prepareDict_; // dict provided by __prepare__
};
} // namespace strictmod
