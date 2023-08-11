// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include <cstddef>
#include <cstdint>

namespace jit {

enum InitStateConfig : uint8_t {
  JIT_NOT_INITIALIZED,
  JIT_INITIALIZED,
  JIT_FINALIZED,
};

enum FrameModeConfig : uint8_t {
  PY_FRAME,
  SHADOW_FRAME,
};

struct Config {
  bool is_enabled{false};
  FrameModeConfig frame_mode{PY_FRAME};
  InitStateConfig init_state{JIT_NOT_INITIALIZED};
  bool allow_jit_list_wildcards{false};
  bool compile_all_static_functions{false};
  bool hir_inliner_enabled{false};
  bool multiple_code_sections{false};
  bool multithreaded_compile_test{false};
  bool use_huge_pages{true};
  size_t batch_compile_workers{0};
  size_t cold_code_section_size{0};
  size_t hot_code_section_size{0};
  uint32_t attr_cache_size{1};
  uint32_t auto_jit_threshold{0};
  int code_watcher_id{-1};
};

// Get the JIT's current config object.
const Config& getConfig();

// Get the JIT's current config object with the intent of modifying it.
Config& getMutableConfig();

} // namespace jit
