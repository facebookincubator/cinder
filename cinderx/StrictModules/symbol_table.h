// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/py_headers.h"

#include "cinderx/Jit/ref.h"

#include <memory>
#include <string>
#include <unordered_map>
namespace strictmod {
using PySymtable = struct symtable;

std::string mangle(const std::string& className, const std::string& name);

struct PySymtableDeleter {
  /**
  Symtables are created using Python's symtable_new() method
  and must be deleted using _PySymtable_Free. This is a custom
  deleter that does that and can be used in smart pointers
  */
  void operator()(PySymtable* p);
};

class SymtableEntry;

/** Wrapper around the CPython symtable struct
 * to make accessing various functionalities easier
 */
class Symtable {
 public:
  Symtable(std::shared_ptr<PySymtable> symtable) : symtable_(symtable){};
  Symtable(std::unique_ptr<PySymtable, PySymtableDeleter> symtable)
      : symtable_(std::move(symtable)) {}

  Symtable(Symtable&& rhs) : symtable_(std::move(rhs.symtable_)){};
  Symtable(const Symtable& rhs) : symtable_(rhs.symtable_){};

  SymtableEntry entryFromAst(void* key) const;

 private:
  std::shared_ptr<PySymtable> symtable_;
};

/** Properties of one symbol in the symbol table
 * Organized similarly to symtable.py
 */
class Symbol {
 public:
  Symbol(long flags) : flags_(flags) {
    scopeFlag_ = (flags_ >> SCOPE_OFFSET) & SCOPE_MASK;
  }

  bool is_global() const;
  bool is_nonlocal() const;
  bool is_local(void) const;
  bool is_cell() const;
  bool is_parameter() const;

 private:
  long flags_;
  int scopeFlag_;
};

/** Wrapper around CPython PySTEntryObject
 * to make accessing various functionalities easier
 */
class SymtableEntry {
 public:
  SymtableEntry(PySTEntryObject* entry) : entry_(entry), symbolCache_() {}
  SymtableEntry(const SymtableEntry& rhs)
      : entry_(rhs.entry_), symbolCache_() {}
  SymtableEntry(SymtableEntry&& rhs) : entry_(rhs.entry_), symbolCache_() {}

  /* expects mangled name */
  const Symbol& getSymbol(const std::string& name) const;

  bool isClassScope(void) const {
    return entry_->ste_type == ClassBlock;
  }

  bool isFunctionScope(void) const {
    return entry_->ste_type == FunctionBlock;
  }

  std::string getTableName() const {
    return PyUnicode_AsUTF8(entry_->ste_name);
  }

  int getFunctionCodeFlag() const;

  // return a vector String PyObjects, these are borrowed refs
  std::vector<PyObject*> getFunctionVarNames();

 private:
  PySTEntryObject* entry_;
  mutable std::unordered_map<std::string, Symbol> symbolCache_;
};
} // namespace strictmod
