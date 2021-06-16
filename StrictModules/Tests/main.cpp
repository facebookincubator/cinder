// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/py_headers.h"
#include "gtest/gtest.h"

#include "StrictModules/Tests/test.h"

#include <iostream>
static void register_test(const char* path, const char* ignorePath) {
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
              std::move(test_case.varNames),
              std::move(test_case.exceptions));
        });
  }
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  wchar_t* argv0 = Py_DecodeLocale(argv[0], nullptr);
  if (argv0 == nullptr) {
    std::cerr << "Py_DecodeLocale() failed to allocate\n";
    std::abort();
  }
  Py_SetProgramName(argv0);
  if (argc > 1) {
    register_test(
        "StrictModules/Tests/comparison_tests/interpreter_test.txt", argv[1]);
  } else {
    register_test(
        "StrictModules/Tests/comparison_tests/interpreter_test.txt", nullptr);
  }

  int result = RUN_ALL_TESTS();
  PyMem_RawFree(argv0);
  return result;
}
