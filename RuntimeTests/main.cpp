// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Python.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

static constexpr char g_disabled_prefix[] = "@disabled";

static void register_test(
    const char* path,
    HIRTest::Flags flags = HIRTest::Flags{}) {
  auto suite = ReadHIRTestSuite(path);
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

static void register_json_test(const char* path) {
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

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  register_test("RuntimeTests/hir_tests/clean_cfg_test.txt");
  register_test(
      "RuntimeTests/hir_tests/dynamic_comparison_elimination_test.txt");
  register_test("RuntimeTests/hir_tests/hir_builder_test.txt");
  register_test(
      "RuntimeTests/hir_tests/hir_builder_static_test.txt",
      HIRTest::kCompileStatic);
  register_test("RuntimeTests/hir_tests/guard_type_removal_test.txt");
  register_test("RuntimeTests/hir_tests/inliner_test.txt");
  register_test("RuntimeTests/hir_tests/inliner_elimination_test.txt");
  register_test(
      "RuntimeTests/hir_tests/inliner_static_test.txt",
      HIRTest::kCompileStatic);
  register_test(
      "RuntimeTests/hir_tests/inliner_elimination_static_test.txt",
      HIRTest::kCompileStatic);
  register_test("RuntimeTests/hir_tests/phi_elimination_test.txt");
  register_test("RuntimeTests/hir_tests/refcount_insertion_test.txt");
  register_test(
      "RuntimeTests/hir_tests/refcount_insertion_static_test.txt",
      HIRTest::kCompileStatic);
  register_test(
      "RuntimeTests/hir_tests/super_access_test.txt", HIRTest::kCompileStatic);
  register_test("RuntimeTests/hir_tests/simplify_test.txt");
  register_test("RuntimeTests/hir_tests/simplify_uses_guard_types.txt");
  register_test("RuntimeTests/hir_tests/dead_code_elimination_test.txt");
  register_test(
      "RuntimeTests/hir_tests/profile_data_hir_test.txt",
      HIRTest::kUseProfileData);
  register_test(
      "RuntimeTests/hir_tests/dead_code_elimination_and_simplify_test.txt",
      HIRTest::kCompileStatic);
  register_test(
      "RuntimeTests/hir_tests/simplify_static_test.txt",
      HIRTest::kCompileStatic);
  register_test(
      "RuntimeTests/hir_tests/profile_data_static_hir_test.txt",
      HIRTest::kUseProfileData | HIRTest::kCompileStatic);
  register_json_test("RuntimeTests/hir_tests/json_test.txt");
  register_test(
      "RuntimeTests/hir_tests/builtin_load_method_elimination_test.txt");
  register_test("RuntimeTests/hir_tests/all_passes_test.txt");
  register_test(
      "RuntimeTests/hir_tests/hir_builder_native_calls_test.txt",
      HIRTest::kCompileStatic);

  wchar_t* argv0 = Py_DecodeLocale(argv[0], nullptr);
  if (argv0 == nullptr) {
    std::cerr << "Py_DecodeLocale() failed to allocate\n";
    std::abort();
  }
  Py_SetProgramName(argv0);

  // Prevent any test failures due to transient pointer values.
  jit::setUseStablePointers(true);

  jit::CodeAllocator::makeGlobalCodeAllocator();

  int result = RUN_ALL_TESTS();

  jit::CodeAllocator::freeGlobalCodeAllocator();

  PyMem_RawFree(argv0);
  return result;
}
