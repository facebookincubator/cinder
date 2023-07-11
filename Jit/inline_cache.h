// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "classloader.h"

#include "Jit/jit_rt.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"
#include "Jit/util.h"

#include <array>
#include <memory>
#include <unordered_map>

namespace jit {

// Mutator for an instance attribute that is stored in a split dictionary
struct SplitMutator {
  PyObject* setAttr(PyObject* obj, PyObject* name, PyObject* value);
  PyObject* getAttr(PyObject* obj, PyObject* name);
  uint32_t dict_offset;
  uint32_t val_offset;
  PyDictKeysObject* keys; // Borrowed
};

// Mutator for an instance attribute that is stored in a combined dictionary
struct CombinedMutator {
  PyObject* setAttr(PyObject* obj, PyObject* name, PyObject* value);
  PyObject* getAttr(PyObject* obj, PyObject* name);
  Py_ssize_t dict_offset;
};

// Mutator for a data descriptor
struct DataDescrMutator {
  PyObject* setAttr(PyObject* obj, PyObject* value);
  PyObject* getAttr(PyObject* obj);
  BorrowedRef<> descr;
};

// Mutator for a member descriptor
struct MemberDescrMutator {
  PyObject* setAttr(PyObject* obj, PyObject* value);
  PyObject* getAttr(PyObject* obj);
  PyMemberDef* memberdef;
};

// Attribute corresponds to a non-data descriptor or a class variable
struct DescrOrClassVarMutator {
  PyObject* setAttr(PyObject* obj, PyObject* name, PyObject* value);
  PyObject* getAttr(PyObject* obj, PyObject* name);
  BorrowedRef<> descr;
  Py_ssize_t dictoffset;
};

// An instance of AttributeMutator is specialized to more efficiently perform a
// get/set of a particular kind of attribute.
class AttributeMutator {
 public:
  // Kind enum is designed to fit within 3 bits and it's value is embedded into
  // the type_ pointer
  enum class Kind : uint8_t {
    kEmpty,
    kSplit,
    kCombined,
    kDataDescr,
    kMemberDescr,
    kDescrOrClassVar,
    kMaxValue,
  };
  static_assert(
      static_cast<uint8_t>(Kind::kMaxValue) <= 8,
      "Kind enum should fit in 3 bits");

  AttributeMutator();
  PyTypeObject* type() const;
  void reset();
  bool isEmpty() const;
  void set_combined(PyTypeObject* type);
  void set_data_descr(PyTypeObject* type, PyObject* descr);
  void set_member_descr(PyTypeObject* type, PyObject* descr);
  void set_descr_or_classvar(PyTypeObject* type, PyObject* descr);
  void
  set_split(PyTypeObject* type, Py_ssize_t val_offset, PyDictKeysObject* keys);

  PyObject* setAttr(PyObject* obj, PyObject* name, PyObject* value);
  PyObject* getAttr(PyObject* obj, PyObject* name);

 private:
  void set_type(PyTypeObject* type, Kind kind);
  Kind get_kind() const;

  uintptr_t type_; // This value stores both a PyTypeObject* for the type object
                   // and the Kind enum value which are bitpacked together to
                   // reduce memory consumption
  union {
    SplitMutator split_;
    CombinedMutator combined_;
    DataDescrMutator data_descr_;
    MemberDescrMutator member_descr_;
    DescrOrClassVarMutator descr_or_cvar_;
  };
};

class AttributeCache {
 public:
  virtual ~AttributeCache();

  void typeChanged(PyTypeObject* type);

 protected:
  std::span<AttributeMutator> entries();

  AttributeMutator* findEmptyEntry();

  void
  fill(BorrowedRef<PyTypeObject> type, BorrowedRef<> name, BorrowedRef<> descr);

  AttributeMutator entries_[0];
};

struct AttributeCacheSizeTrait {
  static size_t size() {
    auto base = sizeof(AttributeCache);
    auto extra = sizeof(AttributeMutator) * _PyJIT_AttrCacheSize();
    return base + extra;
  }
};

// A cache for an individual StoreAttr instruction.
//
// The logic of StoreAttrCache::invoke is equivalent to PyObject_SetAttr,
// however, it can be specialized and accelerated depending on the kinds of
// receiver types that are seen.
class StoreAttrCache : public AttributeCache {
 public:
  StoreAttrCache() = default;

  // Returns a borrowed reference to Py_None on success; nullptr otherwise.
  static PyObject*
  invoke(StoreAttrCache* cache, PyObject* obj, PyObject* name, PyObject* value);

 private:
  DISALLOW_COPY_AND_ASSIGN(StoreAttrCache);

  PyObject* doInvoke(PyObject* obj, PyObject* name, PyObject* value);
  PyObject* invokeSlowPath(PyObject* obj, PyObject* name, PyObject* value);
};

// A cache for an individual LoadAttr instruction.
//
// The logic of LoadAttrCache::invoke is equivalent to PyObject_GetAttr,
// however, it can be specialized and accelerated depending on the kinds of
// receiver types that are seen.
class LoadAttrCache : public AttributeCache {
 public:
  LoadAttrCache() = default;

  // Returns a new reference to the value or NULL on error.
  static PyObject* invoke(LoadAttrCache* cache, PyObject* obj, PyObject* name);

 private:
  DISALLOW_COPY_AND_ASSIGN(LoadAttrCache);

  PyObject* doInvoke(PyObject* obj, PyObject* name);
  PyObject* invokeSlowPath(PyObject* obj, PyObject* name);
};

// A cache for LoadAttr instructions where we expect the receiver to be a type
// object.
//
// The first entry in `items` is a type object. The second entry in `items` is
// the cached value.  Both are borrowed references.
//
// The code for loading an attribute where the expected receiver is a type is
// specialized into a fast path and a slow path. The first element is loaded
// from the cache and compared against the receiver. If they are equal, the
// second element (the cached value) is loaded. If they are not equal,
// `invoke()` is called, which performs the full lookup and potentially fills
// the cache.
class LoadTypeAttrCache {
 public:
  LoadTypeAttrCache();
  ~LoadTypeAttrCache();

  static PyObject*
  invoke(LoadTypeAttrCache* cache, PyObject* obj, PyObject* name);
  PyObject* doInvoke(PyObject* obj, PyObject* name);
  void typeChanged(PyTypeObject* type);

  std::array<PyObject*, 2> items; // Borrowed

 private:
  void fill(PyTypeObject* type, PyObject* value);
  void reset();
};

#define FOREACH_CACHE_MISS_REASON(V) \
  V(WrongTpGetAttro)                 \
  V(PyDescrIsData)                   \
  V(Uncategorized)

enum class CacheMissReason {
#define DECLARE_CACHE_MISS_REASON(name) k##name,
  FOREACH_CACHE_MISS_REASON(DECLARE_CACHE_MISS_REASON)
#undef DECLARE_CACHE_MISS_REASON
};

std::string_view cacheMissReason(CacheMissReason reason);

struct CacheMiss {
  int count{0};
  CacheMissReason reason{CacheMissReason::kUncategorized};
};

struct CacheStats {
  std::string filename;
  std::string method_name;
  std::unordered_map<std::string, CacheMiss> misses;
};

class LoadMethodCache {
 public:
  struct Entry {
    BorrowedRef<PyTypeObject> type{nullptr};
    BorrowedRef<> value{nullptr};
  };

  ~LoadMethodCache();

  static JITRT_LoadMethodResult
  lookupHelper(LoadMethodCache* cache, BorrowedRef<> obj, BorrowedRef<> name);
  JITRT_LoadMethodResult lookup(BorrowedRef<> obj, BorrowedRef<> name);
  void typeChanged(PyTypeObject* type);

  void initCacheStats(const char* filename, const char* method_name);
  void clearCacheStats();
  const CacheStats* cacheStats();

 private:
  JITRT_LoadMethodResult lookupSlowPath(BorrowedRef<> obj, BorrowedRef<> name);
  void fill(BorrowedRef<PyTypeObject> type, BorrowedRef<> value);

  std::array<Entry, 4> entries_;
  std::unique_ptr<CacheStats> cache_stats_;
};

// A cache for LoadMethod instructions where we expect the receiver to be a type
// object.
//
// The first entry in `entry` is the type receiver. The second entry in `entry`
// is the cached value.
//
// The code for loading a method where the expected receiver is a type is
// specialized into a fast path and a slow path. The first element is loaded
// from the cache and compared against the receiver. If they are equal, the
// `getValueHelper()` is called which returns the cached value. If they are not
// equal, `lookupHelper()` is called, which performs the full lookup and
// potentially fills the cache.
class LoadTypeMethodCache {
 public:
  BorrowedRef<PyTypeObject> type;
  BorrowedRef<> value;
  bool is_unbound_meth;

  ~LoadTypeMethodCache();
  static JITRT_LoadMethodResult lookupHelper(
      LoadTypeMethodCache* cache,
      BorrowedRef<PyTypeObject> obj,
      BorrowedRef<> name);
  static JITRT_LoadMethodResult getValueHelper(
      LoadTypeMethodCache* cache,
      BorrowedRef<> obj);

  JITRT_LoadMethodResult lookup(
      BorrowedRef<PyTypeObject> obj,
      BorrowedRef<> name);
  void typeChanged(BorrowedRef<PyTypeObject> type);

  void initCacheStats(const char* filename, const char* method_name);
  void clearCacheStats();
  const CacheStats* cacheStats();

 private:
  void
  fill(BorrowedRef<PyTypeObject> type, BorrowedRef<> value, bool is_bound_meth);
  void reset();
  std::unique_ptr<CacheStats> cache_stats_;
};

class LoadModuleMethodCache {
 public:
  static JITRT_LoadMethodResult lookupHelper(
      LoadModuleMethodCache* cache,
      BorrowedRef<> obj,
      BorrowedRef<> name);
  JITRT_LoadMethodResult lookup(BorrowedRef<> obj, BorrowedRef<> name);
  BorrowedRef<> moduleObj();
  BorrowedRef<> value();

 private:
  JITRT_LoadMethodResult lookupSlowPath(BorrowedRef<> obj, BorrowedRef<> name);
  void fill(BorrowedRef<> obj, BorrowedRef<> value, uint64_t version);

  // This corresponds to module __dict__'s version which allows us
  // to correctly invalidate the cache whenever the dictionary changes.
  uint64_t module_version_{0};
  BorrowedRef<> module_obj_;
  BorrowedRef<> value_;
};

// Invalidate all load/store attr caches for type
void notifyICsTypeChanged(BorrowedRef<PyTypeObject> type);

} // namespace jit

struct FunctionEntryCacheValue {
  void** ptr{nullptr};
  Ref<_PyTypedArgsInfo> arg_info;
};

using FunctionEntryCacheMap =
    std::unordered_map<PyFunctionObject*, FunctionEntryCacheValue>;
