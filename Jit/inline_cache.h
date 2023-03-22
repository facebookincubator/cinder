// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "classloader.h"

#include "Jit/jit_rt.h"
#include "Jit/log.h"
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
  enum class Kind {
    kEmpty,
    kSplit,
    kCombined,
    kDataDescr,
    kMemberDescr,
    kDescrOrClassVar,
  };

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
  Kind kind_;
  PyTypeObject* type_; // borrowed
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
  AttributeMutator* findEmptyEntry();

  void
  fill(BorrowedRef<PyTypeObject> type, BorrowedRef<> name, BorrowedRef<> descr);

  std::array<AttributeMutator, 1> entries_;
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

 private:
  JITRT_LoadMethodResult lookupSlowPath(BorrowedRef<> obj, BorrowedRef<> name);
  void fill(BorrowedRef<PyTypeObject> type, BorrowedRef<> value);

  std::array<Entry, 4> entries_;
};

struct GlobalCacheKey {
  // builtins and globals are weak references; the invalidation code is
  // responsible for erasing any relevant keys when a dict is freed.
  PyObject* builtins;
  PyObject* globals;
  Ref<PyObject> name;

  GlobalCacheKey(PyObject* builtins, PyObject* globals, PyObject* name)
      : builtins(builtins), globals(globals) {
    ThreadedCompileSerialize guard;
    this->name = Ref<>::create(name);
  }

  bool operator==(const GlobalCacheKey& other) const {
    return builtins == other.builtins && globals == other.globals &&
        name == other.name;
  }
};

struct GlobalCacheKeyHash {
  std::size_t operator()(const GlobalCacheKey& key) const {
    std::hash<PyObject*> hasher;
    std::size_t hash = combineHash(hasher(key.builtins), hasher(key.globals));
    return combineHash(hash, hasher(key.name));
  }
};

struct GlobalCacheValue {
  PyObject** ptr;
};

using GlobalCacheMap =
    std::unordered_map<GlobalCacheKey, GlobalCacheValue, GlobalCacheKeyHash>;

// Functions to initialize, update, and disable a global cache. The actual
// cache lives in a GlobalCacheMap, so this is a thin wrapper around a pointer
// to that data.
class GlobalCache {
 public:
  GlobalCache(GlobalCacheMap::value_type* pair) : pair_(pair) {}

  const GlobalCacheKey& key() const {
    return pair_->first;
  }

  PyObject** valuePtr() const {
    return pair_->second.ptr;
  }

  // Initialize the cache: subscribe to both dicts and fill in the current
  // value.
  void init(PyObject** cache) const;

  // Update the cached value after an update to one of the dicts.
  //
  // to_disable collects caches that must be disabled because their builtins
  // dict is unwatchable and the value has been deleted from the globals
  // dict. The caller is responsible for safely disabling any caches in this
  // list.
  void update(
      PyObject* dict,
      PyObject* new_value,
      std::vector<GlobalCache>& to_disable) const;

  // Disable the cache by clearing out its value. Unsubscribing from any
  // watched dicts is left to the caller since it can involve complicated
  // dances with iterators.
  void disable() const;

  bool operator<(const GlobalCache& other) const {
    return pair_ < other.pair_;
  }

 private:
  GlobalCacheMap::value_type* pair_;
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
