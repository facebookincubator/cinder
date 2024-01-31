// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Tests/test.h"
#include "cinderx/StrictModules/py_headers.h"
#include "gtest/gtest.h"

#include <filesystem>
#include <iostream>

static void register_test(std::string&& path, const char* ignorePath) {
  auto suite = ReadStrictMTestSuite(path);
  if (suite == nullptr) {
    std::exit(1);
  }
  std::unordered_set<std::string> ignores;
  if (ignorePath != nullptr) {
    std::string ignorePathS(ignorePath);
    ignores = ReadStrictMIgnoreList(ignorePathS);
  }

  for (auto& test_case : suite->test_cases) {
    if (test_case.isDisabled) {
      continue;
    }
    if (ignores.find(test_case.name) != ignores.end()) {
      continue;
    }
    ::testing::RegisterTest(
        suite->name.c_str(),
        test_case.name.c_str(),
        nullptr,
        nullptr,
        __FILE__,
        __LINE__,
        [=]() -> PythonTest* {
          return new ModuleLoaderComparisonTest(
              test_case.src,
              std::move(test_case.vars),
              std::move(test_case.exceptions));
        });
  }
}

#ifndef BAKED_IN_PYTHONPATH
#error "BAKED_IN_PYTHONPATH must be defined"
#endif
#define _QUOTE(x) #x
#define QUOTE(x) _QUOTE(x)
#define _BAKED_IN_PYTHONPATH QUOTE(BAKED_IN_PYTHONPATH)

int main(int argc, char* argv[]) {
  setenv("PYTHONPATH", _BAKED_IN_PYTHONPATH, 1);

  ::testing::InitGoogleTest(&argc, argv);
  wchar_t* argv0 = Py_DecodeLocale(argv[0], nullptr);
  if (argv0 == nullptr) {
    std::cerr << "Py_DecodeLocale() failed to allocate\n";
    std::abort();
  }
  Py_SetProgramName(argv0);
  if (argc > 1) {
    register_test(
        sourceRelativePath(
            "StrictModules/Tests/comparison_tests/interpreter_test.txt"),
        argv[1]);
  } else {
    register_test(
        sourceRelativePath(
            "StrictModules/Tests/comparison_tests/interpreter_test.txt"),
        nullptr);
  }

  int result = RUN_ALL_TESTS();
  PyMem_RawFree(argv0);
  return result;
}
