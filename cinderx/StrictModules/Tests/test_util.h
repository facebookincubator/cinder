// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

std::string sourceRelativePath(const char* path);

struct VarMatcher {
  std::string name;
  std::optional<std::string> type;

  VarMatcher(std::string name, std::optional<std::string> type)
      : name{std::move(name)}, type{std::move(type)} {};
};

struct StrictMTestCase {
  StrictMTestCase(
      std::string n,
      std::string src,
      std::vector<VarMatcher> vars,
      std::vector<std::string> exc,
      bool isDisabled)
      : name(n),
        src(std::move(src)),
        vars(std::move(vars)),
        exceptions(std::move(exc)),
        isDisabled(isDisabled) {}

  std::string name;
  std::string src;
  std::vector<VarMatcher> vars;
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
// <var1>[:type] <var2>[:type] <var3>[:type]
// ---
// <exception short string>
// ---
//
std::unique_ptr<StrictMTestSuite> ReadStrictMTestSuite(const std::string& path);

std::unordered_set<std::string> ReadStrictMIgnoreList(
    const std::string& ignorePath);
