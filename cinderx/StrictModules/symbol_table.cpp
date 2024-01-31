// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/symbol_table.h"

#include <cstring>
#include <stdexcept>
namespace strictmod {

//-------------------------PySymtableDeleter---------------------------

void PySymtableDeleter::operator()(PySymtable* p) {
  _PySymtable_Free(p);
}

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

bool Symbol::is_cell() const {
  return scopeFlag_ == CELL;
}

bool Symbol::is_parameter() const {
  return flags_ & DEF_PARAM;
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

int SymtableEntry::getFunctionCodeFlag() const {
  // similar to compute_code_flags in compile.c which is unfortunately
  // not a public function
  int flags = 0;
  if (entry_->ste_type == FunctionBlock) {
    flags |= CO_NEWLOCALS | CO_OPTIMIZED;
    if (entry_->ste_nested) {
      flags |= CO_NESTED;
    }

    if (entry_->ste_generator && !entry_->ste_coroutine) {
      flags |= CO_GENERATOR;
    }
    if (!entry_->ste_generator && entry_->ste_coroutine) {
      flags |= CO_COROUTINE;
    }
    if (entry_->ste_generator && entry_->ste_coroutine) {
      flags |= CO_ASYNC_GENERATOR;
    }
    if (entry_->ste_varargs) {
      flags |= CO_VARARGS;
    }
    if (entry_->ste_varkeywords) {
      flags |= CO_VARKEYWORDS;
    }
    if (!entry_->ste_child_free) {
      flags |= CO_NOFREE;
    }
  }
  return flags;
}

std::vector<PyObject*> SymtableEntry::getFunctionVarNames() {
  Ref<> keys = Ref<>::steal(PyDict_Keys(entry_->ste_symbols));
  int keySize = PyList_Size(keys.get());
  std::vector<PyObject*> varnames;

  for (int i = 0; i < keySize; ++i) {
    PyObject* key = PyList_GET_ITEM(keys.get(), i);

    assert(key != nullptr);
    auto flagsPy = Ref<>::create(PyDict_GetItem(entry_->ste_symbols, key));
    assert(flagsPy != nullptr);
    long flags = PyLong_AS_LONG(flagsPy.get());
    // we want non-cell locals
    auto sym = Symbol(flags);
    if (sym.is_parameter() || (sym.is_local() && !sym.is_cell())) {
      varnames.push_back(key);
    }
  }
  return varnames;
}

} // namespace strictmod
