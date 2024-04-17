// Copyright (c) Meta Platforms, Inc. and affiliates.

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

// List of HIR optimization passes to run.
struct HIROptimizations {
  bool begin_inlined_function_elim{true};
  bool builtin_load_method_elim{true};
  bool clean_cfg{true};
  bool dead_code_elim{true};
  bool dynamic_comparison_elim{true};
  bool guard_type_removal{true};
  // TODO(T156009029): Inliner should be on by default.
  bool inliner{false};
  bool phi_elim{true};
  bool simplify{true};
};

struct Config {
  // Initialization state of the JIT.
  InitState init_state{InitState::kNotInitialized};
  // Set when the JIT is initialized and enabled.
  bool is_enabled{false};
  // Ignore CLI arguments and environment variables, always initialize the JIT
  // without enabling it.  Intended for testing.
  bool force_init{false};
  FrameMode frame_mode{FrameMode::kNormal};
  bool allow_jit_list_wildcards{false};
  bool compile_all_static_functions{false};
  bool multiple_code_sections{false};
  bool multithreaded_compile_test{false};
  bool use_huge_pages{true};
  // Assume that code objects are unchanged across Python function calls.
  bool stable_code{true};
  // Assume that globals and builtins dictionaries, but not their contents, are
  // unchanged across Python function calls.
  bool stable_globals{true};
  // Use inline caches for attribute accesses.
  bool attr_caches{true};
  HIROptimizations hir_opts;
  size_t batch_compile_workers{0};
  // Sizes (in bytes) of the hot and cold code sections. Only applicable if
  // multiple code sections are enabled.
  size_t cold_code_section_size{0};
  size_t hot_code_section_size{0};
  // Memory threshold after which we stop jitting.
  size_t max_code_size{0};
  // Size (in number of entries) of the LoadAttrCached and StoreAttrCached
  // inline caches used by the JIT.
  uint32_t attr_cache_size{1};
  uint32_t auto_jit_threshold{0};
  uint32_t auto_jit_profile_threshold{0};
  bool compile_perf_trampoline_prefork{false};
};

// Get the JIT's current config object.
const Config& getConfig();

// Get the JIT's current config object with the intent of modifying it.
Config& getMutableConfig();

} // namespace jit
