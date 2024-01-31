// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/jit_flag_processor.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

#include <exception>
#include <iostream>
#include <list>

// tests to ensure FlagProcessor is correctly processing command
// line and enviroment variable parameters and producing a
// pretty help message for JIT associated X parameters

using JITFlagProcessorTest = RuntimeTest;

using namespace jit;
using namespace std;

TEST_F(JITFlagProcessorTest, EmptyHelpMessage) {
  auto inst = FlagProcessor();
  ASSERT_EQ(
      inst.jitXOptionHelpMessage(),
      "-X opt : set Cinder JIT-specific option. The following options are "
      "available:\n\n");
}

TEST_F(JITFlagProcessorTest, SimpleHelpMessage) {
  auto flag_processor = FlagProcessor();
  int dummy;
  string a_string = "";
  size_t a_long;
  flag_processor.addOption(
      "a-flag", "ENVIROMENT_VARIABLE", dummy, "Help Message about the flag");
  flag_processor.addOption(
      "another-flag",
      "ANOTHER_ENVVAR",
      dummy,
      "Another help Message about the flag");
  flag_processor
      .addOption("test-string-flag", "STRINGENVVAR", a_string, "test flag desc")
      .withFlagParamName("STR_PARAM");
  flag_processor
      .addOption("test-long-flag", "LONGFLAG", a_long, "test long desc")
      .withFlagParamName("LONG_PARAM");

  ASSERT_EQ(
      flag_processor.jitXOptionHelpMessage(),
      "-X opt : set Cinder JIT-specific option. The following options are "
      "available:\n\n         -X a-flag: Help Message about the flag; also "
      "ENVIROMENT_VARIABLE\n         -X another-flag: Another help Message "
      "about the flag; also ANOTHER_ENVVAR\n         -X "
      "test-string-flag=<STR_PARAM>: test flag desc; also "
      "STRINGENVVAR=<STR_PARAM>\n         -X test-long-flag=<LONG_PARAM>: test "
      "long desc; also LONGFLAG=<LONG_PARAM>\n");
}

TEST_F(JITFlagProcessorTest, HiddenFlags) {
  // hidden flags are not shown on the help text
  auto flag_processor = FlagProcessor();
  int dummy;
  flag_processor
      .addOption(
          "a-flag", "ENVIROMENT_VARIABLE", dummy, "Help Message about the flag")
      .isHiddenFlag(true);

  ASSERT_EQ(
      flag_processor.jitXOptionHelpMessage(),
      "-X opt : set Cinder JIT-specific option. The following options are "
      "available:\n\n");
}

TEST_F(JITFlagProcessorTest, HasOptionsSet) {
  auto inst = FlagProcessor();
  int dummy;
  inst.addOption(
      "a-flag", "ENVIROMENT_VARIABLE", dummy, "Help Message about the flag");
  ASSERT_TRUE(inst.hasOptions());
}

TEST_F(JITFlagProcessorTest, LongLineHelpMessage) {
  auto inst = FlagProcessor();
  int dummy;
  inst.addOption(
      "a-flag",
      "ENVIROMENT_VARIABLE",
      dummy,
      "Help Message about a flag which is a very long description that is way "
      "longer than 80 characters the flag");
  ASSERT_EQ(
      inst.jitXOptionHelpMessage(),
      "-X opt : set Cinder JIT-specific option. The following options are "
      "available:\n\n         -X a-flag: Help Message about a flag which is a "
      "very long description that\n             is way longer than 80 "
      "characters the flag; also ENVIROMENT_VARIABLE\n");
}

void try_flag_and_envvar_effect(
    FlagProcessor& flag_processor,
    const wchar_t* flag,
    const char* env_name,
    function<void(void)> reset_vars,
    function<void(void)> conditions_to_check,
    bool capture_stderr = false) {
  PyObject* xoptions = PySys_GetXOptions();

  reset_vars();
  int prev_g_debug_verbose = g_debug_verbose;
  if (capture_stderr) {
    g_debug_verbose = 1;
    testing::internal::CaptureStderr();
  }

  if (nullptr != env_name) {
    // try when set on cmd line as xarg
    const char* key = parseAndSetEnvVar(env_name);
    flag_processor.setFlags(xoptions);
    conditions_to_check();
    reset_vars();
    unsetenv(key);
    if (strcmp(env_name, key)) {
      free((char*)key);
    }
    if (capture_stderr) {
      testing::internal::CaptureStderr();
    }
  }

  // try as env variable
  PyObject* xarg_key = addToXargsDict(flag);
  flag_processor.setFlags(xoptions);
  PyDict_DelItem(xoptions, xarg_key);
  Py_DECREF(xarg_key);

  conditions_to_check();
  reset_vars();
  if (capture_stderr) {
    g_debug_verbose = prev_g_debug_verbose;
  }
}

TEST_F(JITFlagProcessorTest, VarsSetOncmdLineAndEnvVar) {
  // ensure value set to what flag is pointed to
  // works for strings, longs and boolean flags
  // via cmd line and env variables
  auto flag_processor = FlagProcessor();
  string dummy = "";
  int vanilla_flag = 0;
  size_t long_flag;

  flag_processor
      .addOption("test-string-flag", "STRINGENVVAR", dummy, "test flag")
      .withFlagParamName("PARAM");
  flag_processor.addOption(
      "test-vanilla-flag", "VANILLAFLAG", vanilla_flag, "test flag2");
  flag_processor.addOption("test-long-flag", "LONGFLAG", long_flag, "test long")
      .withFlagParamName("PARAM");

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-string-flag=theValue",
      "STRINGENVVAR=theValue",
      [&dummy]() { dummy = ""; },
      [&dummy]() { ASSERT_EQ(dummy, "theValue"); });

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-vanilla-flag",
      "VANILLAFLAG",
      [&vanilla_flag]() { vanilla_flag = 0; },
      [&vanilla_flag]() { ASSERT_EQ(vanilla_flag, 1); });

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-long-flag=123123",
      "LONGFLAG=123123",
      [&long_flag]() { long_flag = 0; },
      [&long_flag]() { ASSERT_EQ(long_flag, 123123); });
}

TEST_F(JITFlagProcessorTest, Callback) {
  // some callbacks can be quite tricky
  auto flag_processor = FlagProcessor();
  string one_variable = "";
  int another_variable = 0;

  flag_processor.addOption(
      "test-string-flag",
      "STRINGENVVAR",
      [&one_variable, &another_variable](string what) {
        one_variable = what;
        another_variable = 99;
      },
      "test flag");

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-string-flag=something",
      "STRINGENVVAR=something",
      [&one_variable, &another_variable]() {
        one_variable = "";
        another_variable = 0;
      },
      [&one_variable, &another_variable]() {
        ASSERT_EQ(one_variable, "something");
        ASSERT_EQ(another_variable, 99);
      });
}

TEST_F(JITFlagProcessorTest, DebugLoggingCorrect) {
  // correct logging formatted when flags matched?
  auto flag_processor = FlagProcessor();
  string dummy = "";

  flag_processor.addOption(
      "test-string-flag", "STRINGENVVAR", dummy, "test flag description here");

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-string-flag=valueString",
      "STRINGENVVAR=valueString",
      [&dummy]() { dummy = ""; },
      [&dummy]() {
        ASSERT_EQ(dummy, "valueString");
        ASSERT_TRUE(
            testing::internal::GetCapturedStderr().find(
                "has been specified - test flag description here") !=
            std::string::npos);
      },
      true);
}

TEST_F(JITFlagProcessorTest, DebugOverrideLoggingCorrect) {
  // correct logging when default string to log is overriden
  auto flag_processor = FlagProcessor();
  string dummy = "";

  flag_processor
      .addOption(
          "test-string-flag",
          "STRINGENVVAR",
          dummy,
          "test flag description here")
      .withDebugMessageOverride("custom message about flag being set");

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-string-flag=valueString",
      "STRINGENVVAR=valueString",
      [&dummy]() { dummy = ""; },
      [&dummy]() {
        ASSERT_EQ(dummy, "valueString");
        ASSERT_TRUE(
            testing::internal::GetCapturedStderr().find(
                "custom message about flag being set") != std::string::npos);
      },
      true);
}

TEST_F(JITFlagProcessorTest, FlagWithNoEnvVar) {
  // some flags have no enviroment variable associated with them
  auto flag_processor = FlagProcessor();
  int the_variable = 0;

  flag_processor.addOption("test-flag", "", the_variable, "test flag");

  try_flag_and_envvar_effect(
      flag_processor,
      L"test-flag",
      nullptr,
      [&the_variable]() { the_variable = 0; },
      [&the_variable]() { ASSERT_EQ(the_variable, 1); });
}
