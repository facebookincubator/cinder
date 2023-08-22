// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include <cstddef>
#include <cstdint>

namespace jit {

enum class InitState : uint8_t {
  kNotInitialized,
  kInitialized,
  kFinalized,
};

enum class FrameMode : uint8_t {
  kNormal,
  kShadow,
};

struct Config {
  bool is_enabled{false};
  FrameMode frame_mode{FrameMode::kNormal};
  InitState init_state{InitState::kNotInitialized};
  bool allow_jit_list_wildcards{false};
  bool compile_all_static_functions{false};
  bool hir_inliner_enabled{false};
  bool multiple_code_sections{false};
  bool multithreaded_compile_test{false};
  bool use_huge_pages{true};
  size_t batch_compile_workers{0};
  // Sizes (in bytes) of the hot and cold code sections. Only applicable if
  // multiple code sections are enabled.
  size_t cold_code_section_size{0};
  size_t hot_code_section_size{0};
  // Size (in number of entries) of the LoadAttr and StoreAttr inline caches
  // used by the JIT.
  uint32_t attr_cache_size{1};
  uint32_t auto_jit_threshold{0};
  uint32_t auto_jit_profile_threshold{0};
};

// Get the JIT's current config object.
const Config& getConfig();

// Get the JIT's current config object with the intent of modifying it.
Config& getMutableConfig();

} // namespace jit
