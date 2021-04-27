// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <cstring>

#include "Jit/jit_list.h"
#include "Jit/util.h"

#include <fstream>
#include <string>

namespace jit {

std::unique_ptr<JITList> JITList::create() {
  auto qualnames = Ref<>::steal(PyDict_New());
  if (qualnames == nullptr) {
    return nullptr;
  }

  return std::unique_ptr<JITList>(new JITList(std::move(qualnames)));
}

bool JITList::parseFile(const char* filename) {
  JIT_LOG("Jit-list file: %s", filename);

  std::ifstream fstream(filename);
  if (!fstream) {
    JIT_LOG("Unable to open %s.", filename);
    return false;
  }

  int lineno = 1;
  for (std::string line; getline(fstream, line);) {
    if (!parseLine(line.data())) {
      JIT_LOG(
          "Error while parsing line %d in jit-list file %s", lineno, filename);
      return false;
    }
    lineno++;
  }

  return true;
}

bool JITList::parseLine(const char* orig_line) {
  std::string line_holder(orig_line);
  char* line = line_holder.data();

  // strip leading and trailing whitespaces
  while (isspace(*line) && *line != '\0') {
    line++;
  }

  if (*line == '\0' || *line == '#') {
    return true;
  }

  size_t i = strlen(line) - 1;
  while (isspace(line[i])) {
    line[i--] = '\0';
  }

  const char* module_name = line;
  char* p = line;
  while (*p != ':' && *p != '\0') {
    p++;
  }

  if (*p == '\0') {
    return false;
  }

  *p = '\0';
  p++;
  const char* qual_name = p;

  if (!addEntry(module_name, qual_name)) {
    return false;
  }

  return true;
}

bool JITList::addEntry(const char* module_name, const char* qualname) {
  auto mn_obj = Ref<>::steal(PyUnicode_FromString(module_name));
  if (mn_obj == nullptr) {
    return false;
  }
  auto qn_obj = Ref<>::steal(PyUnicode_FromString(qualname));
  if (qn_obj == nullptr) {
    return false;
  }
  return addEntry(mn_obj, qn_obj);
}

bool JITList::addEntry(BorrowedRef<> module_name, BorrowedRef<> qualname) {
  Ref<> qualname_set(PyDict_GetItem(qualnames_, module_name));
  if (qualname_set == nullptr) {
    qualname_set = Ref<>::steal(PySet_New(nullptr));
    if (qualname_set == nullptr) {
      return false;
    }
    if (PyDict_SetItem(qualnames_, module_name, qualname_set) < 0) {
      return false;
    }
  }
  return PySet_Add(qualname_set, qualname) == 0;
}

int JITList::lookup(BorrowedRef<PyFunctionObject> func) {
  if (func->func_module == nullptr) {
    return 0;
  }
  return lookup(func->func_module, func->func_qualname);
}

int JITList::lookup(BorrowedRef<> module, BorrowedRef<> qualname) {
  // Check for an exact module:qualname match
  BorrowedRef<> name_set = PyDict_GetItemWithError(qualnames_, module);
  if (name_set == nullptr) {
    return 0;
  }
  return PySet_Contains(name_set, qualname);
}

Ref<> JITList::getList() const {
  return Ref<>(qualnames_.get());
}

std::unique_ptr<WildcardJITList> WildcardJITList::create() {
  auto qualnames = Ref<>::steal(PyDict_New());
  if (qualnames == nullptr) {
    return nullptr;
  }

  auto wildcard = Ref<>::steal(PyUnicode_FromString("*"));
  if (wildcard == nullptr) {
    return nullptr;
  }

  return std::unique_ptr<WildcardJITList>(
      new WildcardJITList(std::move(wildcard), std::move(qualnames)));
}

bool WildcardJITList::addEntry(const char* module_name, const char* qualname) {
  if ((strcmp(module_name, "*") == 0) && (strcmp(qualname, "*") == 0)) {
    // *:* is invalid
    return false;
  }
  return JITList::addEntry(module_name, qualname);
}

int WildcardJITList::lookup(BorrowedRef<> module, BorrowedRef<> qualname) {
  // Check for an exact match
  int st = JITList::lookup(module, qualname);
  if (st != 0) {
    return st;
  }

  // Check if all functions in the module are enabled
  st = JITList::lookup(module, wildcard_);
  if (st != 0) {
    return st;
  }

  // Check if the qualname is unconditionally enabled
  st = JITList::lookup(wildcard_, qualname);
  if (st != 0) {
    return st;
  }

  // Check if we've wildcarded the instance method
  Py_ssize_t len = PyUnicode_GetLength(qualname);
  Py_ssize_t idx = PyUnicode_FindChar(qualname, '.', 0, len, -1);
  if (idx == -1) {
    // Not an instance method
    return 0;
  } else if (idx == -2) {
    // Error ocurred
    return -1;
  }

  auto func_name = Ref<>::steal(PyUnicode_Substring(qualname, idx + 1, len));
  if (func_name == nullptr) {
    return -1;
  }
  auto query = Ref<>::steal(PyUnicode_FromFormat("*.%U", func_name.get()));
  if (query == nullptr) {
    return -1;
  }

  // Check if the instance method is unconditionally enabled
  st = JITList::lookup(wildcard_, query);
  if (st != 0) {
    return st;
  }

  // Check if the instance method is enabled in the module
  st = JITList::lookup(module, query);
  if (st != 0) {
    return st;
  }

  return 0;
}

} // namespace jit
