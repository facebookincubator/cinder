// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace jit::lir {

// maps symbols such as functions to addresses
extern const std::unordered_map<std::string, uint64_t> kSymbolMapping;

} // namespace jit::lir
