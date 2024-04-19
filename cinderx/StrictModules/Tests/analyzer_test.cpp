// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/StrictModules/Tests/test.h"
#include "cinderx/StrictModules/sequence_map.h"

using namespace strictmod;

TEST_F(AnalyzerTest, SanityCheck) {
  std::string name = sourceRelativePath("python_tests/simple_assign.py");
  EXPECT_EQ(analyzeFile(name.c_str()), true);
}

TEST_F(AnalyzerTest, SimpleImport) {
  const char* s = "import foo\n";
  EXPECT_EQ(analyzeSource(s), true);
}

TEST_F(AnalyzerTest, SequenceMap) {
  auto x = sequence_map<std::string, int>();
  std::string keys[] = {"b", "a", "d", "c"};
  int answer[] = {1, 4, 3, 2};
  x[keys[0]] = answer[0];
  int z = x["b"];
  EXPECT_EQ(z, answer[0]);

  x[keys[1]] = answer[1];
  x[keys[2]] = answer[2];
  x[keys[3]] = answer[3];
  int i = 0;
  for (auto& it : x) {
    EXPECT_EQ(it.first, keys[i]);
    EXPECT_EQ(it.second.first, answer[i]);
    ++i;
  }
  auto it = x.find(keys[1]);
  EXPECT_EQ(it->first, keys[1]);
  EXPECT_EQ(it->second.first, answer[1]);

  x.erase(keys[2]);
  auto it2 = x.begin();
  EXPECT_EQ(it2->first, keys[0]);
  EXPECT_EQ(it2->second.first, answer[0]);
  ++it2;
  EXPECT_EQ(it2->first, keys[1]);
  EXPECT_EQ(it2->second.first, answer[1]);
  ++it2;
  EXPECT_EQ(it2->first, keys[3]);
  EXPECT_EQ(it2->second.first, answer[3]);
  ++it2;
  EXPECT_EQ(it2 == x.end(), true);

  auto it3 = x.find(keys[1]);
  EXPECT_EQ(it3->first, keys[1]);
  EXPECT_EQ(it3->second.first, answer[1]);
  it3 = x.find(keys[3]);
  EXPECT_EQ(it3->first, keys[3]);
  EXPECT_EQ(it3->second.first, answer[3]);
  it3 = x.find(keys[2]);
  EXPECT_EQ(it3 == x.map_end(), true);

  x[keys[2]] = answer[2];
  auto it4 = x.begin();
  EXPECT_EQ(it4->first, keys[0]);
  EXPECT_EQ(it4->second.first, answer[0]);
  ++it4;
  EXPECT_EQ(it4->first, keys[1]);
  EXPECT_EQ(it4->second.first, answer[1]);
  ++it4;
  EXPECT_EQ(it4->first, keys[3]);
  EXPECT_EQ(it4->second.first, answer[3]);
  ++it4;
  EXPECT_EQ(it4->first, keys[2]);
  EXPECT_EQ(it4->second.first, answer[2]);
  ++it4;
  EXPECT_EQ(it4 == x.end(), true);
}
