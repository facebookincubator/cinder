// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/inline_cache.h"

#include "Objects/dict-common.h"
#include "Python.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/util.h"

#include <algorithm>
#include <memory>
#include <unordered_set>

// clang-format off
#include "cinder/exports.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_object.h"
#include "structmember.h"
// clang-format on

namespace jit {
namespace {

template <class T>
struct TypeWatcher {
  std::unordered_map<BorrowedRef<PyTypeObject>, std::unordered_set<T*>> caches;

  void watch(BorrowedRef<PyTypeObject> type, T* cache) {
    caches[type].emplace(cache);
  }

  void unwatch(BorrowedRef<PyTypeObject> type, T* cache) {
    auto it = caches.find(type);
    if (it == caches.end()) {
      return;
    }
    it->second.erase(cache);
  }

  void typeChanged(BorrowedRef<PyTypeObject> type) {
    auto it = caches.find(type);
    if (it == caches.end()) {
      return;
    }
    std::unordered_set<T*> to_notify = std::move(it->second);
    caches.erase(it);
    for (T* cache : to_notify) {
      cache->typeChanged(type);
    }
  }
};

TypeWatcher<AttributeCache> ac_watcher;
TypeWatcher<LoadTypeAttrCache> ltac_watcher;
TypeWatcher<LoadMethodCache> lm_watcher;
TypeWatcher<LoadTypeMethodCache> ltm_watcher;

} // namespace

constexpr uintptr_t kKindMask = 0x07;

AttributeMutator::AttributeMutator() {
  reset();
}

PyTypeObject* AttributeMutator::type() const {
  // clear tagged bits and return
  return reinterpret_cast<PyTypeObject*>(type_ & ~kKindMask);
}

void AttributeMutator::reset() {
  set_type(nullptr, Kind::kEmpty);
}

bool AttributeMutator::isEmpty() const {
  return get_kind() == Kind::kEmpty;
}

void AttributeMutator::set_combined(PyTypeObject* type) {
  set_type(type, Kind::kCombined);
  combined_.dict_offset = type->tp_dictoffset;
}

void AttributeMutator::set_split(
    PyTypeObject* type,
    Py_ssize_t val_offset,
    PyDictKeysObject* keys) {
  set_type(type, Kind::kSplit);
  JIT_CHECK(
      type->tp_dictoffset <= std::numeric_limits<uint32_t>::max(),
      "Dict offset does not fit into a 32-bit int");
  JIT_CHECK(
      val_offset <= std::numeric_limits<uint32_t>::max(),
      "Val offset does not fit into a 32-bit int");
  split_.dict_offset = static_cast<uint32_t>(type->tp_dictoffset);
  split_.val_offset = static_cast<uint32_t>(val_offset);
  split_.keys = keys;
}

void AttributeMutator::set_data_descr(PyTypeObject* type, PyObject* descr) {
  set_type(type, Kind::kDataDescr);
  data_descr_.descr = descr;
}

void AttributeMutator::set_member_descr(PyTypeObject* type, PyObject* descr) {
  set_type(type, Kind::kMemberDescr);
  member_descr_.memberdef = ((PyMemberDescrObject*)descr)->d_member;
}

void AttributeMutator::set_descr_or_classvar(
    PyTypeObject* type,
    PyObject* descr) {
  set_type(type, Kind::kDescrOrClassVar);
  descr_or_cvar_.descr = descr;
  descr_or_cvar_.dictoffset = type->tp_dictoffset;
}

void AttributeMutator::set_type(PyTypeObject* type, Kind kind) {
  auto raw = reinterpret_cast<uintptr_t>(type);
  JIT_CHECK((raw & kKindMask) == 0, "PyTypeObject* expected to be aligned");
  auto mask = static_cast<uintptr_t>(kind);
  type_ = raw | mask;
}

AttributeMutator::Kind AttributeMutator::get_kind() const {
  return static_cast<Kind>(type_ & kKindMask);
}

AttributeCache::AttributeCache() {
  for (auto& entry : entries()) {
    entry.reset();
  }
}

AttributeCache::~AttributeCache() {
  for (auto& entry : entries()) {
    if (entry.type() != nullptr) {
      ac_watcher.unwatch(entry.type(), this);
      entry.reset();
    }
  }
}

void AttributeCache::typeChanged(PyTypeObject* type) {
  for (auto& entry : entries()) {
    if (entry.type() == type) {
      entry.reset();
    }
  }
}

void AttributeCache::fill(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<> name,
    BorrowedRef<> descr) {
  if (!PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG)) {
    // The type must have a valid version tag in order for us to be able to
    // invalidate the cache when the type is modified. See the comment at
    // the top of `PyType_Modified` for more details.
    return;
  }

  AttributeMutator* mut = findEmptyEntry();
  if (mut == nullptr) {
    return;
  }

  if (descr != nullptr) {
    BorrowedRef<PyTypeObject> descr_type(Py_TYPE(descr));
    if (descr_type->tp_descr_get != nullptr &&
        descr_type->tp_descr_set != nullptr) {
      // Data descriptor
      if (descr_type == &PyMemberDescr_Type) {
        mut->set_member_descr(type, descr);
      } else {
        mut->set_data_descr(type, descr);
      }
    } else {
      // Non-data descriptor or class var
      mut->set_descr_or_classvar(type, descr);
    }
    ac_watcher.watch(type, this);
    return;
  }

  if (type->tp_dictoffset < 0 ||
      !PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
    // We only support the common case for objects - fixed-size instances
    // (tp_dictoffset >= 0) of heap types (Py_TPFLAGS_HEAPTYPE).
    return;
  }

  // Instance attribute with no shadowing. Specialize the lookup based on
  // whether or not the type is using split dictionaries.
  PyHeapTypeObject* ht = reinterpret_cast<PyHeapTypeObject*>(type.get());
  PyDictKeysObject* keys = ht->ht_cached_keys;
  Py_ssize_t val_offset;
  if (keys != nullptr &&
      (val_offset = _PyDictKeys_GetSplitIndex(keys, name)) != -1) {
    mut->set_split(type, val_offset, keys);
  } else {
    mut->set_combined(type);
  }
  ac_watcher.watch(type, this);
}

std::span<AttributeMutator> AttributeCache::entries() {
  return {entries_, _PyJIT_AttrCacheSize()};
}

AttributeMutator* AttributeCache::findEmptyEntry() {
  auto it = std::ranges::find_if(
      entries(), [](const AttributeMutator& e) { return e.isEmpty(); });
  return it == entries().end() ? nullptr : &*it;
}

inline PyObject*
AttributeMutator::setAttr(PyObject* obj, PyObject* name, PyObject* value) {
  AttributeMutator::Kind kind = get_kind();
  switch (kind) {
    case AttributeMutator::Kind::kSplit:
      return split_.setAttr(obj, name, value);
    case AttributeMutator::Kind::kCombined:
      return combined_.setAttr(obj, name, value);
    case AttributeMutator::Kind::kDataDescr:
      return data_descr_.setAttr(obj, value);
    case AttributeMutator::Kind::kMemberDescr:
      return member_descr_.setAttr(obj, value);
    case AttributeMutator::Kind::kDescrOrClassVar:
      return descr_or_cvar_.setAttr(obj, name, value);
    default:
      JIT_CHECK(
          false,
          "cannot invoke setAttr for attr of kind %d",
          static_cast<int>(kind));
  }
}

inline PyObject* AttributeMutator::getAttr(PyObject* obj, PyObject* name) {
  AttributeMutator::Kind kind = get_kind();
  switch (kind) {
    case AttributeMutator::Kind::kSplit:
      return split_.getAttr(obj, name);
    case AttributeMutator::Kind::kCombined:
      return combined_.getAttr(obj, name);
    case AttributeMutator::Kind::kDataDescr:
      return data_descr_.getAttr(obj);
    case AttributeMutator::Kind::kMemberDescr:
      return member_descr_.getAttr(obj);
    case AttributeMutator::Kind::kDescrOrClassVar:
      return descr_or_cvar_.getAttr(obj, name);
    default:
      JIT_ABORT(
          "Cannot invoke getAttr for attr of kind %d", static_cast<int>(kind));
  }
}

static inline PyDictObject* get_dict(PyObject* obj, Py_ssize_t dictoffset) {
  PyObject** dictptr = (PyObject**)((char*)obj + dictoffset);
  return (PyDictObject*)*dictptr;
}

static inline PyDictObject* get_or_allocate_dict(
    PyObject* obj,
    Py_ssize_t dict_offset) {
  PyDictObject* dict = get_dict(obj, dict_offset);
  if (dict == nullptr) {
    dict =
        reinterpret_cast<PyDictObject*>(PyObject_GenericGetDict(obj, nullptr));
    if (dict == nullptr) {
      return nullptr;
    }
    Py_DECREF(dict);
  }
  return dict;
}

static PyObject* __attribute__((noinline))
raise_attribute_error(PyObject* obj, PyObject* name) {
  PyErr_Format(
      PyExc_AttributeError,
      "'%.50s' object has no attribute '%U'",
      Py_TYPE(obj)->tp_name,
      name);
  Ci_set_attribute_error_context(obj, name);
  return nullptr;
}

PyObject*
SplitMutator::setAttr(PyObject* obj, PyObject* name, PyObject* value) {
  PyDictObject* dict = get_or_allocate_dict(obj, dict_offset);
  if (dict == nullptr) {
    return nullptr;
  }
  PyObject* dictobj = reinterpret_cast<PyObject*>(dict);
  PyObject* result = Py_None;
  if ((dict->ma_keys == keys) &&
      ((dict->ma_used == val_offset) ||
       (dict->ma_values[val_offset] != nullptr))) {
    PyObject* old_value = dict->ma_values[val_offset];

    if (!_PyObject_GC_IS_TRACKED(dictobj)) {
      if (_PyObject_GC_MAY_BE_TRACKED(value)) {
        _PyObject_GC_TRACK(dictobj);
      }
    }

    uint64_t new_version =
        _PyDict_NotifyEvent(PyDict_EVENT_MODIFIED, dict, name, value);

    Py_INCREF(value);
    dict->ma_values[val_offset] = value;
    dict->ma_version_tag = new_version;

    if (old_value == nullptr) {
      dict->ma_used++;
    } else {
      Py_DECREF(old_value);
    }
  } else {
    Py_INCREF(dictobj);
    if (PyDict_SetItem(dictobj, name, value) < 0) {
      result = nullptr;
    }
    Py_DECREF(dictobj);
  }
  return result;
}

PyObject* SplitMutator::getAttr(PyObject* obj, PyObject* name) {
  PyDictObject* dict = get_dict(obj, dict_offset);
  if (dict == nullptr) {
    return raise_attribute_error(obj, name);
  }
  PyObject* result = nullptr;
  if (dict->ma_keys == keys) {
    result = dict->ma_values[val_offset];
  } else {
    auto dictobj = reinterpret_cast<PyObject*>(dict);
    Py_INCREF(dictobj);
    result = PyDict_GetItem(dictobj, name);
    Py_DECREF(dictobj);
  }
  if (result == nullptr) {
    return raise_attribute_error(obj, name);
  }
  Py_INCREF(result);
  return result;
}

PyObject*
CombinedMutator::setAttr(PyObject* obj, PyObject* name, PyObject* value) {
  PyDictObject* dict = get_or_allocate_dict(obj, dict_offset);
  if (dict == nullptr) {
    return nullptr;
  }
  PyObject* result = Py_None;
  auto dictobj = reinterpret_cast<PyObject*>(dict);
  Py_INCREF(dictobj);
  if (PyDict_SetItem(dictobj, name, value) < 0) {
    result = nullptr;
  }
  Py_DECREF(dictobj);
  return result;
}

PyObject* CombinedMutator::getAttr(PyObject* obj, PyObject* name) {
  auto dict = reinterpret_cast<PyObject*>(get_dict(obj, dict_offset));
  if (dict == nullptr) {
    return raise_attribute_error(obj, name);
  }
  Py_INCREF(dict);
  PyObject* result = PyDict_GetItem(dict, name);
  Py_DECREF(dict);
  if (result == nullptr) {
    return raise_attribute_error(obj, name);
  }
  Py_INCREF(result);
  return result;
}

PyObject* DataDescrMutator::setAttr(PyObject* obj, PyObject* value) {
  if (Py_TYPE(descr)->tp_descr_set(descr, obj, value)) {
    return nullptr;
  }
  return Py_None;
}

PyObject* DataDescrMutator::getAttr(PyObject* obj) {
  return Py_TYPE(descr)->tp_descr_get(descr, obj, (PyObject*)Py_TYPE(obj));
}

PyObject* MemberDescrMutator::setAttr(PyObject* obj, PyObject* value) {
  if (PyMember_SetOne((char*)obj, memberdef, value)) {
    return nullptr;
  }
  return Py_None;
}

PyObject* MemberDescrMutator::getAttr(PyObject* obj) {
  return PyMember_GetOne((char*)obj, memberdef);
}

PyObject* DescrOrClassVarMutator::setAttr(
    PyObject* obj,
    PyObject* name,
    PyObject* value) {
  descrsetfunc setter = Py_TYPE(descr)->tp_descr_set;
  if (setter != nullptr) {
    auto descr_guard = Ref<>::create(descr);
    int st = setter(descr, obj, value);
    return (st == -1) ? nullptr : Py_None;
  }
  PyObject** dictptr = Ci_PyObject_GetDictPtrAtOffset(obj, dictoffset);
  if (dictptr == nullptr) {
    PyErr_Format(
        PyExc_AttributeError,
        "'%.50s' object attribute '%U' is read-only",
        Py_TYPE(obj)->tp_name,
        name);
    return nullptr;
  }
  BorrowedRef<PyTypeObject> type(Py_TYPE(obj));
  int st = _PyObjectDict_SetItem(type, dictptr, name, value);
  if (st < 0 && PyErr_ExceptionMatches(PyExc_KeyError)) {
    PyErr_SetObject(PyExc_AttributeError, name);
  }
  _PyType_ClearNoShadowingInstances(type, descr);
  return (st == -1) ? nullptr : Py_None;
}

PyObject* DescrOrClassVarMutator::getAttr(PyObject* obj, PyObject* name) {
  BorrowedRef<PyTypeObject> descr_type(Py_TYPE(descr));
  descrsetfunc setter = descr_type->tp_descr_set;
  descrgetfunc getter = descr_type->tp_descr_get;

  auto descr_guard = Ref<>::create(descr);
  if (setter != nullptr && getter != nullptr) {
    BorrowedRef<PyTypeObject> type(Py_TYPE(obj));
    return getter(descr, obj, type);
  }

  Ref<> dict;
  PyObject** dictptr = Ci_PyObject_GetDictPtrAtOffset(obj, dictoffset);
  if (dictptr != nullptr) {
    dict.reset(*dictptr);
  }

  // Check instance dict.
  if (dict != nullptr) {
    auto res = Ref<>::create(_PyDict_GetItem_UnicodeExact(dict, name));
    if (res != nullptr) {
      return res.release();
    }
  }

  if (getter != nullptr) {
    // Non-data descriptor
    BorrowedRef<PyTypeObject> type(Py_TYPE(obj));
    return getter(descr, obj, type);
  }

  // Class var
  return descr_guard.release();
}

// NB: The logic here needs to be kept in sync with
// _PyObject_GenericSetAttrWithDict, with the proviso that this will never be
// used to delete attributes.
PyObject* __attribute__((noinline))
StoreAttrCache::invokeSlowPath(PyObject* obj, PyObject* name, PyObject* value) {
  BorrowedRef<PyTypeObject> tp(Py_TYPE(obj));

  if (tp->tp_dict == nullptr && PyType_Ready(tp) < 0) {
    return nullptr;
  } else if (tp->tp_setattro != PyObject_GenericSetAttr) {
    int st = PyObject_SetAttr(obj, name, value);
    return st == 0 ? Py_None : nullptr;
  }

  auto name_guard = Ref<>::create(name);
  auto descr = Ref<>::create(_PyType_Lookup(tp, name));
  if (descr != nullptr) {
    descrsetfunc f = descr->ob_type->tp_descr_set;
    if (f != nullptr) {
      int res = f(descr, obj, value);
      fill(tp, name, descr);
      return (res == -1) ? nullptr : Py_None;
    }
  }

  PyObject** dictptr = _PyObject_GetDictPtr(obj);
  if (dictptr == nullptr) {
    if (descr == nullptr) {
      raise_attribute_error(obj, name);
    } else {
      PyErr_Format(
          PyExc_AttributeError,
          "'%.50s' object attribute '%U' is read-only",
          tp->tp_name,
          name);
    }
    return nullptr;
  }

  int res = _PyObjectDict_SetItem(tp, dictptr, name, value);
  if (descr != nullptr) {
    _PyType_ClearNoShadowingInstances(tp, descr);
  }
  if (res != -1) {
    fill(tp, name, descr);
  }

  return (res == -1) ? nullptr : Py_None;
}

PyObject* StoreAttrCache::invoke(
    StoreAttrCache* cache,
    PyObject* obj,
    PyObject* name,
    PyObject* value) {
  return cache->doInvoke(obj, name, value);
}

PyObject*
StoreAttrCache::doInvoke(PyObject* obj, PyObject* name, PyObject* value) {
  PyTypeObject* tp = Py_TYPE(obj);
  for (auto& entry : entries()) {
    if (entry.type() == tp) {
      return entry.setAttr(obj, name, value);
    }
  }
  return invokeSlowPath(obj, name, value);
}

// NB: The logic here needs to be kept in-sync with PyObject_GenericGetAttr
PyObject* __attribute__((noinline))
LoadAttrCache::invokeSlowPath(PyObject* obj, PyObject* name) {
  BorrowedRef<PyTypeObject> tp(Py_TYPE(obj));
  if (tp->tp_getattro != PyObject_GenericGetAttr) {
    return PyObject_GetAttr(obj, name);
  }
  if (tp->tp_dict == nullptr) {
    if (PyType_Ready(tp) < 0) {
      return nullptr;
    }
  }

  auto name_guard = Ref<>::create(name);
  auto descr = Ref<>::create(_PyType_Lookup(tp, name));
  descrgetfunc f = nullptr;
  if (descr != nullptr) {
    f = descr->ob_type->tp_descr_get;
    if (f != nullptr && PyDescr_IsData(descr)) {
      fill(tp, name, descr);
      return f(descr, obj, tp);
    }
  }

  Ref<> dict;
  PyObject** dictptr = _PyObject_GetDictPtr(obj);
  if (dictptr != nullptr) {
    dict.reset(*dictptr);
  }

  if (dict != nullptr) {
    auto res = Ref<>::create(PyDict_GetItem(dict, name));
    if (res != nullptr) {
      fill(tp, name, descr);
      return res.release();
    }
  }

  if (f != nullptr) {
    fill(tp, name, descr);
    return f(descr, obj, tp);
  }

  if (descr != nullptr) {
    fill(tp, name, descr);
    return descr.release();
  }

  raise_attribute_error(obj, name);

  return nullptr;
}

// Sentinel PyObject that must never escape into user code.
static PyObject g_emptyTypeAttrCache = {_PyObject_EXTRA_INIT 1, nullptr};

void LoadTypeAttrCache::fill(PyTypeObject* type, PyObject* value) {
  if (!PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG)) {
    // The type must have a valid version tag in order for us to be able to
    // invalidate the cache when the type is modified. See the comment at
    // the top of `PyType_Modified` for more details.
    return;
  }
  ltac_watcher.unwatch(items[0], this);
  items[0] = reinterpret_cast<PyObject*>(type);
  items[1] = value;
  ltac_watcher.watch(type, this);
}

void LoadTypeAttrCache::reset() {
  // We need to return a PyObject* even in the empty case so that subsequent
  // refcounting operations work correctly.
  items[0] = &g_emptyTypeAttrCache;
  items[1] = nullptr;
}

void LoadTypeAttrCache::typeChanged(PyTypeObject* /* type */) {
  reset();
}

LoadTypeAttrCache::LoadTypeAttrCache() {
  reset();
}

LoadTypeAttrCache::~LoadTypeAttrCache() {
  ltac_watcher.unwatch(items[0], this);
}

PyObject*
LoadAttrCache::invoke(LoadAttrCache* cache, PyObject* obj, PyObject* name) {
  return cache->doInvoke(obj, name);
}

PyObject* LoadAttrCache::doInvoke(PyObject* obj, PyObject* name) {
  PyTypeObject* tp = Py_TYPE(obj);
  for (auto& entry : entries()) {
    if (entry.type() == tp) {
      return entry.getAttr(obj, name);
    }
  }
  return invokeSlowPath(obj, name);
}

// This needs to be kept in sync with PyType_Type.tp_getattro.
PyObject* LoadTypeAttrCache::doInvoke(PyObject* obj, PyObject* name) {
  PyTypeObject* metatype = Py_TYPE(obj);
  if (metatype->tp_getattro != PyType_Type.tp_getattro) {
    return PyObject_GetAttr(obj, name);
  }

  PyTypeObject* type = reinterpret_cast<PyTypeObject*>(obj);
  if (type->tp_dict == nullptr) {
    if (PyType_Ready(type) < 0) {
      return nullptr;
    }
  }

  descrgetfunc meta_get = nullptr;
  PyObject* meta_attribute = _PyType_Lookup(metatype, name);
  if (meta_attribute != nullptr) {
    Py_INCREF(meta_attribute);
    meta_get = Py_TYPE(meta_attribute)->tp_descr_get;

    if (meta_get != nullptr && PyDescr_IsData(meta_attribute)) {
      /* Data descriptors implement tp_descr_set to intercept
       * writes. Assume the attribute is not overridden in
       * type's tp_dict (and bases): call the descriptor now.
       */
      PyObject* res = meta_get(
          meta_attribute,
          reinterpret_cast<PyObject*>(type),
          reinterpret_cast<PyObject*>(metatype));
      Py_DECREF(meta_attribute);
      return res;
    }
  }

  /* No data descriptor found on metatype. Look in tp_dict of this
   * type and its bases */
  PyObject* attribute = _PyType_Lookup(type, name);
  if (attribute != nullptr) {
    /* Implement descriptor functionality, if any */
    Py_INCREF(attribute);
    descrgetfunc local_get = Py_TYPE(attribute)->tp_descr_get;

    Py_XDECREF(meta_attribute);

    if (local_get != nullptr) {
      /* NULL 2nd argument indicates the descriptor was
       * found on the target object itself (or a base)  */
      PyObject* res =
          local_get(attribute, nullptr, reinterpret_cast<PyObject*>(type));
      Py_DECREF(attribute);
      return res;
    }

    fill(type, attribute);

    return attribute;
  }

  /* No attribute found in local __dict__ (or bases): use the
   * descriptor from the metatype, if any */
  if (meta_get != nullptr) {
    PyObject* res;
    res = meta_get(
        meta_attribute,
        reinterpret_cast<PyObject*>(type),
        reinterpret_cast<PyObject*>(metatype));
    Py_DECREF(meta_attribute);
    return res;
  }

  /* If an ordinary attribute was found on the metatype, return it now */
  if (meta_attribute != nullptr) {
    return meta_attribute;
  }

  /* Give up */
  PyErr_Format(
      PyExc_AttributeError,
      "type object '%.50s' has no attribute '%U'",
      type->tp_name,
      name);
  return NULL;
}

PyObject* LoadTypeAttrCache::invoke(
    LoadTypeAttrCache* cache,
    PyObject* obj,
    PyObject* name) {
  return cache->doInvoke(obj, name);
}

std::string_view kCacheMissReasons[] = {
#define NAME_REASON(reason) #reason,
    FOREACH_CACHE_MISS_REASON(NAME_REASON)
#undef NAME_REASON
};

std::string_view cacheMissReason(CacheMissReason reason) {
  return kCacheMissReasons[static_cast<size_t>(reason)];
}

JITRT_LoadMethodResult LoadModuleMethodCache::lookupHelper(
    LoadModuleMethodCache* cache,
    BorrowedRef<> obj,
    BorrowedRef<> name) {
  return cache->lookup(obj, name);
}

static uint64_t getModuleVersion(BorrowedRef<PyModuleObject> mod) {
  if (mod->md_dict) {
    BorrowedRef<PyDictObject> md_dict = mod->md_dict;
    return md_dict->ma_version_tag;
  }
  return 0;
}

static uint64_t getModuleVersion(BorrowedRef<PyStrictModuleObject> mod) {
  if (mod->globals) {
    BorrowedRef<PyDictObject> globals = mod->globals;
    return globals->ma_version_tag;
  }
  return 0;
}

BorrowedRef<> LoadModuleMethodCache::moduleObj() {
  return module_obj_;
}

BorrowedRef<> LoadModuleMethodCache::value() {
  return value_;
}

JITRT_LoadMethodResult LoadModuleMethodCache::lookup(
    BorrowedRef<> obj,
    BorrowedRef<> name) {
  if (module_obj_ == obj && value_ != nullptr) {
    uint64_t version = 0;
    if (PyModule_Check(obj)) {
      BorrowedRef<PyModuleObject> mod{obj};
      version = getModuleVersion(mod);
    } else if (PyStrictModule_Check(obj)) {
      BorrowedRef<PyStrictModuleObject> mod{obj};
      version = getModuleVersion(mod);
    }
    if (module_version_ == version) {
      Py_INCREF(Py_None);
      Py_INCREF(value_);
      return {Py_None, value_};
    }
  }
  return lookupSlowPath(obj, name);
}

JITRT_LoadMethodResult __attribute__((noinline))
LoadModuleMethodCache::lookupSlowPath(BorrowedRef<> obj, BorrowedRef<> name) {
  BorrowedRef<PyTypeObject> tp = Py_TYPE(obj);
  uint64_t dict_version = 0;
  BorrowedRef<> res = nullptr;
  if (PyModule_Check(obj) && tp->tp_getattro == PyModule_Type.tp_getattro) {
    if (_PyType_Lookup(tp, name) == nullptr) {
      BorrowedRef<PyModuleObject> mod{obj};
      BorrowedRef<> dict = mod->md_dict;
      if (dict) {
        dict_version = getModuleVersion(mod);
        res = PyDict_GetItemWithError(dict, name);
      }
    }
  } else if (
      PyStrictModule_Check(obj) &&
      tp->tp_getattro == PyStrictModule_Type.tp_getattro) {
    if (_PyType_Lookup(tp, name) == nullptr) {
      BorrowedRef<PyStrictModuleObject> mod{obj};
      BorrowedRef<> dict = mod->globals;
      if (dict && strictmodule_is_unassigned(dict, name) == 0) {
        dict_version = getModuleVersion(mod);
        res = PyDict_GetItemWithError(dict, name);
      }
    }
  }
  if (res != nullptr) {
    if (PyFunction_Check(res) || PyCFunction_Check(res) ||
        Py_TYPE(res) == &PyMethodDescr_Type) {
      fill(obj, res, dict_version);
    }
    Py_INCREF(Py_None);
    // PyDict_GetItemWithError returns a borrowed reference, so
    // we need to increment it before returning.
    Py_INCREF(res);
    return {Py_None, res};
  }
  auto generic_res = Ref<>::steal(PyObject_GetAttr(obj, name));
  if (generic_res != nullptr) {
    return {Py_None, generic_res.release()};
  }
  return {nullptr, nullptr};
}

void LoadModuleMethodCache::fill(
    BorrowedRef<> obj,
    BorrowedRef<> value,
    uint64_t version) {
  module_obj_ = obj;
  value_ = value;
  module_version_ = version;
}

void LoadMethodCache::initCacheStats(
    const char* filename,
    const char* method_name) {
  cache_stats_ = std::make_unique<CacheStats>();
  cache_stats_->filename = filename;
  cache_stats_->method_name = method_name;
}

void LoadMethodCache::clearCacheStats() {
  cache_stats_->misses.clear();
}

const CacheStats* LoadMethodCache::cacheStats() {
  return cache_stats_.get();
}

LoadMethodCache::~LoadMethodCache() {
  for (auto& entry : entries_) {
    if (entry.type != nullptr) {
      lm_watcher.unwatch(entry.type, this);
      entry.type.reset();
      entry.value.reset();
    }
  }
}

void LoadMethodCache::typeChanged(PyTypeObject* type) {
  for (auto& entry : entries_) {
    if (entry.type == type) {
      entry.type.reset();
      entry.value.reset();
    }
  }
}

void LoadMethodCache::fill(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<> value) {
  if (!PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG)) {
    // The type must have a valid version tag in order for us to be able to
    // invalidate the cache when the type is modified. See the comment at
    // the top of `PyType_Modified` for more details.
    return;
  }

  if (!PyType_HasFeature(type, Py_TPFLAGS_NO_SHADOWING_INSTANCES) &&
      (type->tp_dictoffset != 0)) {
    return;
  }

  for (auto& entry : entries_) {
    if (entry.type == nullptr) {
      lm_watcher.watch(type, this);
      entry.type = type;
      entry.value = value;
      return;
    }
  }
}

static void maybeCollectCacheStats(
    std::unique_ptr<CacheStats>& stat,
    BorrowedRef<PyTypeObject> tp,
    BorrowedRef<> name,
    CacheMissReason reason) {
  if (!g_collect_inline_cache_stats) {
    return;
  }
  std::string key =
      fmt::format("{}.{}", typeFullname(tp), PyUnicode_AsUTF8(name));
  stat->misses.insert({key, CacheMiss{0, reason}}).first->second.count++;
}

JITRT_LoadMethodResult __attribute__((noinline))
LoadMethodCache::lookupSlowPath(BorrowedRef<> obj, BorrowedRef<> name) {
  PyTypeObject* tp = Py_TYPE(obj);
  PyObject* descr;
  descrgetfunc f = nullptr;
  PyObject **dictptr, *dict;
  PyObject* attr;
  bool is_method = false;

  if ((tp->tp_getattro != PyObject_GenericGetAttr)) {
    PyObject* res = PyObject_GetAttr(obj, name);
    if (res != nullptr) {
      maybeCollectCacheStats(
          cache_stats_, tp, name, CacheMissReason::kWrongTpGetAttro);
      Py_INCREF(Py_None);
      return {Py_None, res};
    }
    return {nullptr, nullptr};
  } else if (tp->tp_dict == nullptr && PyType_Ready(tp) < 0) {
    return {nullptr, nullptr};
  }

  descr = _PyType_Lookup(tp, name);
  if (descr != nullptr) {
    Py_INCREF(descr);
    if (PyFunction_Check(descr) || Py_TYPE(descr) == &PyMethodDescr_Type ||
        PyType_HasFeature(Py_TYPE(descr), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
      is_method = true;
    } else {
      f = descr->ob_type->tp_descr_get;
      if (f != nullptr && PyDescr_IsData(descr)) {
        maybeCollectCacheStats(
            cache_stats_, tp, name, CacheMissReason::kPyDescrIsData);
        PyObject* result = f(descr, obj, (PyObject*)obj->ob_type);
        Py_DECREF(descr);
        Py_INCREF(Py_None);
        return {Py_None, result};
      }
    }
  }

  dictptr = _PyObject_GetDictPtr(obj);
  if (dictptr != nullptr && (dict = *dictptr) != nullptr) {
    Py_INCREF(dict);
    attr = PyDict_GetItem(dict, name);
    if (attr != nullptr) {
      maybeCollectCacheStats(
          cache_stats_, tp, name, CacheMissReason::kUncategorized);
      Py_INCREF(attr);
      Py_DECREF(dict);
      Py_XDECREF(descr);
      Py_INCREF(Py_None);
      return {Py_None, attr};
    }
    Py_DECREF(dict);
  }

  if (is_method) {
    fill(tp, descr);
    Py_INCREF(obj);
    return {descr, obj};
  }

  if (f != nullptr) {
    maybeCollectCacheStats(
        cache_stats_, tp, name, CacheMissReason::kUncategorized);
    PyObject* result = f(descr, obj, (PyObject*)Py_TYPE(obj));
    Py_DECREF(descr);
    Py_INCREF(Py_None);
    return {Py_None, result};
  }

  if (descr != nullptr) {
    maybeCollectCacheStats(
        cache_stats_, tp, name, CacheMissReason::kUncategorized);
    Py_INCREF(Py_None);
    return {Py_None, descr};
  }

  PyErr_Format(
      PyExc_AttributeError,
      "'%.50s' object has no attribute '%U'",
      tp->tp_name,
      name);
  return {nullptr, nullptr};
}

JITRT_LoadMethodResult LoadMethodCache::lookup(
    BorrowedRef<> obj,
    BorrowedRef<> name) {
  BorrowedRef<PyTypeObject> tp = Py_TYPE(obj);

  for (auto& entry : entries_) {
    if (entry.type == tp) {
      PyObject* result = entry.value;
      Py_INCREF(result);
      Py_INCREF(obj);
      return {result, obj};
    }
  }

  return lookupSlowPath(obj, name);
}

JITRT_LoadMethodResult LoadMethodCache::lookupHelper(
    LoadMethodCache* cache,
    BorrowedRef<> obj,
    BorrowedRef<> name) {
  return cache->lookup(obj, name);
}

// This needs to be kept in sync with PyType_Type.tp_getattro.
JITRT_LoadMethodResult LoadTypeMethodCache::lookup(
    BorrowedRef<PyTypeObject> obj,
    BorrowedRef<> name) {
  PyTypeObject* metatype = Py_TYPE(obj);
  if (metatype->tp_getattro != PyType_Type.tp_getattro) {
    maybeCollectCacheStats(
        cache_stats_, metatype, name, CacheMissReason::kWrongTpGetAttro);
    PyObject* res = PyObject_GetAttr(obj, name);
    Py_INCREF(Py_None);
    return {Py_None, res};
  }
  if (obj->tp_dict == nullptr) {
    if (PyType_Ready(obj) < 0) {
      return {nullptr, nullptr};
    }
  }

  descrgetfunc meta_get = nullptr;
  PyObject* meta_attribute = _PyType_Lookup(metatype, name);
  if (meta_attribute != nullptr) {
    Py_INCREF(meta_attribute);
    meta_get = Py_TYPE(meta_attribute)->tp_descr_get;

    if (meta_get != nullptr && PyDescr_IsData(meta_attribute)) {
      /* Data descriptors implement tp_descr_set to intercept
       * writes. Assume the attribute is not overridden in
       * type's tp_dict (and bases): call the descriptor now.
       */
      maybeCollectCacheStats(
          cache_stats_, metatype, name, CacheMissReason::kPyDescrIsData);
      PyObject* res =
          meta_get(meta_attribute, obj, reinterpret_cast<PyObject*>(metatype));
      Py_DECREF(meta_attribute);
      Py_INCREF(Py_None);
      return {Py_None, res};
    }
  }

  /* No data descriptor found on metatype. Look in tp_dict of this
   * type and its bases */
  PyObject* attribute = _PyType_Lookup(obj, name);
  if (attribute != nullptr) {
    Py_XDECREF(meta_attribute);
    BorrowedRef<PyTypeObject> attribute_type = Py_TYPE(attribute);
    if (attribute_type == &PyClassMethod_Type) {
      BorrowedRef<> cm_callable = Ci_PyClassMethod_GetFunc(attribute);
      if (Py_TYPE(cm_callable) == &PyFunction_Type) {
        Py_INCREF(obj);
        Py_INCREF(cm_callable);

        // Get the underlying callable from classmethod and return the
        // callable alongside the class object, allowing the runtime to call
        // the method as an unbound method.
        fill(obj, cm_callable, true);
        return {cm_callable, obj};
      } else if (Py_TYPE(cm_callable)->tp_descr_get != NULL) {
        // cm_callable has custom tp_descr_get that can run arbitrary
        // user code. Do not cache in this instance.
        maybeCollectCacheStats(
            cache_stats_, metatype, name, CacheMissReason::kUncategorized);
        Py_INCREF(Py_None);
        return {
            Py_None, Py_TYPE(cm_callable)->tp_descr_get(cm_callable, obj, obj)};
      } else {
        // It is not safe to cache custom objects decorated with classmethod
        // as they can be modified later
        maybeCollectCacheStats(
            cache_stats_, metatype, name, CacheMissReason::kUncategorized);
        BorrowedRef<> py_meth = PyMethod_New(cm_callable, obj);
        Py_INCREF(Py_None);
        return {Py_None, py_meth};
      }
    }
    if (attribute_type == &PyStaticMethod_Type) {
      BorrowedRef<> cm_callable = Ci_PyStaticMethod_GetFunc(attribute);
      Py_INCREF(cm_callable);
      Py_INCREF(Py_None);
      fill(obj, cm_callable, false);
      return {Py_None, cm_callable};
    }
    if (PyFunction_Check(attribute)) {
      Py_INCREF(attribute);
      Py_INCREF(Py_None);
      fill(obj, attribute, false);
      return {Py_None, attribute};
    }
    Py_INCREF(attribute);
    /* Implement descriptor functionality, if any */
    descrgetfunc local_get = Py_TYPE(attribute)->tp_descr_get;
    if (local_get != nullptr) {
      /* NULL 2nd argument indicates the descriptor was
       * found on the target object itself (or a base)  */
      maybeCollectCacheStats(
          cache_stats_, metatype, name, CacheMissReason::kUncategorized);
      PyObject* res = local_get(attribute, nullptr, obj);
      Py_DECREF(attribute);
      Py_INCREF(Py_None);
      return {Py_None, res};
    }
    maybeCollectCacheStats(
        cache_stats_, metatype, name, CacheMissReason::kUncategorized);
    Py_INCREF(Py_None);
    return {Py_None, attribute};
  }

  /* No attribute found in local __dict__ (or bases): use the
   * descriptor from the metatype, if any */
  if (meta_get != nullptr) {
    maybeCollectCacheStats(
        cache_stats_, metatype, name, CacheMissReason::kUncategorized);
    PyObject* res;
    res = meta_get(meta_attribute, obj, reinterpret_cast<PyObject*>(metatype));
    Py_DECREF(meta_attribute);
    Py_INCREF(Py_None);
    return {Py_None, res};
  }

  /* If an ordinary attribute was found on the metatype, return it now */
  if (meta_attribute != nullptr) {
    maybeCollectCacheStats(
        cache_stats_, metatype, name, CacheMissReason::kUncategorized);
    Py_INCREF(Py_None);
    return {Py_None, meta_attribute};
  }

  /* Give up */
  PyErr_Format(
      PyExc_AttributeError,
      "type object '%.50s' has no attribute '%U'",
      type->tp_name,
      name);
  return {nullptr, nullptr};
}

JITRT_LoadMethodResult LoadTypeMethodCache::getValueHelper(
    LoadTypeMethodCache* cache,
    BorrowedRef<> obj) {
  PyObject* result = cache->value;
  Py_INCREF(result);
  if (cache->is_unbound_meth) {
    Py_INCREF(obj);
    return {result, obj};
  } else {
    Py_INCREF(Py_None);
    return {Py_None, result};
  }
}

JITRT_LoadMethodResult LoadTypeMethodCache::lookupHelper(
    LoadTypeMethodCache* cache,
    BorrowedRef<PyTypeObject> obj,
    BorrowedRef<> name) {
  return cache->lookup(obj, name);
}

void LoadTypeMethodCache::typeChanged(BorrowedRef<PyTypeObject> /* type */) {
  type.reset();
  value.reset();
}

void LoadTypeMethodCache::initCacheStats(
    const char* filename,
    const char* method_name) {
  cache_stats_ = std::make_unique<CacheStats>();
  cache_stats_->filename = filename;
  cache_stats_->method_name = method_name;
}

void LoadTypeMethodCache::clearCacheStats() {
  cache_stats_->misses.clear();
}

const CacheStats* LoadTypeMethodCache::cacheStats() {
  return cache_stats_.get();
}

LoadTypeMethodCache::~LoadTypeMethodCache() {
  if (type != nullptr) {
    ltm_watcher.unwatch(type, this);
  }
}

void LoadTypeMethodCache::fill(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<> value,
    bool is_unbound_meth) {
  if (!PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG)) {
    // The type must have a valid version tag in order for us to be able to
    // invalidate the cache when the type is modified. See the comment at
    // the top of `PyType_Modified` for more details.
    return;
  }

  if (!PyType_HasFeature(type, Py_TPFLAGS_NO_SHADOWING_INSTANCES) &&
      (type->tp_dictoffset != 0)) {
    return;
  }
  ltm_watcher.unwatch(this->type, this);
  this->type = type;
  this->value = value;
  this->is_unbound_meth = is_unbound_meth;
  ltm_watcher.watch(type, this);
}
void notifyICsTypeChanged(BorrowedRef<PyTypeObject> type) {
  ac_watcher.typeChanged(type);
  ltac_watcher.typeChanged(type);
  lm_watcher.typeChanged(type);
  ltm_watcher.typeChanged(type);
}

} // namespace jit
