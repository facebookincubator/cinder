// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/Jit/jit_list.h"

#include "cinderx/Common/util.h"

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
  JIT_LOG("Jit-list file: {}", filename);

  std::ifstream fstream(filename);
  if (!fstream) {
    JIT_LOG("Unable to open {}.", filename);
    return false;
  }

  int lineno = 1;
  for (std::string line; getline(fstream, line);) {
    if (!parseLine(line)) {
      JIT_LOG(
          "Error while parsing line {} in jit-list file {}", lineno, filename);
      return false;
    }
    lineno++;
  }

  return true;
}

bool JITList::parseLine(std::string_view line) {
  if (line.empty() || line.at(0) == '#') {
    return true;
  }
  auto atpos = line.find("@");
  if (atpos == std::string_view::npos) {
    auto cln_pos = line.find(":");
    if (cln_pos == std::string_view::npos) {
      return false;
    }
    std::string_view mod = line.substr(0, cln_pos);
    std::string_view qualname = line.substr(cln_pos + 1);
    return addEntryFunc(mod, qualname);
  }

  std::string_view name = line.substr(0, atpos);
  std::string_view loc_str = line.substr(atpos + 1);
  auto cln_pos = loc_str.find(":");
  if (cln_pos == std::string_view::npos) {
    return false;
  }
  std::string_view file = line.substr(atpos + 1, cln_pos);
  std::string_view file_line = loc_str.substr(cln_pos + 1);
  return addEntryCode(name, file, file_line);
}

bool JITList::addEntryFunc(BorrowedRef<> module_name, BorrowedRef<> qualname) {
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

bool JITList::addEntryFunc(
    std::string_view module_name,
    std::string_view qualname) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  Ref<> mn_obj = stringAsUnicode(module_name);
  if (mn_obj == nullptr) {
    return false;
  }
  Ref<> qn_obj = stringAsUnicode(qualname);
  if (qn_obj == nullptr) {
    return false;
  }
  return addEntryFunc(mn_obj, qn_obj);
}

bool JITList::addEntryCode(
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

bool JITList::addEntryCode(
    std::string_view name,
    std::string_view file,
    std::string_view line_no_str) {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "unexpected multithreading");
  Ref<> name_obj = stringAsUnicode(name);
  if (name_obj == nullptr) {
    return false;
  }
  Ref<> file_obj = stringAsUnicode(file);
  if (file_obj == nullptr) {
    return false;
  }
  Ref<> basename_obj = pathBasename(file_obj);
  if (basename_obj == nullptr) {
    return false;
  }

  long line_no = 0;
  auto result =
      std::from_chars(line_no_str.begin(), line_no_str.end(), line_no);
  if (result.ec != std::errc{}) {
    return false;
  }

  auto line_no_obj = Ref<>::steal(PyLong_FromLong(line_no));
  if (file_obj == nullptr) {
    return false;
  }
  return addEntryCode(name_obj, basename_obj, line_no_obj);
}

int JITList::lookupFunc(BorrowedRef<PyFunctionObject> func) const {
  BorrowedRef<PyCodeObject> code =
      reinterpret_cast<PyCodeObject*>(func->func_code);
  if (lookupCode(code) == 1) {
    return 1;
  }
  return lookupName(func->func_module, func->func_qualname);
}

int JITList::lookupCode(BorrowedRef<PyCodeObject> code) const {
  JIT_DCHECK(
      !g_threaded_compile_context.compileRunning(),
      "Unexpected multithreading");

  auto name =
      Ref<>::create(code->co_qualname ? code->co_qualname : code->co_name);
  Ref<> file = pathBasename(code->co_filename);
  if (file == nullptr) {
    return 0;
  }

  BorrowedRef<> file_set = PyDict_GetItemWithError(name_file_line_no_, name);
  if (file_set == nullptr) {
    return 0;
  }
  BorrowedRef<> line_set = PyDict_GetItemWithError(file_set, file);
  if (line_set == nullptr) {
    return 0;
  }

  if (!g_jitlist_match_line_numbers) {
    return 1;
  }

  Ref<> line_no = Ref<>::steal(PyLong_FromLong(code->co_firstlineno));
  return PySet_Contains(line_set, line_no);
}

int JITList::lookupName(BorrowedRef<> module_name, BorrowedRef<> qualname)
    const {
  if (module_name == nullptr) {
    return 0;
  }

  // Check for an exact module:qualname match.
  BorrowedRef<> name_set = PyDict_GetItemWithError(qualnames_, module_name);
  return name_set != nullptr ? PySet_Contains(name_set, qualname) : 0;
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

  Ref<> wildcard = stringAsUnicode("*");
  if (wildcard == nullptr) {
    return nullptr;
  }

  return std::unique_ptr<WildcardJITList>(
      new WildcardJITList(std::move(wildcard), std::move(qualnames)));
}

Ref<> JITList::pathBasename(BorrowedRef<> path) const {
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

bool WildcardJITList::addEntryFunc(
    std::string_view module_name,
    std::string_view qualname) {
  // *:* is invalid.
  return (module_name != "*" || qualname != "*") &&
      JITList::addEntryFunc(module_name, qualname);
}

int WildcardJITList::lookupName(
    BorrowedRef<> module_name,
    BorrowedRef<> qualname) const {
  // Check for an exact match
  if (int st = JITList::lookupName(module_name, qualname); st != 0) {
    return st;
  }

  // Check if all functions in the module are enabled
  if (int st = JITList::lookupName(module_name, wildcard_); st != 0) {
    return st;
  }

  // Check if the qualname is unconditionally enabled
  if (int st = JITList::lookupName(wildcard_, qualname); st != 0) {
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
  if (int st = JITList::lookupName(wildcard_, query); st != 0) {
    return st;
  }

  // Check if the instance method is enabled in the module
  if (int st = JITList::lookupName(module_name, query); st != 0) {
    return st;
  }

  return 0;
}

} // namespace jit
