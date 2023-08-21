// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/profile_runtime.h"

#include "Objects/dict-common.h"
#include "Python.h"
#include "frameobject.h"
#include "opcode.h"

#include "Jit/hir/type.h"
#include "Jit/live_type_map.h"
#include "Jit/log.h"

#include <folly/tracing/StaticTracepoint.h>

#include <fstream>
#include <istream>
#include <ostream>

namespace jit {

namespace {

// TODO(alexanderm): This should be a field on ProfileRuntime, but that
// currently breaks our tests. We depend upon being able to reset the
// jit::Runtime object without resetting the LiveTypeMap.
static LiveTypeMap s_live_types;

constexpr uint64_t kMagicHeader = 0x7265646e6963;
constexpr uint32_t kThisPyVersion = PY_VERSION_HEX >> 16;

// TODO(bsimmers) Use std::endian::native when we have C++20.
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Integers in profile data files are little endian.
#endif

template <typename T>
T read(std::istream& stream) {
  static_assert(
      std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  T val;
  stream.read(reinterpret_cast<char*>(&val), sizeof(val));
  return val;
}

template <typename T>
void write(std::ostream& stream, T value) {
  static_assert(
      std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeStr(std::ostream& stream, const std::string& str) {
  write<uint16_t>(stream, str.size());
  stream.write(str.data(), str.size());
}

std::string readStr(std::istream& stream) {
  auto len = read<uint16_t>(stream);
  std::string result(len, '\0');
  stream.read(result.data(), len);
  return result;
}

} // namespace

std::vector<hir::Type> ProfileRuntime::getProfiledTypes(
    BorrowedRef<PyCodeObject> code,
    BCOffset bc_off) const {
  return getProfiledTypes(code, codeKey(code), bc_off);
}

std::vector<hir::Type> ProfileRuntime::getProfiledTypes(
    BorrowedRef<PyCodeObject> code,
    const CodeKey& code_key,
    BCOffset bc_off) const {
  // Always prioritize profiles loaded from a file.
  auto loaded_types = getLoadedProfiledTypes(code_key, bc_off);
  if (!loaded_types.empty()) {
    return loaded_types;
  }

  auto code_it = profiles_.find(code);
  if (code_it == profiles_.end()) {
    return {};
  }
  auto& code_profile = code_it->second;

  auto type_profiler_it = code_profile.typed_hits.find(bc_off);
  if (type_profiler_it == code_profile.typed_hits.end()) {
    return {};
  }

  // Ignore polymorphic bytecodes, for now.
  auto& type_profiler = type_profiler_it->second;
  if (type_profiler->empty() || type_profiler->isPolymorphic()) {
    return {};
  }

  // PyTypeObject -> hir::Type.
  std::vector<hir::Type> result;
  for (int col = 0; col < type_profiler->cols(); ++col) {
    auto py_type = type_profiler->type(0, col);
    auto hir_type =
        py_type != nullptr ? hir::Type::fromTypeExact(py_type) : hir::TTop;
    result.emplace_back(hir_type);
  }
  return result;
}

std::vector<hir::Type> ProfileRuntime::getLoadedProfiledTypes(
    CodeKey code,
    BCOffset bc_off) const {
  auto code_it = loaded_profiles_.find(code);
  if (code_it == loaded_profiles_.end()) {
    return {};
  }
  auto& code_profile_data = code_it->second;

  auto types_it = code_profile_data.find(bc_off);
  if (types_it == code_profile_data.end()) {
    return {};
  }
  auto& types = types_it->second;

  // Ignore polymorphic bytecodes, for now.
  if (types.size() != 1) {
    return {};
  }

  // std::string -> PyTypeObject -> hir::Type.
  std::vector<hir::Type> result;
  for (auto const& type_name : types[0]) {
    // If there's no type recorded for the given value, then we fall back to
    // TTop.
    auto py_type = s_live_types.get(type_name);
    auto hir_type =
        py_type != nullptr ? hir::Type::fromTypeExact(py_type) : hir::TTop;
    result.emplace_back(hir_type);
  }
  return result;
}

void ProfileRuntime::profileInstr(
    BorrowedRef<PyFrameObject> frame,
    PyObject** stack_top,
    int opcode,
    int oparg) {
  if (!can_profile_) {
    return;
  }

  auto profile_stack = [&](auto... stack_offsets) {
    FOLLY_SDT(
        python,
        profile_bytecode,
        codeQualname(frame->f_code).c_str(),
        frame->f_lasti,
        opcode,
        oparg);

    CodeProfile& code_profile =
        profiles_[Ref<PyCodeObject>::create(frame->f_code)];
    int opcode_offset = frame->f_lasti * sizeof(_Py_CODEUNIT);

    auto pair = code_profile.typed_hits.emplace(opcode_offset, nullptr);
    if (pair.second) {
      constexpr size_t kProfilerRows = 4;
      pair.first->second =
          TypeProfiler::create(kProfilerRows, sizeof...(stack_offsets));
    }
    auto get_type = [&](int offset) {
      PyObject* obj = stack_top[-(offset + 1)];
      return obj != nullptr ? Py_TYPE(obj) : nullptr;
    };
    pair.first->second->recordTypes(get_type(stack_offsets)...);
  };

  // TODO(T127457244): Centralize the information about which stack inputs are
  // interesting for which opcodes.
  switch (opcode) {
    case BEFORE_ASYNC_WITH:
    case DELETE_ATTR:
    case END_ASYNC_FOR:
    case FOR_ITER:
    case GET_AITER:
    case GET_ANEXT:
    case GET_AWAITABLE:
    case GET_ITER:
    case GET_LEN:
    case GET_YIELD_FROM_ITER:
    case JUMP_IF_FALSE_OR_POP:
    case JUMP_IF_TRUE_OR_POP:
    case LIST_TO_TUPLE:
    case LOAD_ATTR:
    case LOAD_FIELD:
    case LOAD_METHOD:
    case MATCH_MAPPING:
    case MATCH_SEQUENCE:
    case POP_JUMP_IF_FALSE:
    case POP_JUMP_IF_TRUE:
    case RETURN_VALUE:
    case SETUP_WITH:
    case STORE_DEREF:
    case STORE_GLOBAL:
    case UNARY_INVERT:
    case UNARY_NEGATIVE:
    case UNARY_NOT:
    case UNARY_POSITIVE:
    case UNPACK_EX:
    case UNPACK_SEQUENCE:
    case YIELD_FROM:
    case YIELD_VALUE: {
      profile_stack(0);
      break;
    }
    case BINARY_ADD:
    case BINARY_AND:
    case BINARY_FLOOR_DIVIDE:
    case BINARY_LSHIFT:
    case BINARY_MATRIX_MULTIPLY:
    case BINARY_MODULO:
    case BINARY_MULTIPLY:
    case BINARY_OR:
    case BINARY_POWER:
    case BINARY_RSHIFT:
    case BINARY_SUBSCR:
    case BINARY_SUBTRACT:
    case BINARY_TRUE_DIVIDE:
    case BINARY_XOR:
    case COMPARE_OP:
    case CONTAINS_OP:
    case COPY_DICT_WITHOUT_KEYS:
    case DELETE_SUBSCR:
    case DICT_MERGE:
    case DICT_UPDATE:
    case INPLACE_ADD:
    case INPLACE_AND:
    case INPLACE_FLOOR_DIVIDE:
    case INPLACE_LSHIFT:
    case INPLACE_MATRIX_MULTIPLY:
    case INPLACE_MODULO:
    case INPLACE_MULTIPLY:
    case INPLACE_OR:
    case INPLACE_POWER:
    case INPLACE_RSHIFT:
    case INPLACE_SUBTRACT:
    case INPLACE_TRUE_DIVIDE:
    case INPLACE_XOR:
    case IS_OP:
    case JUMP_IF_NOT_EXC_MATCH:
    case LIST_APPEND:
    case LIST_EXTEND:
    case MAP_ADD:
    case MATCH_KEYS:
    case SET_ADD:
    case SET_UPDATE:
    case STORE_ATTR:
    case STORE_FIELD: {
      profile_stack(1, 0);
      break;
    }
    case MATCH_CLASS:
    case RERAISE:
    case STORE_SUBSCR: {
      profile_stack(2, 1, 0);
      break;
    }
    case CALL_FUNCTION: {
      profile_stack(oparg);
      break;
    };
    case CALL_FUNCTION_EX: {
      // There's always an iterable of args but if the lowest bit is set then
      // there is also a mapping of kwargs. Also profile the callee.
      if (oparg & 0x01) {
        profile_stack(2, 1, 0);
      } else {
        profile_stack(1, 0);
      }
      break;
    }
    case CALL_FUNCTION_KW: {
      // There is a names tuple on top of the args pushed onto the stack that
      // the oparg does not take into account.
      profile_stack(oparg + 1);
      break;
    }
    case CALL_METHOD: {
      profile_stack(oparg + 1, oparg);
      break;
    }
    case WITH_EXCEPT_START: {
      // TOS6 is a function to call; the other values aren't interesting.
      profile_stack(6);
      break;
    }

    // The below are all shadow bytecodes that will be removed with 3.12.
    case LOAD_ATTR_DICT_DESCR:
    case LOAD_ATTR_DICT_NO_DESCR:
    case LOAD_ATTR_MODULE:
    case LOAD_ATTR_NO_DICT_DESCR:
    case LOAD_ATTR_POLYMORPHIC:
    case LOAD_ATTR_SLOT:
    case LOAD_ATTR_SPLIT_DICT:
    case LOAD_ATTR_SPLIT_DICT_DESCR:
    case LOAD_ATTR_S_MODULE:
    case LOAD_ATTR_TYPE:
    case LOAD_ATTR_UNCACHABLE:
    case LOAD_METHOD_DICT_DESCR:
    case LOAD_METHOD_DICT_METHOD:
    case LOAD_METHOD_MODULE:
    case LOAD_METHOD_NO_DICT_DESCR:
    case LOAD_METHOD_NO_DICT_METHOD:
    case LOAD_METHOD_SPLIT_DICT_DESCR:
    case LOAD_METHOD_SPLIT_DICT_METHOD:
    case LOAD_METHOD_S_MODULE:
    case LOAD_METHOD_TYPE:
    case LOAD_METHOD_TYPE_METHODLIKE:
    case LOAD_METHOD_UNCACHABLE:
    case LOAD_METHOD_UNSHADOWED_METHOD:
    case LOAD_PRIMITIVE_FIELD:
      profile_stack(0);
      break;
    case BINARY_SUBSCR_DICT:
    case BINARY_SUBSCR_DICT_STR:
    case BINARY_SUBSCR_LIST:
    case BINARY_SUBSCR_TUPLE:
    case BINARY_SUBSCR_TUPLE_CONST_INT:
    case STORE_ATTR_DESCR:
    case STORE_ATTR_DICT:
    case STORE_ATTR_SLOT:
    case STORE_ATTR_SPLIT_DICT:
    case STORE_ATTR_UNCACHABLE:
    case STORE_PRIMITIVE_FIELD:
      profile_stack(1, 0);
      break;
  }
}

void ProfileRuntime::countProfiledInstrs(
    BorrowedRef<PyCodeObject> code,
    Py_ssize_t count) {
  profiles_[Ref<PyCodeObject>::create(code)].total_hits += count;
}

bool ProfileRuntime::hasPrimedDictKeys(BorrowedRef<PyTypeObject> type) const {
  // If we have never loaded a serialized profile, then we assume that types
  // will always have primed dict keys.  The simplifier already checks whether
  // the type has cached keys.
  return loaded_profiles_.empty() || s_live_types.hasPrimedDictKeys(type);
}

int ProfileRuntime::numCachedKeys(BorrowedRef<PyTypeObject> type) const {
  if (!PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
    return 0;
  }
  BorrowedRef<PyHeapTypeObject> ht(type);
  if (ht->ht_cached_keys == nullptr) {
    return 0;
  }
  return ht->ht_cached_keys->dk_nentries;
}

void ProfileRuntime::enumerateCachedKeys(
    BorrowedRef<PyTypeObject> type,
    std::function<void(BorrowedRef<>)> callback) const {
  int num_keys = numCachedKeys(type);
  if (num_keys <= 0) {
    return;
  }
  BorrowedRef<PyHeapTypeObject> ht(type);
  PyDictKeyEntry* entries = _PyDictKeys_GetEntries(ht->ht_cached_keys);
  for (Py_ssize_t i = 0; i < num_keys; ++i) {
    callback(entries[i].me_key);
  }
}

void ProfileRuntime::registerType(BorrowedRef<PyTypeObject> type) {
  s_live_types.insert(type);

  if (!PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
    return;
  }
  std::string name = typeFullname(type);
  auto it = type_dict_keys_.find(name);
  if (it == type_dict_keys_.end()) {
    return;
  }
  auto dunder_dict = Ref<>::steal(PyUnicode_InternFromString("__dict__"));
  if (dunder_dict == nullptr) {
    return;
  }
  auto dict = Ref<>::steal(PyDict_New());
  if (dict == nullptr) {
    PyErr_Clear();
    return;
  }
  for (const std::string& key : it->second) {
    if (PyDict_SetItemString(dict, key.c_str(), Py_None) < 0) {
      return;
    }
  }

  PyDictKeysObject* keys = _PyDict_MakeKeysShared(dict);
  if (keys == nullptr) {
    return;
  }
  BorrowedRef<PyHeapTypeObject> ht{type};
  PyDictKeysObject* old_keys = ht->ht_cached_keys;
  ht->ht_cached_keys = keys;
  PyType_Modified(type);
  PyUnstable_Type_AssignVersionTag(type);
  if (old_keys != nullptr) {
    _PyDictKeys_DecRef(old_keys);
  }
  s_live_types.setPrimedDictKeys(type);
}

void ProfileRuntime::unregisterType(BorrowedRef<PyTypeObject> type) {
  s_live_types.erase(type);
}

void ProfileRuntime::setStripPattern(std::regex regex) {
  strip_pattern_ = std::move(regex);
}

bool ProfileRuntime::serialize(const std::string& filename) const {
  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    JIT_LOG("Failed to open %s for writing", filename);
    return false;
  }
  JIT_LOG("Writing out profiling data to %s", filename);
  return serialize(file);
}

bool ProfileRuntime::serialize(std::ostream& stream) const {
  auto start_pos = stream.tellp();

  try {
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    write<uint64_t>(stream, kMagicHeader);
    write<uint32_t>(stream, 4);
    auto [num_codes, num_types] = writeVersion4(stream);
    JIT_LOG(
        "Wrote %d bytes of profile data for %d code objects and %d types",
        stream.tellp() - start_pos,
        num_codes,
        num_types);
  } catch (const std::runtime_error& e) {
    JIT_LOG("Failed to write profile data to stream: %s", e.what());
    return false;
  }

  return true;
}

bool ProfileRuntime::deserialize(const std::string& filename) {
  can_profile_ = false;

  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    JIT_LOG("Failed to open %s for reading", filename);
    return false;
  }
  JIT_LOG("Loading profile data from %s", filename);
  return deserialize(file);
}

bool ProfileRuntime::deserialize(std::istream& stream) {
  can_profile_ = false;

  auto start_pos = stream.tellg();

  try {
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    auto magic = read<uint64_t>(stream);
    if (magic != kMagicHeader) {
      JIT_LOG("Bad magic value %#x in profile data stream", magic);
      return false;
    }
    auto version = read<uint32_t>(stream);
    if (version == 2) {
      readVersion2(stream);
    } else if (version == 3) {
      readVersion3(stream);
    } else if (version == 4) {
      readVersion4(stream);
    } else {
      JIT_LOG("Unknown profile data version %d", version);
      return false;
    }
  } catch (const std::runtime_error& e) {
    JIT_LOG("Failed to load profile data from stream: %s", e.what());
    loaded_profiles_.clear();
    return false;
  }

  auto cur_pos = stream.tellg();

  stream.exceptions(std::ios::iostate{});
  if (stream.peek() != EOF) {
    JIT_LOG("Warning: stream has unread data at end");
  }

  JIT_LOG(
      "Loaded %d bytes of data for %d code objects and %d types",
      cur_pos - start_pos,
      loaded_profiles_.size(),
      type_dict_keys_.size());

  return true;
}

void ProfileRuntime::clear() {
  profiles_.clear();
  loaded_profiles_.clear();
  s_live_types.clear();

  can_profile_ = true;
}

ProfileRuntime::iterator ProfileRuntime::begin() {
  return profiles_.begin();
}

ProfileRuntime::iterator ProfileRuntime::end() {
  return profiles_.end();
}

ProfileRuntime::const_iterator ProfileRuntime::begin() const {
  return profiles_.cbegin();
}

ProfileRuntime::const_iterator ProfileRuntime::end() const {
  return profiles_.cend();
}

CodeKey ProfileRuntime::codeKey(BorrowedRef<PyCodeObject> code) const {
  const std::string filename = std::regex_replace(
      unicodeAsString(code->co_filename), strip_pattern_, "");
  const int firstlineno = code->co_firstlineno;
  const std::string qualname = codeQualname(code);
  uint32_t hash = hashBytecode(code);
  return fmt::format("{}:{}:{}:{}", filename, firstlineno, qualname, hash);
}

void ProfileRuntime::readVersion2(std::istream& stream) {
  auto num_code_keys = read<uint32_t>(stream);
  for (size_t i = 0; i < num_code_keys; ++i) {
    std::string code_key = readStr(stream);
    auto& code_map = loaded_profiles_[code_key];

    auto num_locations = read<uint16_t>(stream);
    for (size_t j = 0; j < num_locations; ++j) {
      auto bc_offset = BCOffset{read<uint16_t>(stream)};

      auto& type_list = code_map[bc_offset];
      auto num_profs = read<uint8_t>(stream);
      for (size_t p = 0; p < num_profs; ++p) {
        std::vector<std::string> single_profile;
        auto num_types = read<uint8_t>(stream);
        for (size_t k = 0; k < num_types; ++k) {
          single_profile.emplace_back(readStr(stream));
        }
        type_list.emplace_back(single_profile);
      }
    }
  }
}

void ProfileRuntime::readVersion3(std::istream& stream) {
  readVersion2(stream);
  auto num_type_key_lists = read<uint32_t>(stream);
  for (size_t i = 0; i < num_type_key_lists; ++i) {
    auto& vec = type_dict_keys_[readStr(stream)];
    auto num_key_names = read<uint16_t>(stream);
    for (size_t j = 0; j < num_key_names; ++j) {
      vec.emplace_back(readStr(stream));
    }
  }
}

void ProfileRuntime::readVersion4(std::istream& stream) {
  auto num_py_versions = read<uint8_t>(stream);
  std::vector<uint16_t> found_versions;
  for (size_t i = 0; i < num_py_versions; ++i) {
    auto py_version = read<uint16_t>(stream);
    auto offset = read<uint32_t>(stream);
    if (py_version == kThisPyVersion) {
      JIT_LOG(
          "Loading profile for Python version %#x at offset %d",
          kThisPyVersion,
          offset);
      stream.seekg(offset);
      readVersion3(stream);
      // Avoid a warning about unread data at the end of the stream.
      stream.seekg(0, std::ios_base::end);
      return;
    }
    found_versions.emplace_back(py_version);
  }

  std::string versions_str;
  std::string_view sep = "";
  for (uint16_t version : found_versions) {
    format_to(versions_str, "{}{:#x}", sep, version);
    sep = ", ";
  }
  JIT_LOG(
      "Couldn't find target version %#x in profile data; found versions [%s]",
      kThisPyVersion,
      versions_str);
}

std::pair<size_t, size_t> ProfileRuntime::writeVersion4(
    std::ostream& stream) const {
  UnorderedMap<CodeKey, CodeProfileData> serialized;
  std::unordered_set<BorrowedRef<PyTypeObject>> dict_key_types;

  // First, serialize the recorded profiling information into the same form as
  // what we load from files.
  for (auto& [code_obj, code_profile] : *this) {
    CodeProfileData code_data;
    for (auto& profile_pair : code_profile.typed_hits) {
      const TypeProfiler& profile = *profile_pair.second;
      if (profile.empty() || profile.isPolymorphic()) {
        // The profile isn't interesting. Ignore it.
        continue;
      }
      auto& vec = code_data[profile_pair.first];
      // Store a list of profile row indices sorted by number of times seen
      std::vector<int> sorted_rows;
      for (int row = 0; row < profile.rows() && profile.count(row) > 0; row++) {
        sorted_rows.emplace_back(row);
      }
      std::sort(sorted_rows.begin(), sorted_rows.end(), [&](int a, int b) {
        return profile.count(a) > profile.count(b);
      });
      for (int row : sorted_rows) {
        std::vector<std::string> single_profile;
        for (int col = 0; col < profile.cols(); ++col) {
          BorrowedRef<PyTypeObject> type = profile.type(row, col);
          if (type == nullptr) {
            single_profile.emplace_back("<NULL>");
          } else {
            if (numCachedKeys(type) > 0) {
              dict_key_types.emplace(type);
            }
            single_profile.emplace_back(typeFullname(type));
          }
        }
        vec.emplace_back(single_profile);
      }
    }
    if (!code_data.empty()) {
      serialized.emplace(codeKey(code_obj), std::move(code_data));
    }
  }

  // Second, write the data to the given stream.
  constexpr int kNumPyVersions = 1;
  write<uint8_t>(stream, kNumPyVersions);
  write<uint16_t>(stream, kThisPyVersion);
  int32_t version_offset = long{stream.tellp()} + sizeof(uint32_t);
  write<uint32_t>(stream, version_offset);

  write<uint32_t>(stream, serialized.size());
  for (auto& [code_key, code_data] : serialized) {
    writeStr(stream, code_key);
    write<uint16_t>(stream, code_data.size());
    for (auto& [bc_offset, type_vec] : code_data) {
      write<uint16_t>(stream, bc_offset.value());
      write<uint8_t>(stream, type_vec.size());
      for (auto& single_profile : type_vec) {
        write<uint8_t>(stream, single_profile.size());
        for (auto& type_name : single_profile) {
          writeStr(stream, type_name);
        }
      }
    }
  }

  write<uint32_t>(stream, dict_key_types.size());
  for (const BorrowedRef<PyTypeObject>& type : dict_key_types) {
    writeStr(stream, typeFullname(type));
    write<uint16_t>(stream, numCachedKeys(type));
    enumerateCachedKeys(type, [&](BorrowedRef<> key) {
      writeStr(stream, unicodeAsString(key));
    });
  }

  return {serialized.size(), dict_key_types.size()};
}

} // namespace jit
