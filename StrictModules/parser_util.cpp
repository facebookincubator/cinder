// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/parser_util.h"
#include <fstream>

namespace strictmod {
std::optional<AstAndSymbols> readFromFile(
    const char* filenameStr,
    PyArena* arena,
    const std::vector<std::string>& checkSubStrings) {
  PyFutureFeatures* pyFutures = nullptr;
  PySymtable* symbols = nullptr;
  mod_ty mod = nullptr;
  bool futureAnnotations;
  PyCompilerFlags localflags = _PyCompilerFlags_INIT;

  // if checkSubStrs is available, preliminarily check
  // the file for those strings, and skip parsing if absent
  if (!checkSubStrings.empty()) {
    std::string line;
    bool found = false;

    std::ifstream checkFp(filenameStr);
    if (checkFp.is_open()) {
      while (!checkFp.eof() && !found) {
        getline(checkFp, line);
        for (auto& checkedSubStr : checkSubStrings) {
          if (line.find(checkedSubStr, 0) != std::string::npos) {
            found = true;
            break;
          }
        }
      }
      checkFp.close();
    }
    if (!found) {
      std::make_optional<AstAndSymbols>(mod, symbols, false, false);
    }
  }

  FILE* fp = _Py_fopen(filenameStr, "rb");

  PyObject* filename = PyUnicode_DecodeFSDefault(filenameStr);
  if (fp == nullptr || arena == nullptr) {
    goto error;
  }

  mod = PyParser_ASTFromFileObject(
      fp,
      filename,
      nullptr,
      Py_file_input,
      nullptr,
      nullptr,
      &localflags,
      nullptr,
      arena);

  if (mod == nullptr)
    goto error;
  pyFutures = PyFuture_FromASTObject(mod, filename);
  if (pyFutures == nullptr)
    goto error;
  futureAnnotations = pyFutures->ff_features & CO_FUTURE_ANNOTATIONS;
  symbols = PySymtable_BuildObject(mod, filename, pyFutures);
  if (symbols == nullptr)
    goto error;
  fclose(fp);
  PyObject_Free(pyFutures);
  Py_DECREF(filename);
  return std::make_optional<AstAndSymbols>(
      mod, symbols, futureAnnotations, true);

error:
  // do not free `mod` since its allocated via arena
  if (fp != nullptr)
    fclose(fp);
  Py_XDECREF(filename);
  if (pyFutures != nullptr)
    PyObject_Free(pyFutures);
  if (symbols != nullptr)
    PySymtable_Free(symbols);

  return {};
}

std::optional<AstAndSymbols>
readFromSource(const char* source, const char* filenameStr, PyArena* arena) {
  PyFutureFeatures* pyFutures = nullptr;
  PySymtable* symbols = nullptr;
  mod_ty mod = nullptr;
  bool futureAnnotations;
  PyCompilerFlags localflags = _PyCompilerFlags_INIT;

  PyObject* filename = PyUnicode_DecodeFSDefault(filenameStr);
  if (arena == nullptr) {
    goto error;
  }

  mod = PyParser_ASTFromStringObject(
      source, filename, Py_file_input, &localflags, arena);

  if (mod == nullptr)
    goto error;
  pyFutures = PyFuture_FromASTObject(mod, filename);
  if (pyFutures == nullptr)
    goto error;
  futureAnnotations = pyFutures->ff_features & CO_FUTURE_ANNOTATIONS;
  symbols = PySymtable_BuildObject(mod, filename, pyFutures);
  if (symbols == nullptr)
    goto error;
  PyObject_Free(pyFutures);
  Py_DECREF(filename);
  return std::make_optional<AstAndSymbols>(
      mod, symbols, futureAnnotations, true);

error:
  // do not free `mod` since its allocated via arena
  if (pyFutures != nullptr)
    PyObject_Free(pyFutures);
  if (symbols != nullptr)
    PySymtable_Free(symbols);
  return {};
}

} // namespace strictmod
