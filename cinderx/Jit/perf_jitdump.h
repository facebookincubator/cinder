// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace jit::perf {

extern const std::string kDefaultSymbolPrefix;
extern const std::string kFuncSymbolPrefix;
extern const std::string kNoFrameSymbolPrefix;
extern const std::string kShadowFrameSymbolPrefix;

// Write out perf metadata for the given compiled function, depending on what's
// enabled in the environment:
//
// jit_perfmap: If != 0, write out /tmp/perf-<pid>.map for JIT symbols.
//
extern int jit_perfmap;

// perf_jitdump_dir: If non-empty, must be an absolute path to a directory that
//                   exists. A perf jitdump file will be written to this
//                   directory.
extern std::string perf_jitdump_dir;

void registerFunction(
    const std::vector<std::pair<void*, std::size_t>>& code_sections,
    const std::string& name,
    const std::string& prefix = kDefaultSymbolPrefix);

// After-fork callback for child processes. Performs any cleanup necessary for
// per-process state, including handling of Linux perf pid maps.
void afterForkChild();

} // namespace jit::perf
