// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_FILEUTIL_H__
#define __STRICTM_FILEUTIL_H__

#include <cstdio>
#include <memory>
#include <optional>
#include "StrictModules/py_headers.h"

#include "StrictModules/symbol_table.h"

namespace strictmod {

struct AstAndSymbols {
  mod_ty ast;
  std::unique_ptr<PySymtable, PySymtableDeleter> symbols;
  bool futureAnnotations;

  AstAndSymbols(mod_ty ast, PySymtable* symbols, bool futureAnnotations)
      : ast(ast), symbols(symbols), futureAnnotations(futureAnnotations) {}

  AstAndSymbols(mod_ty ast, PySymtable* symbols)
      : ast(ast), symbols(symbols), futureAnnotations(false) {}
};

std::optional<AstAndSymbols> readFromFile(
    const char* filenameStr,
    PyArena* arena);

std::optional<AstAndSymbols>
readFromSource(const char* source, const char* filenameStr, PyArena* arena);

} // namespace strictmod

#endif // __STRICTM_FILEUTIL_H__
