// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef RUNTIME_TEST_UTIL_H
#define RUNTIME_TEST_UTIL_H

#include <memory>
#include <string>
#include <vector>

struct HIRTestCase {
  HIRTestCase(std::string n, bool src_is_hir, std::string src, std::string e)
      : name(n), src_is_hir(src_is_hir), src(src), expected_hir(e) {}

  std::string name;
  bool src_is_hir;
  std::string src;
  std::string expected_hir;
};

struct HIRTestSuite {
  std::string name;
  std::vector<std::string> pass_names;
  std::vector<HIRTestCase> test_cases;
};

// Read an HIR test suite specified via a text file.
//
// The text file specifies the test suite name, an optional
// optimization pass to run on the HIR, and a list of test
// cases. Each test case consists of a name, a python function
// that must be named `test`, and the expected textual HIR.
//
// File format:
//
// <Test suite name>
// ---
// <Optimization pass name 1>
// <Optimization pass name 2>
// ...
// ---
// <Test case name>
// ---
// <Python code>
// ---
// <HIR>
// ---
//
std::unique_ptr<HIRTestSuite> ReadHIRTestSuite(const std::string& path);

#endif
