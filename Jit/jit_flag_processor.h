// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace std;
namespace jit {

struct Option {
  // required
  string cmdline_flag;
  string environment_variable;
  function<void(string)> callback_on_match;
  string flag_description;

  // optional
  string flag_param_name;
  string debug_message;
  bool hidden_flag = false;

  Option(){};

  string getFormatted_cmdline_flag();
  string getFormatted_environment_variable();

  Option(
      const string cmdline_flag,
      const string environment_variable,
      function<void(string)> callback_on_match,
      const string flag_description) {
    this->cmdline_flag = cmdline_flag;
    this->environment_variable = environment_variable;
    this->callback_on_match = callback_on_match;
    this->flag_description = flag_description;
  }

  // Normally, when the relevant flag is set a debug log message
  // will be generated. By setting the debug message here, this
  // auto generated message will be overriden
  Option& withDebugMessageOverride(const string debug_message) {
    this->debug_message = debug_message;
    return *this;
  }

  // Allows the definition of a flag parameter name which will
  // apear on the expanded help message for the Option
  Option& withFlagParamName(const string flag_param_name) {
    this->flag_param_name = flag_param_name;
    return *this;
  }

  // Set this to true to hide the flag from the help text
  Option& isHiddenFlag(const bool hidden_flag) {
    this->hidden_flag = hidden_flag;
    return *this;
  }

 private:
  string getFormatted(string);
};

struct FlagProcessor {
  Option& addOption(
      const string cmdline_flag,
      const string environment_variable,
      function<void(string)> callback_on_match,
      const string flag_description);
  Option& addOption(
      const string cmdline_flag,
      const string environment_variable,
      int& variable_to_bind_to,
      const string flag_description);
  Option& addOption(
      const string cmdline_flag,
      const string environment_variable,
      size_t& variable_to_bind_to,
      const string flag_description);
  Option& addOption(
      const string cmdline_flag,
      const string environment_variable,
      string& variable_to_bind_to,
      const string flag_description);

  // passing the xoptions dict from the command line will result in the
  // associated 'variable_to_bind_to' previously passed being assigned with the
  // appropriate value if the key exists in this map. If it cannot be found then
  // the environment variables will be interrogated and the associated value
  // assigned.
  // * In the case of a string variable_to_bind_to this will be a string
  // representation of the value
  // * In the case of a size_t variable_to_bind_to this will be a size_t
  // parse of the value
  // * In the case of a int variable_to_bind_to this will be a 1 (no further
  // parsing takes place).
  void setFlags(PyObject* xoptions);

  // Generates a nicely formatted representation of the added Option
  // flag_description's previously registered
  string jitXOptionHelpMessage();

  // Return true if one or more flags have been registered
  bool hasOptions();

 private:
  vector<std::unique_ptr<Option>> options_;
};
} // namespace jit
