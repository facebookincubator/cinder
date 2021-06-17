// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <cstdlib>
#include <iostream>

#include <string.h>

#include "Python.h"
#include "fixtures.h"
#include "gtest/gtest.h"
#include "testutil.h"

static constexpr char g_disabled_prefix[] = "@disabled";

static void register_test(const char* path, bool compile_static = false) {
  auto suite = ReadHIRTestSuite(path);
  if (suite == nullptr) {
    std::exit(1);
  }
  auto pass_name = suite->pass_name;
  bool has_pass = !pass_name.empty();
  if (has_pass) {
    jit::hir::PassRegistry registry;
    auto pass = registry.MakePass(pass_name);
    if (pass == nullptr) {
      std::cerr << "ERROR [" << path << "] Unknown pass name "
                << suite->pass_name << std::endl;
      std::exit(1);
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
        [=]() -> RuntimeTest* {
          if (has_pass) {
            jit::hir::PassRegistry registry;
            auto p = registry.MakePass(pass_name);
            return new HIRTest(
                std::move(p),
                test_case.src_is_hir,
                test_case.src,
                test_case.expected_hir,
                compile_static);
          } else {
            return new HIRTest(
                test_case.src_is_hir,
                test_case.src,
                test_case.expected_hir,
                compile_static);
          }
        });
  }
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  register_test("RuntimeTests/hir_tests/call_optimization_test.txt");
  register_test(
      "RuntimeTests/hir_tests/dynamic_comparison_elimination_test.txt");
  register_test("RuntimeTests/hir_tests/hir_builder_test.txt");
  register_test("RuntimeTests/hir_tests/hir_builder_static_test.txt", true);
  register_test("RuntimeTests/hir_tests/load_attr_specialization_test.txt");
  register_test(
      "RuntimeTests/hir_tests/load_const_tuple_item_optimization_test.txt");
  register_test("RuntimeTests/hir_tests/null_check_elimination_test.txt");
  register_test("RuntimeTests/hir_tests/phi_elimination_test.txt");
  register_test(
      "RuntimeTests/hir_tests/redundant_conversion_elimination_test.txt", true);
  register_test("RuntimeTests/hir_tests/refcount_insertion_test.txt");
  register_test(
      "RuntimeTests/hir_tests/refcount_insertion_static_test.txt", true);
  register_test("RuntimeTests/hir_tests/super_access_test.txt", true);
  register_test(
      "RuntimeTests/hir_tests/binary_op_list_specialization_test.txt");

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
