// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/inline_cache.h"

#include "Objects/dict-common.h"
#include "Python.h"
#include "switchboard.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/dict_watch.h"

#include <algorithm>

// clang-format off
#include "internal/pycore_pystate.h"
#include "internal/pycore_object.h"
#include "structmember.h"
// clang-format on

namespace jit {
namespace {

template <class T>
struct TypeWatcher {
  std::unordered_map<BorrowedRef<PyTypeObject>, std::vector<T*>> caches;

  void watch(BorrowedRef<PyTypeObject> type, T* cache) {
    caches[type].emplace_back(cache);
  }

  void typeChanged(BorrowedRef<PyTypeObject> type) {
    auto it = caches.find(type);
    if (it == caches.end()) {
      return;
    }
    std::vector<T*> to_notify = std::move(it->second);
    caches.erase(it);
    for (T* cache : to_notify) {
      cache->typeChanged(type);
    }
  }
};

TypeWatcher<AttributeCache> ac_watcher;
TypeWatcher<LoadTypeAttrCache> ltac_watcher;

} // namespace

AttributeMutator::AttributeMutator() {
  reset();
}

PyTypeObject* AttributeMutator::type() const {
  return type_;
}

void AttributeMutator::reset() {
  kind_ = Kind::kEmpty;
  type_ = nullptr;
}

bool AttributeMutator::isEmpty() const {
  return kind_ == Kind::kEmpty;
}

void AttributeMutator::set_combined(PyTypeObject* type) {
  kind_ = Kind::kCombined;
  type_ = type;
  combined_.dict_offset = type->tp_dictoffset;
}

void AttributeMutator::set_split(
    PyTypeObject* type,
    Py_ssize_t val_offset,
    PyDictKeysObject* keys) {
  kind_ = Kind::kSplit;
  type_ = type;
  split_.dict_offset = type->tp_dictoffset;
  split_.val_offset = val_offset;
  split_.keys = keys;
}

void AttributeMutator::set_data_descr(PyTypeObject* type, PyObject* descr) {
  kind_ = Kind::kDataDescr;
  type_ = type;
  data_descr_.descr = descr;
}

void AttributeMutator::set_member_descr(PyTypeObject* type, PyObject* descr) {
  kind_ = Kind::kMemberDescr;
  type_ = type;
  member_descr_.memberdef = ((PyMemberDescrObject*)descr)->d_member;
}

void AttributeMutator::set_descr_or_classvar(
    PyTypeObject* type,
    PyObject* descr) {
  kind_ = Kind::kDescrOrClassVar;
  type_ = type;
  descr_or_cvar_.descr = descr;
  descr_or_cvar_.dictoffset = type->tp_dictoffset;
}

void AttributeCache::typeChanged(PyTypeObject* type) {
  for (auto& entry : entries_) {
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
    if (descr_type->tp_descr_set != nullptr) {
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

AttributeMutator* AttributeCache::findEmptyEntry() {
  auto it = std::find_if(
      entries_.begin(), entries_.end(), [](const AttributeMutator& e) {
        return e.isEmpty();
      });
  return it == entries_.end() ? nullptr : it;
}

inline PyObject*
AttributeMutator::setAttr(PyObject* obj, PyObject* name, PyObject* value) {
  switch (kind_) {
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
          static_cast<int>(kind_));
  }
}

inline PyObject* AttributeMutator::getAttr(PyObject* obj, PyObject* name) {
  switch (kind_) {
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
      JIT_CHECK(
          false,
          "cannot invoke getAttr for attr of kind %d",
          static_cast<int>(kind_));
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

    Py_INCREF(value);
    dict->ma_values[val_offset] = value;
    _PyDict_IncVersionForSet(dict, name, value);

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
    Ref<> descr_guard(descr);
    int st = setter(descr, obj, value);
    return (st == -1) ? nullptr : Py_None;
  }
  PyObject** dictptr = _PyObject_GetDictPtrAtOffset(obj, dictoffset);
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

  Ref<> descr_guard(descr);
  if (setter != nullptr && getter != nullptr) {
    BorrowedRef<PyTypeObject> type(Py_TYPE(obj));
    return getter(descr, obj, type);
  }

  Ref<> dict;
  PyObject** dictptr = _PyObject_GetDictPtrAtOffset(obj, dictoffset);
  if (dictptr != nullptr) {
    dict.reset(*dictptr);
  }

  // Check instance dict.
  if (dict != nullptr) {
    Ref<> res(_PyDict_GetItem_UnicodeExact(dict, name));
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

  Ref<> name_guard(name);
  Ref<> descr(_PyType_Lookup(tp, name));
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
  for (auto& entry : entries_) {
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

  Ref<> name_guard(name);
  Ref<> descr(_PyType_Lookup(tp, name));
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
    Ref<> res(PyDict_GetItem(dict, name));
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

PyObject*
LoadAttrCache::invoke(LoadAttrCache* cache, PyObject* obj, PyObject* name) {
  return cache->doInvoke(obj, name);
}

PyObject* LoadAttrCache::doInvoke(PyObject* obj, PyObject* name) {
  PyTypeObject* tp = Py_TYPE(obj);
  for (auto& entry : entries_) {
    if (entry.type() == tp) {
      return entry.getAttr(obj, name);
    }
  }
  return invokeSlowPath(obj, name);
}

// NB: This needs to be kept in-sync with the logic in type_getattro
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

void GlobalCache::init(PyObject** cache) const {
  pair_->second.ptr = cache;

  // We want to try and only watch builtins if this is really a
  // builtin.  So we will start only watching globals, and if
  // the value gets deleted from globals then we'll start
  // tracking builtins as well.  Once we start tracking builtins
  // we'll never stop rather than trying to handle all of the
  // transitions.
  watchDictKey(key().globals, key().name, *this);
  PyObject* builtins = key().builtins;

  // We don't need to immediately watch builtins if it's
  // defined as a global
  if (PyObject* globals_value = PyDict_GetItem(key().globals, key().name)) {
    // the dict getitem could have triggered a lazy import with side effects
    // that unwatched the dict
    if (valuePtr()) {
      *valuePtr() = globals_value;
    }
  } else if (_PyDict_CanWatch(builtins)) {
    *valuePtr() = PyDict_GetItem(builtins, key().name);
    if (key().globals != builtins) {
      watchDictKey(builtins, key().name, *this);
    }
  }
}

void GlobalCache::update(
    PyObject* dict,
    PyObject* new_value,
    std::vector<GlobalCache>& to_disable) const {
  PyObject* builtins = key().builtins;
  if (dict == key().globals) {
    if (new_value == nullptr && key().globals != builtins) {
      if (!_PyDict_CanWatch(builtins)) {
        // builtins is no longer watchable. Mark this cache for disabling.
        to_disable.emplace_back(*this);
        return;
      }

      // Fall back to the builtin (which may also be null).
      *valuePtr() = PyDict_GetItem(builtins, key().name);

      // it changed, and it changed from something to nothing, so
      // we weren't watching builtins and need to start now.
      if (!isWatchedDictKey(builtins, key().name, *this)) {
        watchDictKey(builtins, key().name, *this);
      }
    } else {
      *valuePtr() = new_value;
    }
  } else {
    JIT_CHECK(dict == builtins, "Unexpected dict");
    JIT_CHECK(_PyDict_CanWatch(key().globals), "Bad globals dict");
    // Check if this value is shadowed.
    PyObject* globals_value = PyDict_GetItem(key().globals, key().name);
    if (globals_value == nullptr) {
      *valuePtr() = new_value;
    }
  }
}

void GlobalCache::disable() const {
  *valuePtr() = nullptr;
  jit::Runtime::get()->forgetLoadGlobalCache(*this);
}

void notifyICsTypeChanged(BorrowedRef<PyTypeObject> type) {
  ac_watcher.typeChanged(type);
  ltac_watcher.typeChanged(type);
}

} // namespace jit
