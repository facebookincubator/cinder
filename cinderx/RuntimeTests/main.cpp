// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "Python.h"

#ifdef BUCK_BUILD
#include "cinderx/_cinderx-lib.h"
#endif

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

#ifdef BUCK_BUILD
#include "tools/cxx/Resources.h"
#endif

#include <cstdlib>
#include <cstring>
#include <iostream>

static constexpr char g_disabled_prefix[] = "@disabled";

static void remap_txt_path(std::string& path) {
#ifdef BUCK_BUILD
  boost::filesystem::path hir_tests_path =
      build::getResourcePath("cinderx/RuntimeTests/hir_tests");
  path = (hir_tests_path / path).string();
#else
  path = "RuntimeTests/hir_tests/" + path;
#endif
}

static void register_test(
    std::string path,
    HIRTest::Flags flags = HIRTest::Flags{}) {
  remap_txt_path(path);
  auto suite = ReadHIRTestSuite(path.c_str());
  if (suite == nullptr) {
    std::exit(1);
  }
  auto pass_names = suite->pass_names;
  bool has_passes = !pass_names.empty();
  if (has_passes) {
    jit::hir::PassRegistry registry;
    for (auto& pass_name : pass_names) {
      auto pass = registry.MakePass(pass_name);
      if (pass == nullptr) {
        std::cerr << "ERROR [" << path << "] Unknown pass name " << pass_name
                  << std::endl;
        std::exit(1);
      }
    }
  }
  for (auto& test_case : suite->test_cases) {
    if (strncmp(
            test_case.name.c_str(),
            g_disabled_prefix,
            sizeof(g_disabled_prefix) - 1) == 0) {
      continue;
    }
    ::testing::RegisterTest(
        suite->name.c_str(),
        test_case.name.c_str(),
        nullptr,
        nullptr,
        __FILE__,
        __LINE__,
        [=] {
          auto test = new HIRTest(
              test_case.src_is_hir,
              test_case.src,
              test_case.expected_hir,
              flags);
          if (has_passes) {
            jit::hir::PassRegistry registry;
            std::vector<std::unique_ptr<jit::hir::Pass>> passes;
            for (auto& pass_name : pass_names) {
              passes.push_back(registry.MakePass(pass_name));
            }
            test->setPasses(std::move(passes));
          }
          return test;
        });
  }
}

static void register_json_test(std::string path) {
  remap_txt_path(path);
  auto suite = ReadHIRTestSuite(path);
  if (suite == nullptr) {
    std::exit(1);
  }
  for (auto& test_case : suite->test_cases) {
    if (strncmp(
            test_case.name.c_str(),
            g_disabled_prefix,
            sizeof(g_disabled_prefix) - 1) == 0) {
      continue;
    }
    ::testing::RegisterTest(
        suite->name.c_str(),
        test_case.name.c_str(),
        nullptr,
        nullptr,
        __FILE__,
        __LINE__,
        [=] {
          auto test = new HIRJSONTest(
              test_case.src,
              // Actually JSON
              test_case.expected_hir);
          return test;
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
      build::getResourcePath("cinderx/RuntimeTests/python_install");
  {
    std::string python_install_str =
        (python_install / "lib" / "python3.10").string() + ":" +
        (python_install / "lib" / "python3.10" / "lib-dynload").string();
    std::cout << "PYTHONPATH=" << python_install_str << std::endl;
    setenv("PYTHONPATH", python_install_str.c_str(), 1);
  }
  if (PyImport_AppendInittab("_cinderx", PyInit__cinderx) != 0) {
    PyErr_Print();
    std::cerr << "Error: could not add to inittab\n";
    return 1;
  }
#endif

  ::testing::InitGoogleTest(&argc, argv);
  register_test("clean_cfg_test.txt");
  register_test("dynamic_comparison_elimination_test.txt");
  register_test("hir_builder_test.txt");
  register_test("hir_builder_static_test.txt", HIRTest::kCompileStatic);
  register_test("guard_type_removal_test.txt");
  register_test("inliner_test.txt");
  register_test("inliner_elimination_test.txt");
  register_test("inliner_static_test.txt", HIRTest::kCompileStatic);
  register_test("inliner_elimination_static_test.txt", HIRTest::kCompileStatic);
  register_test("phi_elimination_test.txt");
  register_test("refcount_insertion_test.txt");
  register_test("refcount_insertion_static_test.txt", HIRTest::kCompileStatic);
  register_test("super_access_test.txt", HIRTest::kCompileStatic);
  register_test("simplify_test.txt");
  register_test("simplify_uses_guard_types.txt");
  register_test("dead_code_elimination_test.txt");
  register_test("profile_data_hir_test.txt", HIRTest::kUseProfileData);
  register_test(
      "dead_code_elimination_and_simplify_test.txt", HIRTest::kCompileStatic);
  register_test("simplify_static_test.txt", HIRTest::kCompileStatic);
  register_test(
      "profile_data_static_hir_test.txt",
      HIRTest::kUseProfileData | HIRTest::kCompileStatic);
  register_json_test("json_test.txt");
  register_test("builtin_load_method_elimination_test.txt");
  register_test("all_passes_test.txt");
  register_test("all_passes_static_test.txt", HIRTest::kCompileStatic);
  register_test("hir_builder_native_calls_test.txt", HIRTest::kCompileStatic);

  wchar_t* argv0 = Py_DecodeLocale(argv[0], nullptr);
  if (argv0 == nullptr) {
    std::cerr << "Py_DecodeLocale() failed to allocate\n";
    std::abort();
  }
  Py_SetProgramName(argv0);

  // Prevent any test failures due to transient pointer values.
  jit::setUseStablePointers(true);

  int result = RUN_ALL_TESTS();

  PyMem_RawFree(argv0);
  return result;
}
