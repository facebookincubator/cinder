// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef STRICTM_SCOPE_IMPL_H
#define STRICTM_SCOPE_IMPL_H
#include <stdexcept>
//------------------------Scope----------------------------
namespace strictmod {
template <typename TVar, typename TScopeData>
TVar& Scope<TVar, TScopeData>::operator[](const std::string& key) {
  return (*vars_)[key];
}

template <typename TVar, typename TScopeData>
TVar& Scope<TVar, TScopeData>::at(const std::string& key) {
  return vars_->at(key);
}

template <typename TVar, typename TScopeData>
bool Scope<TVar, TScopeData>::erase(const std::string& key) {
  return vars_->erase(key) > 0;
}

template <typename TVar, typename TScopeData>
bool Scope<TVar, TScopeData>::contains(const std::string& key) const {
  return vars_->find(key) != vars_->end();
}

//------------------------ScopeStack----------------------------
template <typename TVar, typename TScopeData>
TVar& ScopeStack<TVar, TScopeData>::operator[](const std::string& key) {
  const std::string mangledKey = mangleName(key);
  const Symbol& symbol = scopes_.back()->getSTEntry().getSymbol(mangledKey);
  if (symbol.is_global()) {
    return (*scopes_.front())[key];
  } else if (symbol.is_nonlocal()) {
    for (auto it = std::next(scopes_.rbegin()); it != scopes_.rend(); ++it) {
      auto scope = *it;
      if (!scope->isClassScope() && scope->contains(key)) {
        return (*scope)[key];
      }
    }
  }
  return (*scopes_.back())[key];
}

template <typename TVar, typename TScopeData>
const TVar* ScopeStack<TVar, TScopeData>::at(const std::string& key) const {
  // reading in python scope is different from writing.
  // search from innermost to outmost scope, skipping over non-leaf class scope
  auto currentScope = scopes_.back();
  if (currentScope->contains(key)) {
    return &currentScope->at(key);
  }

  for (auto it = std::next(scopes_.rbegin()); it != scopes_.rend(); ++it) {
    auto scope = *it;
    if (!scope->isClassScope() && scope->contains(key)) {
      return &scope->at(key);
    }
  }
  return nullptr;
}

template <typename TVar, typename TScopeData>
bool ScopeStack<TVar, TScopeData>::erase(const std::string& key) {
  const std::string mangledKey = mangleName(key);
  const Symbol& symbol = scopes_.back()->getSTEntry().getSymbol(mangledKey);
  if (symbol.is_global()) {
    return scopes_.front()->erase(key);
  } else if (symbol.is_nonlocal()) {
    for (auto it = std::next(scopes_.rbegin()); it != scopes_.rend(); ++it) {
      auto scope = *it;
      if (!scope->isClassScope() && scope->contains(key)) {
        return scope->erase(key);
      }
    }
  }
  return scopes_.back()->erase(key);
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
inline ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAst(stmt_ty key) {
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
  return enterScopeByAstBody(key, std::move(className));
}

template <typename TVar, typename TScopeData>
inline ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAst(mod_ty key) {
  return enterScopeByAstBody(key);
}

template <typename TVar, typename TScopeData>
inline ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAst(expr_ty key) {
  return enterScopeByAstBody(key);
}

template <typename TVar, typename TScopeData>
ScopeManager<TVar, TScopeData>
ScopeStack<TVar, TScopeData>::enterScopeByAstBody(
    void* key,
    std::optional<std::string> className) {
  SymtableEntry entry = symbols_.entryFromAst(key);
  auto scope = scopeFactory_(
      entry, std::make_shared<std::unordered_map<std::string, TVar>>());
  return ScopeManager<TVar, TScopeData>(
      *this, std::shared_ptr(std::move(scope)), className);
}

template <typename TVar, typename TScopeData>
ScopeManager<TVar, TScopeData> ScopeStack<TVar, TScopeData>::enterScope(
    std::unique_ptr<Scope<TVar, TScopeData>> scope,
    std::optional<std::string> currentClass) {
  return ScopeManager<TVar, TScopeData>(
      *this, std::shared_ptr(std::move(scope)), std::move(currentClass));
}
} // namespace strictmod
#endif // STRICTM_SCOPE_IMPL_H
