// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

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

// Perform any cleanup needed in a child process after fork().
void afterForkChild();

} // namespace jit::perf
