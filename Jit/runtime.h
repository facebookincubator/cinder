// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/containers.h"
#include "Jit/deopt.h"
#include "Jit/fixed_type_profiler.h"
#include "Jit/inline_cache.h"
#include "Jit/jit_rt.h"
#include "Jit/pyjit.h"
#include "Jit/threaded_compile.h"
#include "Jit/type_profiler.h"
#include "Jit/util.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace jit {
class LoadMethodCachePool {
 public:
  explicit LoadMethodCachePool(std::size_t num_entries)
      : entries_(nullptr), num_entries_(num_entries), num_allocated_(0) {
    if (num_entries == 0) {
      return;
    }
    entries_.reset(new JITRT_LoadMethodCache[num_entries]);
    for (std::size_t i = 0; i < num_entries_; i++) {
      JITRT_InitLoadMethodCache(&(entries_[i]));
    }
  }

  ~LoadMethodCachePool() {}

  JITRT_LoadMethodCache* AllocateEntry() {
    JIT_CHECK(
        num_allocated_ < num_entries_,
        "not enough space alloc=%lu capacity=%lu",
        num_allocated_,
        num_entries_);
    JITRT_LoadMethodCache* entry = &entries_[num_allocated_];
    num_allocated_++;
    return entry;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(LoadMethodCachePool);

  std::unique_ptr<JITRT_LoadMethodCache[]> entries_;
  std::size_t num_entries_;
  std::size_t num_allocated_;
};

class GenYieldPoint;

// In a regular JIT function spill-data is stored at negative offsets from RBP
// and RBP points into the system stack. In JIT generators spilled data is still
// stored backwards from RBP, but RBP points to a heap allocated block and this
// persists when the generator is suspended.
//
// While the content of spill data is arbitrary depending on the function, we
// also have a few items of data about the current generator we want to access
// quickly. We can do this via positive offsets from RBP into the
// GenSuspendDataFooter struct defined below.
//
// Together the spill data and GenDataFooter make up the complete JIT-specific
// data needed for a generator. PyGenObject::gi_jit_data points to the _top_ of
// the spill data (i.e. at the start of the footer). This allows us to easily
// set RBP to the pointer value on generator resume.
//
// The base address of the complete heap allocated suspend data is:
//   PyGenObject::gi_jit_data - GenDataFooter::spill_words
typedef struct _GenDataFooter {
  // Tools which examine/walk the stack expect the following two values to be
  // ahead of RBP.
  uint64_t linkAddress;
  uint64_t returnAddress;

  // RBP that was swapped out to point to this spill-data.
  uint64_t originalRbp;

  // Current overall state of the JIT.
  _PyJitGenState state;

  // Allocated space before this struct in 64-bit words.
  size_t spillWords;

  // Entry-point to resume a JIT generator.
  GenResumeFunc resumeEntry;

  // Static data specific to the current yield point. Only non-null when we are
  // suspended.
  GenYieldPoint* yieldPoint;

  // Associated generator object
  PyGenObject* gen;

  // JIT metadata for associated code object
  CodeRuntime* code_rt;
} GenDataFooter;

// The state field needs to be at a fixed offset so it can be quickly accessed
// from C code.
static_assert(
    offsetof(GenDataFooter, state) == _PY_GEN_JIT_DATA_STATE_OFFSET,
    "Byte offset for state shifted");

// The number of words for pre-allocated blocks in the generator suspend data
// free-list. I chose this based on it covering 99% of the JIT generator
// spill-sizes needed when running 'make testcinder_jit' at the time I collected
// this data. For reference:
//   99.9% coverage came at 256 spill size
//   99.99% was at 1552
//   max was 4999
// There were about ~15k JIT generators in total during the run.
const size_t kMinGenSpillWords = 89;

class GenYieldPoint {
 public:
  explicit GenYieldPoint(
      const std::vector<ptrdiff_t>&& pyobj_offs,
      bool is_yield_from,
      ptrdiff_t yield_from_offs)
      : pyobj_offs_(std::move(pyobj_offs)),
        isYieldFrom_(is_yield_from),
        yieldFromOffs_(yield_from_offs) {}

  void setResumeTarget(uint64_t resume_target) {
    resume_target_ = resume_target;
  }

  uint64_t resumeTarget() const {
    return resume_target_;
  }

  int visitRefs(PyGenObject* gen, visitproc visit, void* arg) const;
  void releaseRefs(PyGenObject* gen) const;
  PyObject* yieldFromValue(GenDataFooter* gen_footer) const;

  static constexpr int resumeTargetOffset() {
    return offsetof(GenYieldPoint, resume_target_);
  }

 private:
  uint64_t resume_target_;
  const std::vector<ptrdiff_t> pyobj_offs_;
  const bool isYieldFrom_;
  const ptrdiff_t yieldFromOffs_;
};

class RuntimeFrameState {
 public:
  RuntimeFrameState(BorrowedRef<PyCodeObject> code, BorrowedRef<> globals)
      : code_(code), globals_(globals) {}

  bool isGen() const {
    return code()->co_flags & kCoFlagsAnyGenerator;
  }

  BorrowedRef<PyCodeObject> code() const {
    return code_;
  }

  BorrowedRef<> globals() const {
    return globals_;
  }

  static constexpr int64_t codeOffset() {
    return offsetof(RuntimeFrameState, code_);
  }

 private:
  // These are owned by the CodeRuntime that owns this RuntimeFrameState.
  BorrowedRef<PyCodeObject> code_;
  BorrowedRef<> globals_;
};

// Runtime data for a PyCodeObject object, containing caches and any other data
// associated with a JIT-compiled function.
class CodeRuntime {
 public:
  explicit CodeRuntime(
      PyCodeObject* code,
      PyObject* globals,
      jit::hir::FrameMode frame_mode,
      std::size_t num_lm_caches,
      std::size_t num_la_caches,
      std::size_t num_sa_caches,
      std::size_t num_lat_caches)
      : frame_state_(code, globals),
        frame_mode_(frame_mode),
        load_method_cache_pool_(num_lm_caches),
        load_attr_cache_pool_(num_la_caches),
        store_attr_cache_pool_(num_sa_caches),
        load_type_attr_caches_(
            std::make_unique<LoadTypeAttrCache[]>(num_lat_caches)) {
    // TODO(T88040922): Until we work out something smarter, force code and
    // globals objects for compiled functions to live as long as the JIT is
    // initialized.
    addReference(reinterpret_cast<PyObject*>(code));
    addReference(globals);
  }

  template <typename... Args>
  RuntimeFrameState* allocateRuntimeFrameState(Args&&... args) {
    // Serialize as we modify the globally shared runtimes data.
    ThreadedCompileSerialize guard;
    inlined_frame_states_.emplace_back(
        std::make_unique<RuntimeFrameState>(std::forward<Args>(args)...));
    return inlined_frame_states_.back().get();
  }

  ~CodeRuntime() {}

  jit::hir::FrameMode frameMode() const {
    return frame_mode_;
  }

  const RuntimeFrameState* frameState() const {
    return &frame_state_;
  }

  // Release any references this CodeRuntime holds to Python objects.
  void releaseReferences();

  JITRT_LoadMethodCache* AllocateLoadMethodCache() {
    return load_method_cache_pool_.AllocateEntry();
  }

  LoadAttrCache* AllocateLoadAttrCache() {
    return load_attr_cache_pool_.allocate();
  }

  StoreAttrCache* allocateStoreAttrCache() {
    return store_attr_cache_pool_.allocate();
  }

  LoadTypeAttrCache* getLoadTypeAttrCache(int id) {
    return &load_type_attr_caches_[id];
  }

  // Ensure that this CodeRuntime owns a reference to the given object, keeping
  // it alive for use by the compiled code.
  void addReference(PyObject* obj);

  // Store meta-data about generator yield point.
  GenYieldPoint* addGenYieldPoint(GenYieldPoint&& gen_yield_point) {
    gen_yield_points_.emplace_back(std::move(gen_yield_point));
    return &gen_yield_points_.back();
  }

  void set_frame_size(int size) {
    frame_size_ = size;
  }
  int frame_size() const {
    return frame_size_;
  }

  // Add or lookup a mapping from a point in generated code to corresponding
  // bytecode offset.
  void addIPtoBCOff(uintptr_t ip, int bc_off);

  // Returns the bytecode offset for the given address in generated code.
  std::optional<int> getBCOffForIP(uintptr_t ip) const;

  static constexpr int64_t frameStateOffset() {
    return offsetof(CodeRuntime, frame_state_);
  }

  static const int64_t kPyCodeOffset;

 private:
  RuntimeFrameState frame_state_;
  std::vector<std::unique_ptr<RuntimeFrameState>> inlined_frame_states_;
  jit::hir::FrameMode frame_mode_;
  LoadMethodCachePool load_method_cache_pool_;
  InlineCachePool<LoadAttrCache> load_attr_cache_pool_;
  InlineCachePool<StoreAttrCache> store_attr_cache_pool_;
  std::unique_ptr<LoadTypeAttrCache[]> load_type_attr_caches_;

  std::unordered_set<Ref<PyObject>> references_;

  // Metadata about yield points. Deque so we can have raw pointers to content.
  std::deque<GenYieldPoint> gen_yield_points_;

  int frame_size_{-1};

  // Map of address in compiled code to bytecode offset
  std::unordered_map<uintptr_t, int> ip_to_bc_off_;
};

// Information about the runtime behavior of a single deopt point: how often
// it's been hit, and the frequency of guilty types, if applicable.
struct DeoptStat {
  std::size_t count;
  FixedTypeProfiler<4> types;
};

// Map from DeoptMetadata index to stats about that deopt point.
using DeoptStats = std::unordered_map<std::size_t, DeoptStat>;

using BytecodeOffset = int;

// Profiling information for a PyCodeObject. Includes the total number of
// bytecodes executed and type profiles for certain opcodes, keyed by bytecode
// offset.
struct CodeProfile {
  UnorderedMap<BytecodeOffset, std::unique_ptr<TypeProfiler>> typed_hits;
  int64_t total_hits;
};

using TypeProfiles = std::unordered_map<Ref<PyCodeObject>, CodeProfile>;

// this class collects all the data needed for JIT at runtime
// it maps a PyCodeObject to the runtime info the PyCodeObject needs.
class Runtime {
 public:
  template <typename... Args>
  CodeRuntime* allocateCodeRuntime(Args&&... args) {
    // Serialize as we modify the globally shared runtimes data.
    ThreadedCompileSerialize guard;
    runtimes_.emplace_back(
        std::make_unique<CodeRuntime>(std::forward<Args>(args)...));
    return runtimes_.back().get();
  }

  // Create or look up a cache for the global with the given name, in the
  // context of the given globals dict.  This cache will fall back to
  // builtins if the value isn't defined in this dict.
  GlobalCache findGlobalCache(PyObject* globals, PyObject* name);

  // Create or look up a cache for a member with the given name, in the
  // context of the given dict.  This cache will not fall back to builtins
  // if the value isn't defined in the dict.
  GlobalCache findDictCache(PyObject* globals, PyObject* name);

  // Find a cache for the indirect static entry point for a function.
  void** findFunctionEntryCache(PyFunctionObject* function);

  // Gets information about the primitive arguments that a function
  // is typed to.  Typed object references are explicitly excluded.
  _PyTypedArgsInfo* findFunctionPrimitiveArgInfo(PyFunctionObject* function);

  // Forget given cache. Note that for now, this only removes bookkeeping for
  // the cache; the cache itself is not freed and may still be reachable from
  // compiled code.
  void forgetLoadGlobalCache(GlobalCache cache);

  // Add metadata used during deopt. Returns a handle that can be used to
  // fetch the metadata from generated code.
  std::size_t addDeoptMetadata(DeoptMetadata&& deopt_meta);
  DeoptMetadata& getDeoptMetadata(std::size_t id);

  // Record that a deopt of the given index happened at runtime, with an
  // optional guilty value.
  void recordDeopt(std::size_t idx, PyObject* guilty_value);

  // Get and/or clear runtime deopt stats.
  const DeoptStats& deoptStats() const;
  void clearDeoptStats();

  TypeProfiles& typeProfiles();

  using GuardFailureCallback = std::function<void(const DeoptMetadata&)>;

  // Add a function to be called when deoptimization occurs due to guard
  // failure. Intended to be used for testing/debugging only.
  void setGuardFailureCallback(GuardFailureCallback cb);
  void guardFailed(const DeoptMetadata& deopt_meta);
  void clearGuardFailureCallback();

  // Ensure that this Runtime owns a reference to the given object, keeping
  // it alive for use by compiled code.
  void addReference(PyObject* obj);

  // Release any references this Runtime holds to Python objects.
  void releaseReferences();

  template <typename T, typename... Args>
  T* allocateDeoptPatcher(Args&&... args) {
    deopt_patchers_.emplace_back(
        std::make_unique<T>(std::forward<Args>(args)...));
    return static_cast<T*>(deopt_patchers_.back().get());
  }

 private:
  std::vector<std::unique_ptr<CodeRuntime>> runtimes_;
  GlobalCacheMap global_caches_;
  FunctionEntryCacheMap function_entry_caches_;

  // Global caches removed by forgetGlobalCaches() may still be reachable from
  // compiled code, and are kept alive here until runtime shutdown.
  std::vector<GlobalCacheValue> orphaned_global_caches_;

  std::vector<DeoptMetadata> deopt_metadata_;
  DeoptStats deopt_stats_;
  GuardFailureCallback guard_failure_callback_;

  TypeProfiles type_profiles_;

  // References to Python objects held by this Runtime
  std::unordered_set<Ref<PyObject>> references_;
  std::vector<std::unique_ptr<DeoptPatcher>> deopt_patchers_;
};
} // namespace jit
