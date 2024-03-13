// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/RuntimeTests/testutil.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

static const std::string kDelim = "---";

static std::ostream& err(const std::string& path) {
  std::cerr << "ERROR [" << path << "]: ";
  return std::cerr;
}

bool read_delim(std::ifstream& /* file */) {
  return false;
}

struct FileGuard {
  explicit FileGuard(std::ifstream& f) : file(f) {}
  ~FileGuard() {
    file.close();
  }
  std::ifstream& file;
};

struct Result {
  bool is_error;
  std::string error;

  static Result Ok() {
    Result result = {
        .is_error = false,
        .error = std::string(),
    };
    return result;
  }

  static Result Error(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 2048, fmt, args);
    std::string s(buf);
    Result result = {.is_error = true, .error = s};
    return result;
  }

  static Result ErrorFromErrno(const char* prefix) {
    std::ostringstream os;
    if (prefix != nullptr) {
      os << prefix << ": ";
    }
    os << strerror(errno);
    Result result = {.is_error = true, .error = os.str()};
    return result;
  }
};

struct Reader {
  explicit Reader(const std::string& p, std::ifstream& f)
      : path(p), file(f), line_num(0) {}

  Result ReadLine(std::string& s) {
    if (!std::getline(file, s)) {
      return Result::ErrorFromErrno("Failed reading line");
    }
    line_num++;
    return Result::Ok();
  }

  Result ReadDelim() {
    std::string line;
    auto result = ReadLine(line);
    if (result.is_error) {
      return result;
    }
    if (line != kDelim) {
      return Result::Error("Expected delimiter at line %d", line_num);
    }
    return Result::Ok();
  }

  Result ReadUntilDelim(std::string& s) {
    std::ostringstream os;
    std::string line;
    bool done = false;
    Result result;
    while (!done) {
      auto result = ReadLine(line);
      if (result.is_error) {
        s = os.str();
        return result;
      }
      if (line == kDelim) {
        done = true;
      } else {
        os << line << std::endl;
      }
    }
    s = os.str();
    return Result::Ok();
  }

  bool IsExhausted() const {
    return file.eof();
  }

  const std::string& path;
  std::ifstream& file;
  int line_num;
};

std::unique_ptr<HIRTestSuite> ReadHIRTestSuite(const std::string& suite_path) {
  std::ifstream file;

  std::filesystem::path path =
      std::filesystem::path(__FILE__).parent_path().parent_path().append(
          suite_path);

  file.open(path);
  if (file.fail()) {
    err(path) << "Failed opening test data file: " << strerror(errno)
              << std::endl;
    return nullptr;
  }
  FileGuard g(file);

  auto suite = std::make_unique<HIRTestSuite>();
  Reader reader(path, file);
  Result result = reader.ReadLine(suite->name);
  if (result.is_error) {
    err(path) << "Failed reading test suite name: " << result.error
              << std::endl;
    return nullptr;
  }
  result = reader.ReadDelim();
  if (result.is_error) {
    err(path) << "Failed reading test suite name: " << result.error
              << std::endl;
    return nullptr;
  }

  std::string pass_name;
  result = reader.ReadLine(pass_name);
  if (result.is_error) {
    err(path) << "Failed reading pass name: " << result.error << std::endl;
    return nullptr;
  }
  while (pass_name != kDelim) {
    suite->pass_names.push_back(pass_name);
    result = reader.ReadLine(pass_name);
    if (result.is_error) {
      err(path) << "Failed reading pass name: " << result.error << std::endl;
      return nullptr;
    }
  }

  while (!reader.IsExhausted()) {
    std::string name;
    Result result = reader.ReadUntilDelim(name);
    if (result.is_error) {
      if (reader.IsExhausted() && name.empty()) {
        break;
      } else {
        err(path) << "Incomplete test case at end of file" << std::endl;
        return nullptr;
      }
    }
    // Ignore newlines at the end of test names.
    if (!name.empty() && name.back() == '\n') {
      name.pop_back();
    }

    std::string src;
    result = reader.ReadUntilDelim(src);
    if (result.is_error) {
      if (reader.IsExhausted()) {
        err(path) << "Incomplete test case at end of file" << std::endl;
      } else {
        err(path) << "Failed reading test case " << result.error << std::endl;
      }
      return nullptr;
    }

    auto src_is_hir = false;
    const char* hir_tag = "# HIR\n";
    if (src.substr(0, strlen(hir_tag)) == hir_tag) {
      src_is_hir = true;
      src = src.substr(strlen(hir_tag));
    }

    std::string hir;
    result = reader.ReadUntilDelim(hir);
    if (result.is_error) {
      if (reader.IsExhausted()) {
        err(path) << "Incomplete test case at end of file" << std::endl;
      } else {
        err(path) << "Failed reading test case " << result.error << std::endl;
      }
      return nullptr;
    }

    suite->test_cases.emplace_back(name, src_is_hir, src, hir);
  }

  return suite;
}

const char* parseAndSetEnvVar(const char* env_name) {
  if (strchr(env_name, '=')) {
    const char* key = strtok(strdup(env_name), "=");
    const char* value = strtok(nullptr, "=");
    setenv(key, value, 1);
    return key;
  } else {
    setenv(env_name, "1", 1);
    return env_name;
  }
}

PyObject* addToXargsDict(const wchar_t* flag) {
  PyObject *key = nullptr, *value = nullptr;

  PyObject* opts = PySys_GetXOptions();

  const wchar_t* key_end = wcschr(flag, L'=');
  if (!key_end) {
    key = PyUnicode_FromWideChar(flag, -1);
    value = Py_True; // PyUnicode_FromWideChar(flag, -1);
    Py_INCREF(value);
  } else {
    key = PyUnicode_FromWideChar(flag, key_end - flag);
    value = PyUnicode_FromWideChar(key_end + 1, -1);
  }

  PyDict_SetItem(opts, key, value);
  Py_DECREF(value);

  // we will need the object later on...
  return key;
}
