// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "StrictModules/Tests/test.h"

using namespace strictmod;

TEST_F(AnalyzerTest, SanityCheck) {
  const char* name = "StrictModules/Tests/python_tests/simple_assign.py";
  EXPECT_EQ(analyzeFile(name), true);
}

TEST_F(AnalyzerTest, SimpleImport) {
  const char* s = "import foo\n";
  EXPECT_EQ(analyzeSource(s), true);
}
