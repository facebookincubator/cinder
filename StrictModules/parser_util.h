// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_FILEUTIL_H__
#define __STRICTM_FILEUTIL_H__

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>
#include "StrictModules/py_headers.h"

#include "StrictModules/symbol_table.h"

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

std::optional<AstAndSymbols>
readFromSource(const char* source, const char* filenameStr, PyArena* arena);

} // namespace strictmod

#endif // __STRICTM_FILEUTIL_H__
