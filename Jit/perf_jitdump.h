// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __JIT_PERF_JITDUMP_H__
#define __JIT_PERF_JITDUMP_H__

#include <cstddef>
#include <string>

namespace jit {
namespace perf {

extern const std::string kDefaultSymbolPrefix;
extern const std::string kFuncSymbolPrefix;
extern const std::string kNoFrameSymbolPrefix;
extern const std::string kShadowFrameSymbolPrefix;

// Write out perf metadata for the given compiled function, depending on what's
// enabled in the environment:
//
// JIT_PERFMAP: If non-empty, write out /tmp/perf-<pid>.map for JIT symbols.
//
// JIT_DUMPDIR: If non-empty, must be an absolute path to a directory that
//              exists. A perf jitdump file will be written to this directory.
void registerFunction(
    void* code,
    std::size_t size,
    const std::string& name,
    const std::string& prefix = kDefaultSymbolPrefix);

// Perform any cleanup needed in a child process after fork().
void afterForkChild();

} // namespace perf
} // namespace jit

#endif
