// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include <cstdint>
#include <initializer_list>
#include <utility>

namespace jit::lir {

// kCHelperMappingAuto maps C helper function memory addresses to
// their LIR string.
// The LIR strings are automatically generated.
extern const std::initializer_list<std::pair<const uint64_t, const char*>>
    kCHelperMappingAuto;

} // namespace jit::lir
