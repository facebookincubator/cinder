// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
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
