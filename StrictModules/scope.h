#ifndef STRICTM_SCOPE_H
#define STRICTM_SCOPE_H
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "StrictModules/symbol_table.h"
namespace strictmod {

std::string mangle(const std::string& className, const std::string& name);

template <typename TVar, typename TScopeData>
class Scope {
 public:
  Scope(SymtableEntry scope, TScopeData data)
      : scope_(scope),
        vars_(std::make_shared<std::unordered_map<std::string, TVar>>()),
        data_(data),
        invisible_(false) {}

  Scope(
      SymtableEntry scope,
      std::shared_ptr<std::unordered_map<std::string, TVar>> vars,
      TScopeData data)
      : scope_(scope), vars_(vars), data_(data), invisible_(false) {}

  TVar& operator[](const std::string& key);
  TVar& at(const std::string& key);
  bool erase(const std::string& key);
  bool contains(const std::string& key) const;

  const SymtableEntry& getSTEntry() const {
    return scope_;
  }

  bool isClassScope() const {
    return scope_.isClassScope();
  }

 private:
  SymtableEntry scope_;
  std::shared_ptr<std::unordered_map<std::string, TVar>> vars_;
  TScopeData data_;
  bool invisible_;
};

template <typename TVar, typename TScopeData>
class ScopeManager;

template <typename TVar, typename TScopeData>
class ScopeStack {
  typedef std::function<std::unique_ptr<Scope<TVar, TScopeData>>(
      SymtableEntry,
      std::shared_ptr<std::unordered_map<std::string, TVar>>)>
      ScopeFactory;

  typedef std::vector<std::shared_ptr<Scope<TVar, TScopeData>>> ScopeVector;

 public:
  ScopeStack(ScopeVector scopes, Symtable symbols, ScopeFactory factory)
      : scopes_(std::move(scopes)),
        symbols_(symbols),
        scopeFactory_(factory),
        currentClass_() {}

  ScopeStack(
      Symtable symbols,
      ScopeFactory factory,
      std::shared_ptr<Scope<TVar, TScopeData>> topScope)
      : scopes_(), symbols_(symbols), scopeFactory_(factory), currentClass_() {
    scopes_.push_back(topScope);
  }

  ScopeStack(
      Symtable symbols,
      ScopeFactory factory,
      std::unique_ptr<Scope<TVar, TScopeData>> topScope)
      : ScopeStack(symbols, factory, std::shared_ptr(std::move(topScope))) {}

  /* use this for setting */
  TVar& operator[](const std::string& key);
  /* use this for reading, return nullptr if key doesn't exist */
  const TVar* at(const std::string& key) const;
  bool erase(const std::string& key);

  void push(std::shared_ptr<Scope<TVar, TScopeData>> scope);
  void pop();

  ScopeManager<TVar, TScopeData> enterScopeByAst(stmt_ty key);
  ScopeManager<TVar, TScopeData> enterScopeByAst(mod_ty key);
  ScopeManager<TVar, TScopeData> enterScopeByAst(expr_ty key);

  std::optional<std::string> getCurrentClass(void) {
    return currentClass_;
  }
  void setCurrentClass(std::optional<std::string> className) {
    currentClass_ = std::move(className);
  }
  std::string mangleName(std::string name) const {
    if (!currentClass_) {
      return name;
    }
    return mangle(currentClass_.value(), std::move(name));
  }

 private:
  ScopeVector scopes_;
  Symtable symbols_;
  ScopeFactory scopeFactory_;
  std::optional<std::string> currentClass_;

  ScopeManager<TVar, TScopeData> enterScopeByAstBody(
      void* key,
      std::optional<std::string> className = std::nullopt);
};

template <typename TVar, typename TScopeData>
class ScopeManager {
 public:
  ScopeManager(
      ScopeStack<TVar, TScopeData>& parent,
      std::shared_ptr<Scope<TVar, TScopeData>> scope,
      std::optional<std::string> currentClass)
      : parent_(parent), scope_(scope), oldClass_(parent_.getCurrentClass()) {
    parent_.push(scope);
    if (currentClass) {
      parent_.setCurrentClass(currentClass);
    }
  }

  ~ScopeManager() {
    parent_.pop();
    parent_.setCurrentClass(oldClass_);
  }

  std::shared_ptr<Scope<TVar, TScopeData>> getScope() {
    return scope_;
  }

 private:
  ScopeStack<TVar, TScopeData>& parent_;
  std::shared_ptr<Scope<TVar, TScopeData>> scope_;
  std::optional<std::string> oldClass_;
};

} // namespace strictmod

#include "StrictModules/scope_impl.h"

#define STRICTM_SCOPE_H
#endif // STRICTM_SCOPE_H
