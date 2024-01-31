// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/dataflow.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

using namespace jit::optimizer;

// This test runs the example found in Section 8.1 of
// the book Advanced Compiler Design And Implementation
TEST(DataFlowTest, ReachingTest) {
  DataFlowAnalyzer<std::string> analyzer;
  analyzer.AddObjects(
      {"m:1", "f0:2", "f1:3", "i:5", "f2:8", "f0:9", "f1:10", "i:11"});

  DataFlowBlock b1, b2, b3, b4, b5, b6, ENTRY, EXIT;

  ENTRY.ConnectTo(b1);
  b1.ConnectTo(b2);
  b1.ConnectTo(b3);
  b2.ConnectTo(EXIT);
  b3.ConnectTo(b4);
  b4.ConnectTo(b5);
  b4.ConnectTo(b6);
  b5.ConnectTo(EXIT);
  b6.ConnectTo(b4);

  analyzer.AddBlock(ENTRY);
  analyzer.AddBlock(EXIT);
  analyzer.AddBlock(b1);
  analyzer.AddBlock(b2);
  analyzer.AddBlock(b3);
  analyzer.AddBlock(b4);
  analyzer.AddBlock(b5);
  analyzer.AddBlock(b6);

  analyzer.SetBlockGenBits(b1, {"m:1", "f0:2", "f1:3"});
  analyzer.SetBlockKillBits(b1, {"f0:9", "f1:10"});

  analyzer.SetBlockGenBits(b3, {"i:5"});
  analyzer.SetBlockKillBits(b3, {"i:11"});

  analyzer.SetBlockGenBits(b6, {"f2:8", "f0:9", "f1:10", "i:11"});
  analyzer.SetBlockKillBits(b6, {"f0:2", "f1:3", "i:5"});

  analyzer.SetEntryBlock(ENTRY);
  analyzer.SetEntryBlock(EXIT);

  analyzer.RunAnalysis();

  ASSERT_EQ(ENTRY.in_.GetBitChunk(), 0);
  ASSERT_EQ(b1.in_.GetBitChunk(), 0);
  ASSERT_EQ(b2.in_.GetBitChunk(), 7);
  ASSERT_EQ(b3.in_.GetBitChunk(), 7);
  ASSERT_EQ(b4.in_.GetBitChunk(), 0xff);
  ASSERT_EQ(b5.in_.GetBitChunk(), 0xff);
  ASSERT_EQ(b6.in_.GetBitChunk(), 0xff);
  ASSERT_EQ(EXIT.in_.GetBitChunk(), 0xff);

  ASSERT_EQ(ENTRY.out_.GetBitChunk(), 0);
  ASSERT_EQ(b1.out_.GetBitChunk(), 7);
  ASSERT_EQ(b2.out_.GetBitChunk(), 7);
  ASSERT_EQ(b3.out_.GetBitChunk(), 0xf);
  ASSERT_EQ(b4.out_.GetBitChunk(), 0xff);
  ASSERT_EQ(b5.out_.GetBitChunk(), 0xff);
  ASSERT_EQ(b6.out_.GetBitChunk(), 0xf1);
  ASSERT_EQ(EXIT.out_.GetBitChunk(), 0xff);
}
