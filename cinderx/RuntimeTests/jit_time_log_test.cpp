// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/jit_time_log.h"

#include "cinderx/RuntimeTests/fixtures.h"

#include <chrono>

using JITTimeLogTest = RuntimeTest;

using namespace jit;

void testWithCompilationTimes(
    const std::string& funcList,
    const std::function<void()>& tests) {
  parseAndSetFuncList(funcList);
  tests();
}

TEST_F(JITTimeLogTest, CheckFuncListInclusion) {
  testWithCompilationTimes("__main__:foo", []() {
    EXPECT_TRUE(captureCompilationTimeFor("__main__:foo"));
    EXPECT_FALSE(captureCompilationTimeFor("__main__:bar"));
  });
}

TEST_F(JITTimeLogTest, CheckFuncListInclusionWildCardStar) {
  testWithCompilationTimes("__main__:*", []() {
    EXPECT_TRUE(captureCompilationTimeFor("__main__:foo"));
    EXPECT_TRUE(captureCompilationTimeFor("__main__:bar"));
  });
}

TEST_F(JITTimeLogTest, CheckFuncListInclusionWildCardQM) {
  testWithCompilationTimes("__main__:f?o", []() {
    EXPECT_TRUE(captureCompilationTimeFor("__main__:foo"));
    EXPECT_FALSE(captureCompilationTimeFor("__main__:fo"));
    EXPECT_FALSE(captureCompilationTimeFor("__main__:fp"));
  });
}

TEST_F(JITTimeLogTest, DumpNothing) {
  CompilationPhaseTimer compilation_phase_timer("function_name");
  testing::internal::CaptureStderr();
  compilation_phase_timer.end(); // this is a noop
  std::string output = testing::internal::GetCapturedStderr();
  EXPECT_EQ(output, "");
}

TEST_F(JITTimeLogTest, BuildTimingsAndDump) {
  auto c_time = std::chrono::steady_clock::now();

  CompilationPhaseTimer compilation_phase_timer("function_name", [&c_time]() {
    c_time += std::chrono::milliseconds(20);
    return c_time;
  });
  compilation_phase_timer.start("Overall compilation");
  compilation_phase_timer.start("Subphase 1");
  compilation_phase_timer.start("Subsubphase 1");
  compilation_phase_timer.end();
  compilation_phase_timer.end();
  compilation_phase_timer.start("Subphase 2");
  compilation_phase_timer.end();
  testing::internal::CaptureStderr();
  compilation_phase_timer.end();
  std::string output = testing::internal::GetCapturedStderr();
  EXPECT_TRUE(
      output.find(
          R"( -- Compilation phase time breakdown for function_name
Phase                Time/µs       Leaf/%     Sub Phase/%     Unattributed Time/µs|%
>Overall compilation 140000                   100.0           60000 | 42.9
 >Subphase 1         60000                     75.0           40000 | 66.7
  >Subsubphase 1     20000         50.0       100.0
 >Subphase 2         20000         50.0        25.0

)") != std::string::npos);
}
