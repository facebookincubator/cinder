// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace jit {
namespace lir {

// kCHelperMapping maps C helper function memory addresses to
// their LIR string.
extern const std::unordered_map<uint64_t, std::string> kCHelperMapping;

} // namespace lir
} // namespace jit
