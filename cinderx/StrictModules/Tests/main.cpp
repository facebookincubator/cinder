// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifdef BUCK_BUILD
#include "cinderx/_cinderx-lib.h"
#endif

#include "cinderx/StrictModules/Tests/test.h"
#include "cinderx/StrictModules/py_headers.h"
#include "gtest/gtest.h"

#ifdef BUCK_BUILD
#include "tools/cxx/Resources.h"
#endif

#include <filesystem>
#include <iostream>

static void remap_txt_path(std::string& path) {
#ifdef BUCK_BUILD
  boost::filesystem::path tests_path =
      build::getResourcePath("cinderx/StrictModules/Tests/TestFiles");
  path = (tests_path / "comparison_tests" / path).string();
#else
  path = "cinderx/StrictModules/Tests/comparison_tests/" + path;
#endif
}

static void register_test(std::string&& path, const char* ignorePath) {
  remap_txt_path(path);
  auto suite = ReadStrictMTestSuite(path);
  if (suite == nullptr) {
    std::exit(1);
  }
  std::unordered_set<std::string> ignores;
  if (ignorePath != nullptr) {
    std::string ignorePathS(ignorePath);
    remap_txt_path(ignorePathS);
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

#ifdef BAKED_IN_PYTHONPATH
#define _QUOTE(x) #x
#define QUOTE(x) _QUOTE(x)
#define _BAKED_IN_PYTHONPATH QUOTE(BAKED_IN_PYTHONPATH)
#endif

#ifdef BUCK_BUILD
PyMODINIT_FUNC PyInit__cinderx() {
  return _cinderx_lib_init();
}
#endif

int main(int argc, char* argv[]) {
#ifdef BAKED_IN_PYTHONPATH
  setenv("PYTHONPATH", _BAKED_IN_PYTHONPATH, 1);
#endif

#ifdef BUCK_BUILD
  boost::filesystem::path python_install =
      build::getResourcePath("cinderx/StrictModules/Tests/python_install");
  {
    std::string python_install_str =
        (python_install / "lib" / "python3.10").string() + ":" +
        (python_install / "lib" / "python3.10" / "lib-dynload").string();
    std::cout << "PYTHONPATH=" << python_install_str << std::endl;
    setenv("PYTHONPATH", python_install_str.c_str(), 1);
  }
#endif

#ifdef BUCK_BUILD
  if (PyImport_AppendInittab("_cinderx", PyInit__cinderx) != 0) {
    PyErr_Print();
    std::cerr << "Error: could not add to inittab\n";
    return 1;
  }
#endif

  ::testing::InitGoogleTest(&argc, argv);
  wchar_t* argv0 = Py_DecodeLocale(argv[0], nullptr);
  if (argv0 == nullptr) {
    std::cerr << "Py_DecodeLocale() failed to allocate\n";
    std::abort();
  }
  Py_SetProgramName(argv0);
  if (argc > 1) {
    register_test("interpreter_test.txt", argv[1]);
  } else {
    register_test("interpreter_test.txt", nullptr);
  }

  int result = RUN_ALL_TESTS();
  PyMem_RawFree(argv0);
  return result;
}
