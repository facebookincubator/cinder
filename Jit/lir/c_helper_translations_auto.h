// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __C_HELPER_TRANSLATIONS_AUTO_H__
#define __C_HELPER_TRANSLATIONS_AUTO_H__

#include <cstdint>
#include <initializer_list>
#include <utility>

namespace jit {
namespace lir {

// kCHelperMappingAuto maps C helper function memory addresses to
// their LIR string.
// The LIR strings are automatically generated.
extern const std::initializer_list<std::pair<const uint64_t, const char*>>
    kCHelperMappingAuto;

} // namespace lir
} // namespace jit

#endif
