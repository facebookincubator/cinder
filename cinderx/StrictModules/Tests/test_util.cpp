// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "test_util.h"

#ifdef BUCK_BUILD
#include "tools/cxx/Resources.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

static const std::string kDelim = "---";
static const std::string kVarDelim = " ";
static const std::string kExcDelim = "\n";

std::string sourceRelativePath(const char* path) {
#ifdef BUCK_BUILD
  return (build::getResourcePath("cinderx/StrictModules/Tests/TestFiles") /
          path)
      .string();
#else
  return std::filesystem::path(__FILE__)
      .parent_path()
      .append(path)
      .string();
#endif
}

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

std::unique_ptr<StrictMTestSuite> ReadStrictMTestSuite(
    const std::string& path) {
  std::ifstream file;
  file.open(path);
  if (file.fail()) {
    err(path) << "Failed opening test data file: " << strerror(errno)
              << std::endl;
    return nullptr;
  }
  FileGuard g(file);

  auto suite = std::make_unique<StrictMTestSuite>();
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

  while (!reader.IsExhausted()) {
    std::string name;
    bool disabled = false;
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

    if (auto space = name.find(" "); space != std::string::npos) {
      auto disabledStr = name.substr(space + 1);
      disabled = disabledStr == "@disabled";
      name.resize(space);
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

    std::string varNames;
    result = reader.ReadUntilDelim(varNames);
    if (result.is_error) {
      if (reader.IsExhausted()) {
        err(path) << "Incomplete test case at end of file" << std::endl;
      } else {
        err(path) << "Failed reading test case " << result.error << std::endl;
      }
      return nullptr;
    }

    // Ignore newlines at the end of var names.
    if (!varNames.empty()) {
      if (varNames.back() == '\n') {
        varNames.pop_back();
      }
      // put kVarDelim at the end to capture all variables
      varNames.append(kVarDelim);
    }

    std::vector<VarMatcher> varVec;
    auto start = 0;
    auto end = varNames.find(kVarDelim);
    while (end != varNames.npos) {
      auto varDef = varNames.substr(start, end - start);
      size_t col = varDef.find(":");
      if (col != varDef.npos) {
        auto name = varDef.substr(0, col);
        auto type = varDef.substr(col + 1);
        varVec.emplace_back(std::move(name), std::move(type));
      } else {
        varVec.emplace_back(std::move(varDef), std::nullopt);
      }

      start = end + kVarDelim.length();
      end = varNames.find(kVarDelim, start);
    }

    std::string excShortString;
    result = reader.ReadUntilDelim(excShortString);
    if (result.is_error) {
      if (reader.IsExhausted()) {
        err(path) << "Incomplete test case at end of file" << std::endl;
      } else {
        err(path) << "Failed reading test case " << result.error << std::endl;
      }
      return nullptr;
    }

    std::vector<std::string> excShortStringVec;
    start = 0;
    end = excShortString.find(kExcDelim);
    while (end != excShortString.npos) {
      excShortStringVec.push_back(excShortString.substr(start, end - start));
      start = end + kExcDelim.length();
      end = excShortString.find(kExcDelim, start);
    }

    suite->test_cases.emplace_back(
        name, src, std::move(varVec), std::move(excShortStringVec), disabled);
  }

  return suite;
}

std::unordered_set<std::string> ReadStrictMIgnoreList(
    const std::string& ignorePath) {
  std::ifstream file;
  file.open(ignorePath);
  if (file.fail()) {
    err(ignorePath) << "Failed opening test ignore file: " << strerror(errno)
                    << std::endl;
    return {};
  }
  FileGuard g(file);
  Reader reader(ignorePath, file);
  std::unordered_set<std::string> ignores;
  while (!reader.IsExhausted()) {
    std::string ignoreName;

    Result result = reader.ReadLine(ignoreName);
    if (result.is_error) {
      err(ignoreName) << "Failed reading ignore name: " << result.error
                      << std::endl;
      return ignores;
    }
    ignores.insert(ignoreName);
  }
  return ignores;
}
