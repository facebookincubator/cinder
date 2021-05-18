// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __JIT_RUNTIME_H__
#define __JIT_RUNTIME_H__

#include "Jit/deopt.h"
#include "Jit/inline_cache.h"
#include "Jit/jit_rt.h"
#include "Jit/pyjit.h"
#include "Jit/threaded_compile.h"
#include "Jit/util.h"

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
  uint32_t code_rt_id;
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
  uint64_t resume_target_;

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

  int visitRefs(PyGenObject* gen, visitproc visit, void* arg) const;
  void releaseRefs(PyGenObject* gen) const;
  PyObject* yieldFromValue(GenDataFooter* gen_footer) const;

 private:
  const std::vector<ptrdiff_t> pyobj_offs_;
  const bool isYieldFrom_;
  const ptrdiff_t yieldFromOffs_;
};

// Runtime data for a PyCodeObject object, containing caches and any other data
// associated with a JIT-compiled function.
class CodeRuntime {
 public:
  explicit CodeRuntime(
      uint32_t id,
      PyCodeObject* py_code,
      PyObject* globals,
      jit::hir::FrameMode frame_mode,
      std::size_t num_lm_caches,
      std::size_t num_la_caches,
      std::size_t num_sa_caches,
      std::size_t num_lat_caches)
      : id_(id),
        py_code_(py_code),
        frame_mode_(frame_mode),
        load_method_cache_pool_(num_lm_caches),
        load_attr_cache_pool_(num_la_caches),
        store_attr_cache_pool_(num_sa_caches),
        load_type_attr_caches_(
            std::make_unique<LoadTypeAttrCache[]>(num_lat_caches)),
        globals_(globals),
        builtins_(PyEval_GetBuiltins()) {
    // TODO(T88040922): Until we work out something smarter, force code and
    // globals objects for compiled functions to live as long as the JIT is
    // initialized.
    addReference(py_code_);
    addReference(globals_);
  }

  ~CodeRuntime() {}

  jit::hir::FrameMode frameMode() const {
    return frame_mode_;
  }

  bool isGen() const {
    return GetCode()->co_flags & kCoFlagsAnyGenerator;
  }

  uint32_t id() const {
    return id_;
  }

  // Release any references this CodeRuntime holds to Python objects.
  void releaseReferences();

  PyCodeObject* GetCode() const {
    return py_code_;
  }
  PyObject* GetGlobals() {
    return globals_;
  }
  PyObject* GetBuiltins() {
    return builtins_;
  }

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

 private:
  uint32_t id_;
  BorrowedRef<PyCodeObject> py_code_;
  jit::hir::FrameMode frame_mode_;
  LoadMethodCachePool load_method_cache_pool_;
  InlineCachePool<LoadAttrCache> load_attr_cache_pool_;
  InlineCachePool<StoreAttrCache> store_attr_cache_pool_;
  std::unique_ptr<LoadTypeAttrCache[]> load_type_attr_caches_;
  BorrowedRef<PyDictObject> globals_;
  BorrowedRef<PyDictObject> builtins_;

  std::unordered_set<Ref<PyObject>> references_;

  // Metadata about yield points. Deque so we can have raw pointers to content.
  std::deque<GenYieldPoint> gen_yield_points_;
};

// this class collects all the data needed for JIT at runtime
// it maps a PyCodeObject to the runtime info the PyCodeObject needs.
class Runtime {
 public:
  template <typename... Args>
  CodeRuntime* allocateCodeRuntime(Args&&... args) {
    // Serialize as we modify the globally shared runtimes data.
    ThreadedCompileSerialize guard;
    uint32_t id = runtimes_.size();
    runtimes_.emplace_back(
        std::make_unique<CodeRuntime>(id, std::forward<Args>(args)...));
    return runtimes_.back().get();
  }

  CodeRuntime* getCodeRuntime(uint32_t id) const {
    JIT_CHECK(id < runtimes_.size(), "invalid id %u!", id);
    return runtimes_[id].get();
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

 private:
  std::vector<std::unique_ptr<CodeRuntime>> runtimes_;
  GlobalCacheMap global_caches_;
  FunctionEntryCacheMap function_entry_caches_;

  // Global caches removed by forgetGlobalCaches() may still be reachable from
  // compiled code, and are kept alive here until runtime shutdown.
  std::vector<GlobalCacheValue> orphaned_global_caches_;

  std::vector<DeoptMetadata> deopt_metadata_;
  GuardFailureCallback guard_failure_callback_;

  // References to Python objects held by this Runtime
  std::unordered_set<Ref<PyObject>> references_;
};
} // namespace jit
#endif
