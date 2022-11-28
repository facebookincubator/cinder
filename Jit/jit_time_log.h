// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/containers.h"
#include "Jit/util.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace jit {

using time_point = std::chrono::steady_clock::time_point;

#define COMPILE_TIMER(com_phase_timer, phase_name, block) \
  if (nullptr != com_phase_timer) {                       \
    com_phase_timer->start(phase_name);                   \
    block;                                                \
    com_phase_timer->end();                               \
  } else {                                                \
    block;                                                \
  }

// flag_value is expected to be the value associate with a the flag jit-time
// and represents the function list for which compilation phase times are
// expected to be captured for such that a breakdown may be presented
// the individual functions are comma seperated and may contain wildcards
// wildcards will be glob processed (not treated as regex)
// e.g. -X jit-time=*
//      -X jit-time=__main__:*
//      -X jit-time=__main__:foo
//      -X jit-time=__main__:foo, __main__:bar
void parseAndSetFuncList(const std::string& flag_value);

// check to see if a function_name_ matches any of the specified function
// patterns defined via parseAndSetFuncList
bool captureCompilationTimeFor(const std::string& function_name);

class SubPhaseTimer {
 public:
  explicit SubPhaseTimer(const std::string& sub_phase_name)
      : sub_phase_name{sub_phase_name} {}

  std::string sub_phase_name;
  std::vector<std::unique_ptr<SubPhaseTimer>> children;
  time_point start;
  time_point end;

 private:
  DISALLOW_COPY_AND_ASSIGN(SubPhaseTimer);
};

class CompilationPhaseTimer {
 public:
  CompilationPhaseTimer(
      const std::string& function_name_,
      const std::function<time_point()>& time_provider_)
      : function_name_(function_name_), time_provider_(time_provider_){};
  CompilationPhaseTimer(const std::string& function_name_)
      : CompilationPhaseTimer(function_name_, []() {
          return std::chrono::steady_clock::now();
        }){};

  void start(const std::string& phase_name);

  // when final start end pair is called with no nesting, then
  // dumpPhaseTimingsAndTidy is invoked and the output dumped to the JIT debug
  // log
  void end();

 private:
  DISALLOW_COPY_AND_ASSIGN(CompilationPhaseTimer);
  std::vector<SubPhaseTimer*> current_phase_stack_;
  std::string function_name_;
  std::function<time_point()> time_provider_;
  std::unique_ptr<SubPhaseTimer> root_{nullptr};

  // Dumps a table of the following information concerning each phase
  // Phase Name, Time/µs    Leaf/%     Sub Phase/%     Unattributed Time/µs|%
  // * Phase Name - Descriptive Phase or sub phase name
  // * Time/µs - Time in microseconds spent in phase
  // * Leaf/% - Proportion of time spent in phases which have no sub phases
  // * Sub Phase/% - Proportion of time spent in sub phase relative to other
  //    phases sharing the same common parent phase
  // * Unattributed Time/µs|% - Time reported at phase level minus sum of time
  //    spent in subphases of that phase. Reported as microseconds and % of
  //    total phase time. Useful for detecting opportunities to drill into
  //    more detail of a phase and detecting bugs. For example, a new
  //    compilation phase without a start end wrapper around it would
  //    manifest as a large Unattributed Time value on the parent phase -
  //    thus indicating a problem.
  void dumpPhaseTimingsAndTidy();
};

} // namespace jit
