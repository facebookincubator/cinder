// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/symbol_table.h"
#include <cstring>
#include <stdexcept>
namespace strictmod {

//-------------------------Symtable---------------------------

SymtableEntry Symtable::entryFromAst(void* key) const {
  PySTEntryObject* obj = PySymtable_Lookup(symtable_.get(), key);
  if (PyErr_Occurred() || obj == nullptr) {
    Py_XDECREF(obj);
    PyErr_Clear();
    throw std::runtime_error("internal error: symbol not found from AST");
  }
  // entries are managed through the Symtable and should not outlive the table
  Py_DECREF(obj);
  return SymtableEntry(obj);
}

//-----------------------------Symbol----------------------------
bool Symbol::is_global() const {
  return scopeFlag_ == GLOBAL_EXPLICIT || scopeFlag_ == GLOBAL_IMPLICIT;
}

bool Symbol::is_nonlocal() const {
  return flags_ & DEF_NONLOCAL;
}

bool Symbol::is_local(void) const {
  return scopeFlag_ == LOCAL || scopeFlag_ == CELL;
}

//-------------------------SymtableEntry-------------------------
const Symbol& SymtableEntry::getSymbol(const std::string& name) const {
  if (symbolCache_.find(name) != symbolCache_.end()) {
    return symbolCache_.at(name);
  }
  PyObject* symbols = entry_->ste_symbols;
  PyObject* flagsPy = PyDict_GetItemString(symbols, name.c_str());
  if (flagsPy == nullptr) {
    PyErr_Clear();
    throw std::runtime_error(
        "internal error: symbol not found from symbol table");
  }
  long flags = PyLong_AS_LONG(flagsPy);
  symbolCache_.try_emplace(name, flags);
  return symbolCache_.at(name);
}

bool SymtableEntry::isClassScope() const {
  return entry_->ste_type == ClassBlock;
}

} // namespace strictmod
