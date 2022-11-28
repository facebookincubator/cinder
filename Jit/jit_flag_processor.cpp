// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/jit_flag_processor.h"

#include "Python.h"

#include "Jit/log.h"

#include <fmt/format.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using std::function;
using std::string;
using std::vector;

namespace jit {

static const string indent1 = "         ";
static const string indent2 = "             ";
static size_t line_length = 80 - indent1.size();

bool FlagProcessor::hasOptions() {
  return options_.size() > 0;
}

Option& FlagProcessor::addOption(
    const string cmdline_flag,
    const string environment_variable,
    const function<void(int)> callback,
    const string flag_description) {
  assert(!cmdline_flag.empty());
  assert(!flag_description.empty());

  function<void(string)> int_callback =
      [callback, &cmdline_flag, &environment_variable](string flag_value) {
        try {
          // The callback only gets called for empty X-options, not empty
          // environment variables. This makes `-X foo` equivalent to `-X
          // foo=1`, but `PYTHONFOO=` is not equivalent to `PYTHONFOO=1`.
          callback(flag_value == "" ? 1 : std::stoi(flag_value));
        } catch (std::exception const&) {
          JIT_LOG(
              "Invalid int value for %s/%s: %s",
              cmdline_flag,
              environment_variable,
              flag_value);
        }
      };

  return addOption(
      cmdline_flag, environment_variable, int_callback, flag_description);
}

Option& FlagProcessor::addOption(
    const string cmdline_flag,
    const string environment_variable,
    const function<void(string)> callback,
    const string flag_description) {
  assert(!cmdline_flag.empty());
  assert(!flag_description.empty());

  auto option = std::make_unique<Option>(
      cmdline_flag, environment_variable, callback, flag_description);
  Option& optref = *option;

  options_.push_back(std::move(option));

  return optref;
}

Option& FlagProcessor::addOption(
    const string cmdline_flag,
    const string environment_variable,
    string& variable_to_bind_to,
    const string flag_description) {
  function<void(string)> setter = [&variable_to_bind_to](string flag_value) {
    variable_to_bind_to = flag_value;
  };
  return addOption(
      cmdline_flag, environment_variable, setter, flag_description);
}

Option& FlagProcessor::addOption(
    const string cmdline_flag,
    const string environment_variable,
    int& variable_to_bind_to,
    const string flag_description) {
  function<void(int)> setter = [&variable_to_bind_to](int flag_value) {
    variable_to_bind_to = flag_value;
  };
  return addOption(
      cmdline_flag, environment_variable, setter, flag_description);
}

Option& FlagProcessor::addOption(
    const string cmdline_flag,
    const string environment_variable,
    size_t& variable_to_bind_to,
    const string flag_description) {
  function<void(string)> setter = [&variable_to_bind_to,
                                   &cmdline_flag,
                                   &environment_variable](string flag_value) {
    try {
      // The callback only gets called for empty X-options, not empty
      // environment variables. This makes `-X foo` equivalent to `-X foo=1`,
      // but `PYTHONFOO=` is not equivalent to `PYTHONFOO=1`.
      variable_to_bind_to = flag_value == "" ? 1 : std::stoull(flag_value);
    } catch (std::exception const&) {
      JIT_LOG(
          "Invalid unsigned long value for %s/%s: %s",
          cmdline_flag,
          environment_variable,
          flag_value);
    }
  };
  return addOption(
      cmdline_flag, environment_variable, setter, flag_description);
}

bool FlagProcessor::canHandle(const char* provided_option) {
  for (auto const& option : options_) {
    if (option->cmdline_flag == provided_option) {
      return true;
    }
  }
  return false;
}

void FlagProcessor::setFlags(PyObject* cmdline_args) {
  assert(cmdline_args != nullptr);

  for (auto const& option : options_) {
    PyObject* key = PyUnicode_FromString(option->cmdline_flag.c_str());
    assert(key != nullptr);

    PyObject* resolves_to = PyDict_GetItem(cmdline_args, key);
    Py_DECREF(key);
    string found;

    if (resolves_to != NULL) {
      const char* got =
          PyUnicode_Check(resolves_to) ? PyUnicode_AsUTF8(resolves_to) : "";
      option->callback_on_match(got);
      found = option->cmdline_flag;
    }
    if (found.empty() && !option->environment_variable.empty()) {
      // check to see if it can be found via an enviroment variable
      const char* envval = Py_GETENV(option->environment_variable.c_str());
      if (envval != nullptr && envval[0] != '\0') {
        option->callback_on_match(envval);
        found = option->environment_variable;
      }
    }

    if (!found.empty()) {
      // use overriden debug message if it's been defined
      JIT_DLOG(fmt::format(
                   "{} has been specified - {}",
                   found,
                   option->debug_message.empty() ? option->flag_description
                                                 : option->debug_message)
                   .c_str());
    }
  }

  PyObject* key;
  PyObject* value;
  auto jit_str = Ref<>::steal(PyUnicode_FromString("jit"));
  for (Py_ssize_t pos = 0; PyDict_Next(cmdline_args, &pos, &key, &value);) {
    int match = PyUnicode_Tailmatch(
        key, jit_str, /*start=*/0, /*end=*/3, /*direction=*/-1);
    JIT_DCHECK(match != -1, "An error occurred");
    const char* option = PyUnicode_AsUTF8(key);
    JIT_DCHECK(option != nullptr, "An error occurred");
    if (match && !canHandle(option)) {
      JIT_LOG("Warning: JIT cannot handle X-option %s", option);
    }
  }
}

// split long lines into many, but only cut on whitespace
static string multi_line_split_(string src_string) {
  vector<string> temp_result(1);

  std::stringstream stm(src_string);

  string word;
  bool addIndent = false;
  while (stm >> word) {
    if (addIndent) {
      temp_result.push_back(indent2);
    }

    if ((temp_result.back().size() + word.size()) <= line_length) {
      temp_result.back() += word + ' ';
      addIndent = false;
    } else {
      temp_result.push_back(word + "\n");
      addIndent = true;
    }
  }

  temp_result.back().pop_back();
  string result;
  for (const auto& item : temp_result) {
    result += item;
  }
  return result;
}

string Option::getFormatted(string left_hand_side) {
  if (!flag_param_name.empty()) {
    return fmt::format("{}=<{}>", left_hand_side, flag_param_name);
  }
  return left_hand_side;
}

string Option::getFormatted_cmdline_flag() {
  return getFormatted(cmdline_flag);
}

string Option::getFormatted_environment_variable() {
  return environment_variable.empty() ? "" : getFormatted(environment_variable);
}

string FlagProcessor::jitXOptionHelpMessage() {
  string ret =
      "-X opt : set Cinder JIT-specific option. The following options are "
      "available:\n\n";
  for (auto const& option : options_) {
    if (!option->hidden_flag) {
      string fmt_env_var = option->getFormatted_environment_variable().empty()
          ? ""
          : fmt::format(
                "; also {}", option->getFormatted_environment_variable());
      ret += indent1 +
          multi_line_split_(fmt::format(
              "-X {}: {}{}\n",
              option->getFormatted_cmdline_flag(),
              option->flag_description,
              fmt_env_var)) +
          "\n";
    }
  }
  return ret;
}

} // namespace jit
