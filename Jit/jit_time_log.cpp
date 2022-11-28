// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/jit_time_log.h"

#include "fmt/core.h"

#include "Jit/log.h"

#include <fmt/format.h>
#include <parallel_hashmap/phmap.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace jit {

static std::vector<std::string> capture_compilation_times_for;

void parseAndSetFuncList(const std::string& flag_value) {
  capture_compilation_times_for.clear();

  std::stringstream ss(flag_value);

  while (ss.good()) {
    std::string substr;
    getline(ss, substr, ',');
    if (!substr.empty()) {
      capture_compilation_times_for.emplace_back(substr);
    }
  }
}

bool isMatch(
    const std::string& word,
    int n,
    const std::string& pattern,
    int m) {
  if (m == static_cast<int>(pattern.size())) {
    return n == static_cast<int>(word.size());
  }

  if (n == static_cast<int>(word.size())) {
    for (int i = m; i < static_cast<int>(pattern.size()); i++) {
      if (pattern[i] != '*') {
        return false;
      }
    }
    return true;
  }

  if (pattern[m] == '?' || pattern[m] == word[n]) {
    return isMatch(word, n + 1, pattern, m + 1);
  }

  if (pattern[m] == '*') {
    return isMatch(word, n + 1, pattern, m) || isMatch(word, n, pattern, m + 1);
  }
  return false;
}

bool isMatch(const std::string& word, const std::string& pattern) {
  return isMatch(word, 0, pattern, 0);
}

bool captureCompilationTimeFor(const std::string& function_name) {
  for (std::string pattern : capture_compilation_times_for) {
    if (isMatch(function_name, pattern)) {
      return true;
    }
  }
  return false;
}

void CompilationPhaseTimer::dumpPhaseTimingsAndTidy() {
  // flatten phase timings into one vector
  std::vector<std::tuple<int, SubPhaseTimer*>> toproc;
  std::vector<std::tuple<int, SubPhaseTimer*, int, bool, int>> flat_rows;
  phmap::flat_hash_map<SubPhaseTimer*, int> subphase_to_group_total_time;

  toproc.emplace_back(0, root_.get());
  while (!toproc.empty()) {
    auto elem = toproc.back();
    toproc.pop_back();
    auto& [indent, phase] = elem;

    int subphase_group_total = 0;
    for (auto it = phase->children.rbegin(); it != phase->children.rend();
         ++it) {
      toproc.emplace_back(indent + 1, (*it).get());
      subphase_group_total +=
          std::chrono::duration_cast<std::chrono::microseconds>(
              (*it)->end - (*it)->start)
              .count();
    }

    for (auto it = phase->children.rbegin(); it != phase->children.rend();
         ++it) {
      subphase_to_group_total_time.emplace((*it).get(), subphase_group_total);
    }

    int time_span = std::chrono::duration_cast<std::chrono::microseconds>(
                        phase->end - phase->start)
                        .count();

    bool isLeaf = phase->children.empty();
    flat_rows.emplace_back(
        indent, phase, time_span, isLeaf, time_span - subphase_group_total);
  }

  int longest_phase = 0;
  int ts_digits = 0;
  int unattributed_time_digitls = 0;
  double leaf_total_time = 0;
  for (auto it = flat_rows.begin(); it != flat_rows.end(); ++it) {
    auto& [indent, phase, time_span, is_leaf, unattributed_time] = *it;

    longest_phase =
        std::max(longest_phase, int(phase->sub_phase_name.size() + 1 + indent));
    ts_digits = std::max(ts_digits, int(log10(time_span) + 1));
    unattributed_time_digitls =
        std::max(unattributed_time_digitls, int(log10(unattributed_time) + 1));

    if (is_leaf) {
      int time_span = std::get<2>(*it);
      leaf_total_time += time_span;
    }
  }

  std::string phase_info;
  for (auto it = flat_rows.begin(); it != flat_rows.end(); ++it) {
    auto& [indent, phase, time_span, is_leaf, unattributed_time] = *it;

    phase_info += fmt::format(
        "{:<{}}",
        fmt::format("{}>{}", std::string(indent, ' '), phase->sub_phase_name),
        longest_phase);

    phase_info +=
        fmt::format(" {}", fmt::format("{:<{}}", time_span, ts_digits + 7));

    if (is_leaf) {
      phase_info +=
          fmt::format("{:>5.1f} ", (time_span / leaf_total_time) * 100);
    } else {
      phase_info += "      ";
    }

    phase_info += "      ";

    if (subphase_to_group_total_time.contains(phase)) {
      phase_info += fmt::format(
          "{:>5.1f}",
          (time_span / ((double)subphase_to_group_total_time[phase])) * 100);
    } else {
      phase_info += "100.0";
    }

    if (!is_leaf) {
      phase_info += "           ";
      phase_info += fmt::format(
          "{}|",
          fmt::format("{:<{}} ", unattributed_time, unattributed_time_digitls));
      phase_info += fmt::format(
          "{:>5.1f}", unattributed_time / ((double)time_span) * 100);
    }

    phase_info += "\n";
  }

  std::string header = fmt::format(
      "Phase{}Time/µs{}Leaf/%     Sub Phase/%     Unattributed Time/µs|%\n",
      std::string(longest_phase - 4, ' '),
      std::string(ts_digits + 1, ' '));

  JIT_LOG(
      "Compilation phase time breakdown for %s\n%s",
      function_name_,
      header + phase_info);

  root_ = nullptr;
}

void CompilationPhaseTimer::start(const std::string& phase_name) {
  JIT_CHECK(!phase_name.empty(), "Phase name cannot be empty");
  auto timer = std::make_unique<SubPhaseTimer>(phase_name);
  SubPhaseTimer* timerptr = timer.get();
  if (root_ == nullptr) {
    root_ = std::move(timer);
  } else {
    current_phase_stack_.back()->children.emplace_back(std::move(timer));
  }
  current_phase_stack_.push_back(timerptr);
  timerptr->start = time_provider_();
}

void CompilationPhaseTimer::end() {
  if (current_phase_stack_.empty()) {
    // if called end at root_ level already then stop
    return;
  }

  current_phase_stack_.back()->end = time_provider_();

  if (current_phase_stack_.back() == root_.get()) {
    dumpPhaseTimingsAndTidy();
  }

  current_phase_stack_.pop_back();
}

} // namespace jit
