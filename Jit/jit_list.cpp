// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/jit_list.h"

#include "cinder/port-assert.h"

#include "Jit/util.h"

#include <cstring>
#include <fstream>
#include <string>

namespace jit {

static bool g_jitlist_match_line_numbers = false;

void jitlist_match_line_numbers(bool v) {
  g_jitlist_match_line_numbers = v;
}

bool get_jitlist_match_line_numbers() {
  return g_jitlist_match_line_numbers;
}

std::unique_ptr<JITList> JITList::create() {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto qualnames = Ref<>::steal(PyDict_New());
  if (qualnames == nullptr) {
    return nullptr;
  }
  auto name_file_line_no = Ref<>::steal(PyDict_New());
  if (name_file_line_no == nullptr) {
    return nullptr;
  }

  return std::unique_ptr<JITList>(
      new JITList(std::move(qualnames), std::move(name_file_line_no)));
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
    if (!parseLine(line)) {
      JIT_LOG(
          "Error while parsing line %d in jit-list file %s", lineno, filename);
      return false;
    }
    lineno++;
  }

  return true;
}

bool JITList::parseLine(const std::string& line) {
  if (line.empty() || line.at(0) == '#') {
    return true;
  }
  auto atpos = line.find("@");
  if (atpos == std::string::npos) {
    auto cln_pos = line.find(":");
    if (cln_pos == std::string::npos) {
      return false;
    }
    std::string mod = line.substr(0, cln_pos);
    std::string qualname = line.substr(cln_pos + 1);
    if (!addEntryFO(mod.c_str(), qualname.c_str())) {
      return false;
    }
  } else {
    std::string name = line.substr(0, atpos);
    std::string loc_str = line.substr(atpos + 1);
    auto cln_pos = loc_str.find(":");
    if (cln_pos == std::string::npos) {
      return false;
    }
    std::string file = line.substr(atpos + 1, cln_pos);
    std::string file_line = loc_str.substr(cln_pos + 1);
    if (!addEntryCO(name.c_str(), file.c_str(), file_line.c_str())) {
      return false;
    }
  }
  return true;
}

bool JITList::addEntryFO(const char* module_name, const char* qualname) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto mn_obj = Ref<>::steal(PyUnicode_FromString(module_name));
  if (mn_obj == nullptr) {
    return false;
  }
  auto qn_obj = Ref<>::steal(PyUnicode_FromString(qualname));
  if (qn_obj == nullptr) {
    return false;
  }
  return addEntryFO(mn_obj, qn_obj);
}

bool JITList::addEntryFO(BorrowedRef<> module_name, BorrowedRef<> qualname) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto qualname_set = Ref<>::create(PyDict_GetItem(qualnames_, module_name));
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

bool JITList::addEntryCO(
    const char* name,
    const char* file,
    const char* line_no_str) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto name_obj = Ref<>::steal(PyUnicode_FromString(name));
  if (name_obj == nullptr) {
    return false;
  }
  auto file_obj = Ref<>::steal(PyUnicode_FromString(file));
  if (file_obj == nullptr) {
    return false;
  }
  Ref<> basename_obj(pathBasename(file_obj));
  long line_no;
  try {
    line_no = std::stol(line_no_str);
  } catch (...) {
    return false;
  }
  auto line_no_obj = Ref<>::steal(PyLong_FromLong(line_no));
  if (file_obj == nullptr) {
    return false;
  }
  return addEntryCO(name_obj, basename_obj, line_no_obj);
}

Ref<> JITList::pathBasename(BorrowedRef<> path) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  if (path_sep_ == nullptr) {
    const wchar_t* sep_str = L"/";
    auto sep_str_obj = Ref<>::steal(PyUnicode_FromWideChar(&sep_str[0], 1));
    if (sep_str_obj == nullptr) {
      return nullptr;
    }
    path_sep_ = std::move(sep_str_obj);
  }
  auto split_path_obj = Ref<>::steal(PyUnicode_RSplit(path, path_sep_, 1));
  if (split_path_obj == nullptr || !PyList_Check(split_path_obj) ||
      PyList_GET_SIZE(split_path_obj.get()) < 1) {
    return nullptr;
  }
  return Ref<>::create(PyList_GET_ITEM(
      split_path_obj.get(), PyList_GET_SIZE(split_path_obj.get()) - 1));
}

bool JITList::addEntryCO(
    BorrowedRef<> name,
    BorrowedRef<> file,
    BorrowedRef<> line_no) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto file_set = Ref<>::create(PyDict_GetItem(name_file_line_no_, name));
  if (file_set == nullptr) {
    file_set = Ref<>::steal(PyDict_New());
    if (file_set == nullptr) {
      return false;
    }
    if (PyDict_SetItem(name_file_line_no_, name, file_set) < 0) {
      return false;
    }
  }
  auto line_set = Ref<>::create(PyDict_GetItem(file_set, file));
  if (line_set == nullptr) {
    line_set = Ref<>::steal(PySet_New(nullptr));
    if (line_set == nullptr) {
      return false;
    }
    if (PyDict_SetItem(file_set, file, line_set) < 0) {
      return false;
    }
  }
  return PySet_Add(line_set, line_no) == 0;
}

int JITList::lookup(BorrowedRef<PyFunctionObject> func) {
  int res;
  if (func->func_module) {
    if ((res = lookupFO(func->func_module, func->func_qualname))) {
      return res;
    }
  }
  if (func->func_code) {
    return lookupCO(reinterpret_cast<PyCodeObject*>(func->func_code));
  }
  return 0;
}

int JITList::lookupFO(BorrowedRef<> mod, BorrowedRef<> qualname) {
  if (mod == nullptr) {
    return 0;
  }
  // Check for an exact module:qualname match
  BorrowedRef<> name_set = PyDict_GetItemWithError(qualnames_, mod);
  if (name_set == nullptr) {
    return 0;
  }
  return PySet_Contains(name_set, qualname);
}

int JITList::lookupCO(BorrowedRef<PyCodeObject> code) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto name =
      Ref<>::create(code->co_qualname ? code->co_qualname : code->co_name);
  Ref<> line_no = Ref<>::steal(PyLong_FromLong(code->co_firstlineno));
  Ref<> file(pathBasename(code->co_filename));

  BorrowedRef<> file_set = PyDict_GetItemWithError(name_file_line_no_, name);
  if (file_set == nullptr) {
    return 0;
  }
  BorrowedRef<> line_set = PyDict_GetItemWithError(file_set, file);
  if (line_set == nullptr) {
    return 0;
  }

  return g_jitlist_match_line_numbers ? PySet_Contains(line_set, line_no) : 1;
}

Ref<> JITList::getList() const {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  return Ref<>::steal(
      PyTuple_Pack(2, qualnames_.get(), name_file_line_no_.get()));
}

std::unique_ptr<WildcardJITList> WildcardJITList::create() {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
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

bool WildcardJITList::addEntryFO(
    const char* module_name,
    const char* qualname) {
  if ((strcmp(module_name, "*") == 0) && (strcmp(qualname, "*") == 0)) {
    // *:* is invalid
    return false;
  }
  return JITList::addEntryFO(module_name, qualname);
}

int WildcardJITList::lookupFO(BorrowedRef<> mod, BorrowedRef<> qualname) {
  // Check for an exact match
  int st = JITList::lookupFO(mod, qualname);
  if (st != 0) {
    return st;
  }

  // Check if all functions in the module are enabled
  st = JITList::lookupFO(mod, wildcard_);
  if (st != 0) {
    return st;
  }

  // Check if the qualname is unconditionally enabled
  st = JITList::lookupFO(wildcard_, qualname);
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

  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  auto func_name = Ref<>::steal(PyUnicode_Substring(qualname, idx + 1, len));
  if (func_name == nullptr) {
    return -1;
  }
  auto query = Ref<>::steal(PyUnicode_FromFormat("*.%U", func_name.get()));
  if (query == nullptr) {
    return -1;
  }

  // Check if the instance method is unconditionally enabled
  st = JITList::lookupFO(wildcard_, query);
  if (st != 0) {
    return st;
  }

  // Check if the instance method is enabled in the module
  st = JITList::lookupFO(mod, query);
  if (st != 0) {
    return st;
  }

  return 0;
}

} // namespace jit
