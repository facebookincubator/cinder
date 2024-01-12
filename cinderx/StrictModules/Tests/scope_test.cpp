// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Tests/test.h"
#include "StrictModules/scope.h"

#include <utility>
class ScopeTest : public PythonTest {};

using strictmod::Scope;
using strictmod::ScopeManager;
using strictmod::ScopeStack;
using strictmod::SymtableEntry;

std::unique_ptr<Scope<int, std::nullptr_t>> factory(
    strictmod::SymtableEntry entry,
    std::shared_ptr<sequence_map<std::string, int>> map) {
  return std::make_unique<Scope<int, std::nullptr_t>>(entry, map, nullptr);
}

TEST_F(ScopeTest, TESTSCOPECREATE) {
  const char* s = R"*(
x = 1
class B:
    x = 2
    def f(self):
        global x
        x = 3
        y = 4
        del y
        y = 5
  )*";
  PyArena* arena = _PyArena_New();

  std::optional<strictmod::AstAndSymbols> result =
      strictmod::readFromSource(s, "<string>", Py_file_input, arena);
  ASSERT_NE(result, std::nullopt);
  EXPECT_NE(result.value().ast, nullptr);
  EXPECT_NE(result.value().symbols, nullptr);
  EXPECT_EQ(result.value().futureAnnotations, false);
  // Creating scopes from symtable should not raise runtime errors
  mod_ty ast = result.value().ast;
  strictmod::Symtable table(std::move(result.value().symbols));
  SymtableEntry entry = table.entryFromAst(ast);
  auto map = std::make_shared<sequence_map<std::string, int>>();
  // not used but scopestack always expect a builtin scope
  auto builtinScope = std::shared_ptr(factory(entry, map));
  auto topScope = std::shared_ptr(factory(entry, map));

  std::vector<std::shared_ptr<Scope<int, std::nullptr_t>>> scopeVector;
  scopeVector.emplace_back(builtinScope);
  scopeVector.emplace_back(topScope);
  ScopeStack<int, std::nullptr_t> scopes(
      std::move(scopeVector), table, factory);
  // global scope
  const std::string x("x");
  const std::string y("y");
  auto topXSymbol = topScope->getSTEntry().getSymbol(x);
  EXPECT_EQ(topXSymbol.is_global(), true);
  EXPECT_EQ(topXSymbol.is_nonlocal(), false);
  EXPECT_EQ(scopes.getCurrentClass(), std::nullopt);
  EXPECT_EQ(scopes.mangleName(x), "x");
  scopes.set(x, 1);
  EXPECT_EQ(scopes.at(x).value(), 1);

  // class B scope
  asdl_stmt_seq* seq = ast->v.Module.body;
  stmt_ty classDef = reinterpret_cast<stmt_ty>(asdl_seq_GET(seq, 1));
  {
    ScopeManager classB = scopes.enterScopeByAst(classDef);
    EXPECT_EQ(scopes.getCurrentClass().value(), "B");
    auto scopeB = classB.getScope();
    auto classBXSymbol = scopeB->getSTEntry().getSymbol(x);
    EXPECT_EQ(classBXSymbol.is_global(), false);
    EXPECT_EQ(classBXSymbol.is_local(), true);
    EXPECT_EQ(classBXSymbol.is_nonlocal(), false);
    EXPECT_EQ(scopeB->isClassScope(), true);
    EXPECT_EQ(scopeB->isFunctionScope(), false);
    EXPECT_EQ(scopes.at(x).value(), 1);
    scopes.set(x, 2); // x = 2
    EXPECT_EQ(scopes.at(x).value(), 2);
    EXPECT_EQ(topScope->at(x), 1);

    // function f scope
    asdl_stmt_seq* classSeq = classDef->v.ClassDef.body;
    stmt_ty funcDef = reinterpret_cast<stmt_ty>(asdl_seq_GET(classSeq, 1));
    {
      ScopeManager funcF = scopes.enterScopeByAst(funcDef);
      EXPECT_EQ(scopes.getCurrentClass().value(), "B");
      auto scopeF = funcF.getScope();
      auto funcFXSymbol = scopeF->getSTEntry().getSymbol(x);
      auto funcFYSymbol = scopeF->getSTEntry().getSymbol(y);
      EXPECT_EQ(funcFXSymbol.is_global(), true);
      EXPECT_EQ(funcFXSymbol.is_local(), false);
      EXPECT_EQ(funcFXSymbol.is_nonlocal(), false);
      EXPECT_EQ(funcFYSymbol.is_global(), false);
      EXPECT_EQ(funcFYSymbol.is_local(), true);
      EXPECT_EQ(funcFYSymbol.is_nonlocal(), false);
      EXPECT_EQ(scopeF->isClassScope(), false);
      EXPECT_EQ(scopeF->isFunctionScope(), true);

      scopes.set(x, 3); // x = 3
      EXPECT_EQ(scopes.at(x).value(), 3);
      EXPECT_EQ(topScope->at(x), 3);
      EXPECT_EQ(scopeB->at(x), 2);

      scopes.set(y, 4); // y = 4
      EXPECT_EQ(scopes.at(y).value(), 4);
      EXPECT_EQ(topScope->contains(y), false);
      EXPECT_EQ(scopeB->contains(y), false);

      scopes.erase(y); // del y
      EXPECT_EQ(scopes.at(y), std::nullopt);
      EXPECT_EQ(topScope->contains(y), false);
      EXPECT_EQ(scopeB->contains(y), false);

      scopes.set(y, 5); // y = 5
      EXPECT_EQ(scopes.at(y).value(), 5);
      EXPECT_EQ(topScope->contains(y), false);
      EXPECT_EQ(scopeB->contains(y), false);
    } // exit function f scope
    EXPECT_EQ(scopes.at(x).value(), 2);
    EXPECT_EQ(scopes.at(y), std::nullopt);
    EXPECT_EQ(scopes.getCurrentClass().value(), "B");

  } // exit class B scope
  EXPECT_EQ(scopes.at(x).value(), 3);
  EXPECT_EQ(scopes.at(y), std::nullopt);
  EXPECT_EQ(scopes.getCurrentClass(), std::nullopt);

  if (arena != nullptr) {
    _PyArena_Free(arena);
  }
}
