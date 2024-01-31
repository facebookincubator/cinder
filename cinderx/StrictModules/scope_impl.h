// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <stdexcept>
//------------------------Scope----------------------------
namespace strictmod {
template <typename TVar, typename TScopeData>
void Scope<TVar, TScopeData>::set(const std::string& key, TVar value) {
  (*vars_)[key] = std::forward<TVar>(value);
}

template <typename TVar, typename TScopeData>
TVar Scope<TVar, TScopeData>::at(const std::string& key) {
  return vars_->at(key);
}

template <typename TVar, typename TScopeData>
bool Scope<TVar, TScopeData>::erase(const std::string& key) {
  return vars_->erase(key) > 0;
}

template <typename TVar, typename TScopeData>
bool Scope<TVar, TScopeData>::contains(const std::string& key) const {
  return vars_->find(key) != vars_->map_end();
}

//------------------------ScopeStack----------------------------
template <typename TVar, typename TScopeData>
void ScopeStack<TVar, TScopeData>::set(const std::string& key, TVar value) {
  const std::string mangledKey = mangleName(key);
  const Symbol& symbol = scopes_.back()->getSTEntry().getSymbol(mangledKey);
  if (symbol.is_global()) {
    getGlobalScope()->set(key, std::move(value));
    return;
  } else if (symbol.is_nonlocal()) {
    for (auto it = std::next(scopes_.rbegin()); it != getBuiltinScopeRevIt();
         ++it) {
      auto scope = *it;
      if (!scope->isClassScope() && scope->contains(key)) {
        scope->set(key, std::move(value));
        return;
      }
    }
  }
  scopes_.back()->set(key, std::move(value));
}

template <typename TVar, typename TScopeData>
std::optional<TVar> ScopeStack<TVar, TScopeData>::at(
    const std::string& key) const {
  // reading in python scope is different from writing.
  // search from innermost to outmost scope, skipping over non-leaf class scope
  auto currentScope = scopes_.back();
  if (currentScope->contains(key)) {
    return currentScope->at(key);
  }

  for (auto it = std::next(scopes_.rbegin()); it != scopes_.rend(); ++it) {
    auto scope = *it;
    if (!scope->isClassScope() && scope->contains(key)) {
      return scope->at(key);
    }
  }
  return std::nullopt;
}

template <typename TVar, typename TScopeData>
std::tuple<std::optional<TVar>, Scope<TVar, TScopeData>*>
ScopeStack<TVar, TScopeData>::at_and_scope(const std::string& key) {
  // reading in python scope is different from writing.
  // search from innermost to outmost scope, skipping over non-leaf class scope
  auto currentScope = scopes_.back();
  if (currentScope->contains(key)) {
    return std::make_tuple(currentScope->at(key), currentScope.get());
  }

  for (auto it = std::next(scopes_.rbegin()); it != scopes_.rend(); ++it) {
    auto scope = *it;
    if (!scope->isClassScope() && scope->contains(key)) {
      return std::make_tuple(scope->at(key), scope.get());
    }
  }
  return std::make_tuple(std::nullopt, nullptr);
}

template <typename TVar, typename TScopeData>
bool ScopeStack<TVar, TScopeData>::erase(const std::string& key) {
  const std::string mangledKey = mangleName(key);
  const Symbol& symbol = scopes_.back()->getSTEntry().getSymbol(mangledKey);
  if (symbol.is_global()) {
    return getGlobalScope()->erase(key);
  } else if (symbol.is_nonlocal()) {
    for (auto it = std::next(scopes_.rbegin()); it != getBuiltinScopeRevIt();
         ++it) {
      auto scope = *it;
      if (!scope->isClassScope() && scope->contains(key)) {
        return scope->erase(key);
      }
    }
  }
  return scopes_.back()->erase(key);
}

template <typename TVar, typename TScopeData>
void ScopeStack<TVar, TScopeData>::clear() {
  scopes_.clear();
}

template <typename TVar, typename TScopeData>
bool ScopeStack<TVar, TScopeData>::isGlobal(const std::string& key) const {
  if (scopes_.size() <= 2) {
    return true;
  }
  const std::string mangledKey = mangleName(key);
  const Symbol& symbol = scopes_.back()->getSTEntry().getSymbol(mangledKey);
  return symbol.is_global();
}

template <typename TVar, typename TScopeData>
bool ScopeStack<TVar, TScopeData>::isNonLocal(const std::string& key) const {
  const std::string mangledKey = mangleName(key);
  const Symbol& symbol = scopes_.back()->getSTEntry().getSymbol(mangledKey);
  return symbol.is_nonlocal();
}

template <typename TVar, typename TScopeData>
inline void ScopeStack<TVar, TScopeData>::push(
    std::shared_ptr<Scope<TVar, TScopeData>> scope) {
  scopes_.push_back(scope);
}

template <typename TVar, typename TScopeData>
inline void ScopeStack<TVar, TScopeData>::pop() {
  scopes_.pop_back();
}

template <typename TVar, typename TScopeData>
ScopeStack<TVar, TScopeData> ScopeStack<TVar, TScopeData>::getFunctionScope() {
  ScopeVector newScope;
  newScope.reserve(scopes_.size());
  for (auto& scope : scopes_) {
    if (scope->isInvisible() || !scope->isClassScope()) {
      newScope.push_back(scope);
    }
  }
  return ScopeStack<TVar, TScopeData>(
      std::move(newScope), symbols_, scopeFactory_);
}

template <typename TVar, typename TScopeData>
inline ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAst(stmt_ty key, TMapPtr vars) {
  std::optional<std::string> className = std::nullopt;
  switch (key->kind) {
    case ClassDef_kind: {
      auto pyClassName = key->v.ClassDef.name;
      className = std::string(PyUnicode_AsUTF8(pyClassName));
      break;
    }
    default:
      break;
  }
  return enterScopeByAstBody(key, std::move(vars), std::move(className));
}

template <typename TVar, typename TScopeData>
inline ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAst(mod_ty key, TMapPtr vars) {
  return enterScopeByAstBody(key, std::move(vars));
}

template <typename TVar, typename TScopeData>
inline ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAst(expr_ty key, TMapPtr vars) {
  return enterScopeByAstBody(key, std::move(vars));
}

template <typename TVar, typename TScopeData>
ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAstBody(
    void* key,
    TMapPtr vars,
    std::optional<std::string> className) {
  SymtableEntry entry = symbols_.entryFromAst(key);
  if (vars == nullptr) {
    vars = std::make_shared<ScopeDictType>();
  }
  auto scope = scopeFactory_(entry, std::move(vars));
  return ScopeManager<TVar, TScopeData>(
      *this, std::shared_ptr(std::move(scope)), std::move(className));
}

template <typename TVar, typename TScopeData>
ScopeManager<TVar, TScopeData> ScopeStack<TVar, TScopeData>::enterScope(
    std::unique_ptr<Scope<TVar, TScopeData>> scope,
    std::optional<std::string> currentClass) {
  return ScopeManager<TVar, TScopeData>(
      *this, std::shared_ptr(std::move(scope)), std::move(currentClass));
}

template <typename TVar, typename TScopeData>
inline bool ScopeStack<TVar, TScopeData>::isClassScope() const {
  return scopes_.back()->isClassScope();
}

template <typename TVar, typename TScopeData>
inline bool ScopeStack<TVar, TScopeData>::isGlobalScope() const {
  return scopes_.size() <= 2;
}

template <typename TVar, typename TScopeData>
bool ScopeStack<TVar, TScopeData>::localContains(const std::string& key) const {
  return scopes_.back()->contains(key);
}
template <typename TVar, typename TScopeData>
void ScopeStack<TVar, TScopeData>::localSet(std::string key, TVar value) {
  scopes_.back()->set(key, std::move(value));
}
} // namespace strictmod
