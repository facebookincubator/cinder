// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "StrictModules/py_headers.h"
#include "StrictModules/symbol_table.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace strictmod {

struct AstAndSymbols {
  mod_ty ast;
  std::unique_ptr<PySymtable, PySymtableDeleter> symbols;
  bool futureAnnotations;
  bool parsed;

  AstAndSymbols(
      mod_ty ast,
      PySymtable* symbols,
      bool futureAnnotations,
      bool parsed)
      : ast(ast),
        symbols(symbols),
        futureAnnotations(futureAnnotations),
        parsed(parsed) {}

  AstAndSymbols(mod_ty ast, PySymtable* symbols, bool parsed)
      : ast(ast), symbols(symbols), futureAnnotations(false), parsed(parsed) {}
};

std::optional<AstAndSymbols> readFromFile(
    const char* filenameStr,
    PyArena* arena,
    const std::vector<std::string>& checkSubStrings);

std::optional<AstAndSymbols> readFromSource(
    const char* source,
    const char* filenameStr,
    int mode,
    PyArena* arena);

} // namespace strictmod
