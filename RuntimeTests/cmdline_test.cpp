// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/codegen/copy_graph.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/jit_list.h"
#include "Jit/lir/inliner.h"
#include "Jit/perf_jitdump.h"
#include "Jit/profile_data.h"
#include "Jit/pyjit.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

#include <dis-asm.h>
#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

// Here we make sure that the JIT specific command line arguments
// are being processed correctly to have the required effect
// on the JIT config

using namespace jit;
using namespace std;

using namespace jit::lir;

class CmdLineTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
  }

  void TearDown() override {
    RuntimeTest::TearDown();
  }
};

int try_flag_and_envvar_effect(
    const wchar_t* flag,
    const char* env_name,
    function<void(void)> reset_vars,
    function<void(void)> conditions_to_check,
    bool enable_JIT = false,
    bool capture_stderr = false,
    bool capture_stdout = false) {
  // Shutdown the JIT so we can start it up again under different conditions.
  _PyJIT_Finalize();

  // As most tests don't use _PyJIT_Initialize() we allocated a global code
  // allocator "manually" in main.cpp. We now need to deallocate it so we can
  // call _PyJIT_Initialize safely.
  CodeAllocator::freeGlobalCodeAllocator();

  reset_vars(); // reset variable state before and
  // between flag and cmd line param runs

  int init_status = 0;

  PyObject* jit_xarg_key = nullptr;

  if (enable_JIT) {
    jit_xarg_key = addToXargsDict(L"jit");
  }

  // as env var
  if (nullptr != env_name) {
    if (capture_stderr) {
      testing::internal::CaptureStderr();
    }
    if (capture_stdout) {
      testing::internal::CaptureStdout();
    }

    const char* key = parseAndSetEnvVar(env_name);
    init_status = _PyJIT_Initialize();
    conditions_to_check();
    unsetenv(key);
    if (strcmp(env_name, key)) {
      free((char*)key);
    }
    _PyJIT_Finalize();
    reset_vars();
  }

  if (capture_stderr) {
    testing::internal::CaptureStderr();
  }
  if (capture_stdout) {
    testing::internal::CaptureStdout();
  }
  // sneak in a command line argument
  PyObject* to_remove = addToXargsDict(flag);
  init_status += _PyJIT_Initialize();
  conditions_to_check();
  PyDict_DelItem(PySys_GetXOptions(), to_remove);
  Py_DECREF(to_remove);

  if (nullptr != jit_xarg_key) {
    PyDict_DelItem(PySys_GetXOptions(), jit_xarg_key);
    Py_DECREF(jit_xarg_key);
  }

  _PyJIT_Finalize();
  reset_vars();
  CodeAllocator::makeGlobalCodeAllocator();

  return init_status;
}

TEST_F(CmdLineTest, BasicFlags) {
  // easy flags that don't interact with one another in tricky ways
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-debug",
          "PYTHONJITDEBUG",
          []() {
            g_debug = 0;
            g_debug_verbose = 0;
          },
          []() {
            ASSERT_EQ(g_debug, 1);
            ASSERT_EQ(g_debug_verbose, 1);
          }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-debug-refcount",
          "PYTHONJITDEBUGREFCOUNT",
          []() { g_debug_refcount = 0; },
          []() { ASSERT_EQ(g_debug_refcount, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-hir",
          "PYTHONJITDUMPHIR",
          []() { g_dump_hir = 0; },
          []() { ASSERT_EQ(g_dump_hir, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-hir-passes",
          "PYTHONJITDUMPHIRPASSES",
          []() { g_dump_hir_passes = 0; },
          []() { ASSERT_EQ(g_dump_hir_passes, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-final-hir",
          "PYTHONJITDUMPFINALHIR",
          []() { g_dump_final_hir = 0; },
          []() { ASSERT_EQ(g_dump_final_hir, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-lir",
          "PYTHONJITDUMPLIR",
          []() { g_dump_lir = 0; },
          []() { ASSERT_EQ(g_dump_lir, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-lir-no-origin",
          "PYTHONJITDUMPLIRNOORIGIN",
          []() {
            g_dump_lir = 0;
            g_dump_lir_no_origin = 0;
          },
          []() {
            ASSERT_EQ(g_dump_lir, 1);
            ASSERT_EQ(g_dump_lir_no_origin, 1);
          }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-c-helper",
          "PYTHONJITDUMPCHELPER",
          []() { g_dump_c_helper = 0; },
          []() { ASSERT_EQ(g_dump_c_helper, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-disas-funcs",
          "PYTHONJITDISASFUNCS",
          []() { g_dump_asm = 0; },
          []() { ASSERT_EQ(g_dump_asm, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-asm",
          "PYTHONJITDUMPASM",
          []() { g_dump_asm = 0; },
          []() { ASSERT_EQ(g_dump_asm, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-gdb-support",
          "PYTHONJITGDBSUPPORT",
          []() {
            g_debug = 0;
            g_gdb_support = 0;
          },
          []() {
            ASSERT_EQ(g_debug, 1);
            ASSERT_EQ(g_gdb_support, 1);
          }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-gdb-stubs-support",
          "PYTHONJITGDBSTUBSSUPPORT",
          []() { g_gdb_stubs_support = 0; },
          []() { ASSERT_EQ(g_gdb_stubs_support, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-gdb-write-elf",
          "PYTHONJITGDBWRITEELF",
          []() {
            g_debug = 0;
            g_gdb_support = 0;
            g_gdb_write_elf_objects = 0;
          },
          []() {
            ASSERT_EQ(g_debug, 1);
            ASSERT_EQ(g_gdb_support, 1);
            ASSERT_EQ(g_gdb_write_elf_objects, 1);
          }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-dump-stats",
          "PYTHONJITDUMPSTATS",
          []() { g_dump_stats = 0; },
          []() { ASSERT_EQ(g_dump_stats, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-disable-lir-inliner",
          "PYTHONJITDISABLELIRINLINER",
          []() { g_disable_lir_inliner = 0; },
          []() { ASSERT_EQ(g_disable_lir_inliner, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-disable-huge-pages",
          "PYTHONJITDISABLEHUGEPAGES",
          []() {},
          []() { ASSERT_FALSE(_PyJIT_UseHugePages()); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-enable-jit-list-wildcards",
          "PYTHONJITENABLEJITLISTWILDCARDS",
          []() {},
          []() { ASSERT_EQ(_PyJIT_IsJitConfigAllow_jit_list_wildcards(), 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-all-static-functions",
          "PYTHONJITALLSTATICFUNCTIONS",
          []() {},
          []() {
            ASSERT_EQ(_PyJIT_IsJitConfigCompile_all_static_functions(), 1);
          }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-perfmap",
          "JIT_PERFMAP",
          []() { perf::jit_perfmap = 0; },
          []() { ASSERT_EQ(perf::jit_perfmap, 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-perf-dumpdir=/tmp/",
          "JIT_DUMPDIR=/tmp/",
          []() { perf::perf_jitdump_dir = ""; },
          []() { ASSERT_EQ(perf::perf_jitdump_dir, "/tmp/"); }),
      0);
}

TEST_F(CmdLineTest, JITEnable) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit",
          "PYTHONJIT",
          []() {},
          []() {
            ASSERT_EQ(_PyJIT_IsEnabled(), 1);
            ASSERT_EQ(
                _PyJIT_IsDisassemblySyntaxIntel(), 0); // default to AT&T syntax
          }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit=0",
          "PYTHONJIT=0",
          []() {},
          []() { ASSERT_EQ(_PyJIT_IsEnabled(), 0); }),
      0);
}

// start of tests associated with flags the setting of which is dependant upon
// if jit is enabled
TEST_F(CmdLineTest, JITEnabledFlags_ShadowFrame) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-shadow-frame",
          "PYTHONJITSHADOWFRAME",
          []() {},
          []() { ASSERT_FALSE(_PyJIT_ShadowFrame()); },
          false),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-shadow-frame",
          "PYTHONJITSHADOWFRAME",
          []() {},
          []() { ASSERT_TRUE(_PyJIT_ShadowFrame()); },
          true),
      0);

  // Explicitly disable it.
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-shadow-frame=0",
          "PYTHONJITSHADOWFRAME=0",
          []() {},
          []() { ASSERT_FALSE(_PyJIT_ShadowFrame()); },
          true),
      0);
}

TEST_F(CmdLineTest, JITEnabledFlags_MultithreadCompile) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-multithreaded-compile-test",
          "PYTHONJITMULTITHREADEDCOMPILETEST",
          []() {},
          []() {
            ASSERT_EQ(_PyJIT_IsJitConfigMultithreaded_compile_test(), 0);
          },
          false),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-multithreaded-compile-test",
          "PYTHONJITMULTITHREADEDCOMPILETEST",
          []() {},
          []() {
            ASSERT_EQ(_PyJIT_IsJitConfigMultithreaded_compile_test(), 1);
          },
          true),
      0);
}

TEST_F(CmdLineTest, JITEnabledFlags_MatchLineNumbers) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-list-match-line-numbers",
          "PYTHONJITLISTMATCHLINENUMBERS",
          []() { jitlist_match_line_numbers(false); },
          []() { ASSERT_FALSE(get_jitlist_match_line_numbers()); },
          false),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-list-match-line-numbers",
          "PYTHONJITLISTMATCHLINENUMBERS",
          []() { jitlist_match_line_numbers(false); },
          []() { ASSERT_TRUE(get_jitlist_match_line_numbers()); },
          true),
      0);
}

// end of tests associated with flags the setting of which is dependant upon if
// jit is enabled

TEST_F(CmdLineTest, JITEnabledFlags_BatchCompileWorkers) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-batch-compile-workers=21",
          "PYTHONJITBATCHCOMPILEWORKERS=21",
          []() {},
          []() { ASSERT_EQ(_PyJIT_GetJitConfigBatch_compile_workers(), 21); },
          true),
      0);
}

TEST_F(CmdLineTest, ASMSyntax) {
  // default when nothing defined is AT&T, covered in prvious test
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-asm-syntax=intel",
          "PYTHONJITASMSYNTAX=intel",
          []() { _PyJIT_SetDisassemblySyntaxATT(); },
          []() { ASSERT_EQ(_PyJIT_IsDisassemblySyntaxIntel(), 1); }),
      0);

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-asm-syntax=att",
          "PYTHONJITASMSYNTAX=att",
          []() { _PyJIT_SetDisassemblySyntaxATT(); },
          []() { ASSERT_EQ(_PyJIT_IsDisassemblySyntaxIntel(), 0); }),
      0);
}

const wchar_t* makeWideChar(const char* to_convert) {
  const size_t cSize = strlen(to_convert) + 1;
  wchar_t* wide = new wchar_t[cSize];
  mbstowcs(wide, to_convert, cSize);

  return wide;
}

TEST_F(CmdLineTest, JITList) {
  string list_file = tmpnam(nullptr);
  ofstream list_file_handle(list_file);
  list_file_handle.close();
  const wchar_t* xarg =
      makeWideChar(const_cast<char*>(("jit-list-file=" + list_file).c_str()));

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          xarg,
          const_cast<char*>(("PYTHONJITLISTFILE=" + list_file).c_str()),
          []() { _PyJIT_SetDisassemblySyntaxATT(); },
          []() { ASSERT_EQ(_PyJIT_IsEnabled(), 1); }),
      0);

  delete[] xarg;
  filesystem::remove(list_file);
}

TEST_F(CmdLineTest, JITLogFile) {
  string log_file = tmpnam(nullptr);
  ofstream log_file_handle(log_file);
  log_file_handle.close();
  const wchar_t* xarg =
      makeWideChar(const_cast<char*>(("jit-log-file=" + log_file).c_str()));

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          xarg,
          const_cast<char*>(("PYTHONJITLOGFILE=" + log_file).c_str()),
          []() { g_log_file = stderr; },
          []() { ASSERT_NE(g_log_file, stderr); }),
      0);

  delete[] xarg;
  filesystem::remove(log_file);
}

TEST_F(CmdLineTest, ExplicitJITDisable) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-disable",
          "PYTHONJITDISABLE",
          []() {},
          []() { ASSERT_EQ(_PyJIT_IsEnabled(), 0); },
          true),
      0);
}

TEST_F(CmdLineTest, WriteProfile) {
  string list_file = tmpnam(nullptr);

  const wchar_t* xarg = makeWideChar(
      const_cast<char*>(("jit-write-profile=" + list_file).c_str()));

  ASSERT_EQ(
      try_flag_and_envvar_effect(
          xarg,
          const_cast<char*>(("PYTHONJITWRITEPROFILE=" + list_file).c_str()),
          []() { _PyJIT_SetProfileNewInterpThreads(0); },
          []() { ASSERT_EQ(_PyJIT_GetProfileNewInterpThreads(), 1); }),
      0);

  delete[] xarg;

  filesystem::remove(list_file);
}

TEST_F(CmdLineTest, ProfileInterp) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-profile-interp",
          "PYTHONJITPROFILEINTERP",
          []() { _PyJIT_SetProfileNewInterpThreads(0); },
          []() { ASSERT_EQ(_PyJIT_GetProfileNewInterpThreads(), 1); }),
      0);
}

TEST_F(CmdLineTest, ReadProfile) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-read-profile=fname",
          "PYTHONJITREADPROFILE=fname",
          []() {},
          []() {
            ASSERT_TRUE(
                testing::internal::GetCapturedStderr().find(
                    "Loading profile data from fname") != std::string::npos);
          },
          false,
          true),
      -2);
}

TEST_F(CmdLineTest, DisplayHelpMessage) {
  ASSERT_EQ(
      try_flag_and_envvar_effect(
          L"jit-help",
          nullptr,
          []() {},
          []() {
            ASSERT_TRUE(
                testing::internal::GetCapturedStdout().find(
                    "-X opt : set Cinder JIT-specific option.") !=
                std::string::npos);
          },
          false,
          false,
          true),
      -2);
}
