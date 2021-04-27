// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef STRICTM_TEST_UTIL_H
#define STRICTM_TEST_UTIL_H

#include <memory>
#include <string>
#include <vector>

struct StrictMTestCase {
  StrictMTestCase(
      std::string n,
      std::string src,
      std::vector<std::string> varNames,
      std::vector<std::string> exc,
      bool isDisabled)
      : name(n),
        src(std::move(src)),
        varNames(std::move(varNames)),
        exceptions(std::move(exc)),
        isDisabled(isDisabled) {}

  std::string name;
  std::string src;
  std::vector<std::string> varNames;
  std::vector<std::string> exceptions;
  bool isDisabled;
};

struct StrictMTestSuite {
  std::string name;
  std::vector<StrictMTestCase> test_cases;
};

// Read an Strict module test suite specified via a text file.
//
// The text file specifies the test suite name, a python module level
// source code, a space separated list of variable names
// to compare against the python exec result, and a line of exceptions
//
// File format:
//
// <Test suite name>
// ---
// <Test case name>
// ---
// <Python code>
// ---
// <var1> <var2> <var3>
// ---
// <exception short string>
// ---
//
std::unique_ptr<StrictMTestSuite> ReadStrictMTestSuite(const std::string& path);

#endif // STRICTM_TEST_UTIL_H
