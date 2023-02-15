// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/profile_data.h"

#include "Objects/dict-common.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/containers.h"
#include "Jit/hir/type.h"
#include "Jit/live_type_map.h"
#include "Jit/ref.h"

#include <zlib.h>

#include <fstream>
#include <type_traits>

namespace jit {

uint32_t hashBytecode(PyCodeObject* code) {
  uint32_t crc = crc32(0, nullptr, 0);
  BorrowedRef<> bc = code->co_code;
  if (!PyBytes_Check(bc)) {
    return crc;
  }

  char* buffer;
  Py_ssize_t len;
  if (PyBytes_AsStringAndSize(bc, &buffer, &len) < 0) {
    return crc;
  }

  return crc32(crc, reinterpret_cast<unsigned char*>(buffer), len);
}

namespace {

const uint64_t kMagicHeader = 0x7265646e6963;

using ProfileData = UnorderedMap<CodeKey, CodeProfileData>;
ProfileData s_profile_data;

using TypeDictKeys = UnorderedMap<std::string, std::vector<std::string>>;
TypeDictKeys s_type_dict_keys;

LiveTypeMap s_live_types;

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

void readVersion1(std::istream& stream) {
  auto num_code_keys = read<uint32_t>(stream);
  for (size_t i = 0; i < num_code_keys; ++i) {
    std::string code_key = readStr(stream);
    auto& code_map = s_profile_data[code_key];

    auto num_locations = read<uint16_t>(stream);
    for (size_t j = 0; j < num_locations; ++j) {
      auto bc_offset = BCOffset{read<uint16_t>(stream)};

      auto& type_list = code_map[bc_offset];
      auto num_types = read<uint8_t>(stream);
      for (size_t k = 0; k < num_types; ++k) {
        std::vector<std::string> single_profile{readStr(stream)};
        type_list.emplace_back(single_profile);
      }
    }
  }
}

void readVersion2(std::istream& stream) {
  auto num_code_keys = read<uint32_t>(stream);
  for (size_t i = 0; i < num_code_keys; ++i) {
    std::string code_key = readStr(stream);
    auto& code_map = s_profile_data[code_key];

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

void writeVersion3(std::ostream& stream, const TypeProfiles& profiles) {
  ProfileData data;
  std::unordered_set<BorrowedRef<PyTypeObject>> dict_key_types;

  for (auto& [code_obj, code_profile] : profiles) {
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
      data.emplace(codeKey(code_obj), std::move(code_data));
    }
  }

  // Second, write data to the given stream.
  write<uint32_t>(stream, data.size());
  for (auto& [code_key, code_data] : data) {
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
}

void readVersion3(std::istream& stream) {
  readVersion2(stream);
  auto num_type_key_lists = read<uint32_t>(stream);
  for (size_t i = 0; i < num_type_key_lists; ++i) {
    auto& vec = s_type_dict_keys[readStr(stream)];
    auto num_key_names = read<uint16_t>(stream);
    for (size_t j = 0; j < num_key_names; ++j) {
      vec.emplace_back(readStr(stream));
    }
  }
}

void readVersion4(std::istream& stream) {
  constexpr uint32_t kTargetVersion = PY_VERSION_HEX >> 16;

  auto num_py_versions = read<uint8_t>(stream);
  std::vector<uint16_t> found_versions;
  for (size_t i = 0; i < num_py_versions; ++i) {
    auto py_version = read<uint16_t>(stream);
    auto offset = read<uint32_t>(stream);
    if (py_version == kTargetVersion) {
      JIT_LOG(
          "Loading profile for Python version %#x at offset %d",
          kTargetVersion,
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
      kTargetVersion,
      versions_str);
}

} // namespace

bool readProfileData(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    JIT_LOG("Failed to open %s for reading", filename);
    return false;
  }
  if (readProfileData(file)) {
    JIT_LOG(
        "Loaded data for %d code objects and %d types from %s",
        s_profile_data.size(),
        s_type_dict_keys.size(),
        filename);
    return true;
  }
  return false;
}

bool readProfileData(std::istream& stream) {
  try {
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    auto magic = read<uint64_t>(stream);
    if (magic != kMagicHeader) {
      JIT_LOG("Bad magic value %#x in profile data stream", magic);
      return false;
    }
    auto version = read<uint32_t>(stream);
    if (version == 1) {
      readVersion1(stream);
    } else if (version == 2) {
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
    s_profile_data.clear();
    return false;
  }

  stream.exceptions(std::ios::iostate{});
  if (stream.peek() != EOF) {
    JIT_LOG("Warning: stream has unread data at end");
  }

  return true;
}

bool writeProfileData(const std::string& filename) {
  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    JIT_LOG("Failed to open %s for writing", filename);
    return false;
  }
  if (writeProfileData(file)) {
    JIT_LOG("Wrote %d bytes of profile data to %s", file.tellp(), filename);
    return true;
  }
  return false;
}

bool writeProfileData(std::ostream& stream) {
  try {
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    write<uint64_t>(stream, kMagicHeader);
    write<uint32_t>(stream, 3);
    writeVersion3(stream, Runtime::get()->typeProfiles());
  } catch (const std::runtime_error& e) {
    JIT_LOG("Failed to write profile data to stream: %s", e.what());
    return false;
  }

  return true;
}

void clearProfileData() {
  s_profile_data.clear();
  s_live_types.clear();
}

const CodeProfileData* getProfileData(PyCodeObject* code) {
  auto it = s_profile_data.find(codeKey(code));
  return it == s_profile_data.end() ? nullptr : &it->second;
}

PolymorphicTypes getProfiledTypes(
    const CodeProfileData& data,
    BCOffset bc_off) {
  auto it = data.find(bc_off);
  if (it == data.end()) {
    return {};
  }

  PolymorphicTypes ret;
  for (const std::vector<std::string>& profiled_types : it->second) {
    std::vector<BorrowedRef<PyTypeObject>> single_profile;
    for (const std::string& type_name : profiled_types) {
      single_profile.emplace_back(s_live_types.get(type_name));
    }
    ret.emplace_back(single_profile);
  }
  return ret;
}

std::string codeKey(PyCodeObject* code) {
  const std::string filename = unicodeAsString(code->co_filename);
  const int firstlineno = code->co_firstlineno;
  const std::string qualname = codeQualname(code);
  uint32_t hash = hashBytecode(code);
  return fmt::format("{}:{}:{}:{}", filename, firstlineno, qualname, hash);
}

std::string codeQualname(PyCodeObject* code) {
  if (code->co_qualname != nullptr) {
    return unicodeAsString(code->co_qualname);
  }
  if (code->co_name != nullptr) {
    return unicodeAsString(code->co_name);
  }
  return "<unknown>";
}

int numCachedKeys(BorrowedRef<PyTypeObject> type) {
  if (!PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
    return 0;
  }
  BorrowedRef<PyHeapTypeObject> ht(type);
  if (ht->ht_cached_keys == nullptr) {
    return 0;
  }
  return ht->ht_cached_keys->dk_nentries;
}

void enumerateCachedKeys(
    BorrowedRef<PyTypeObject> type,
    std::function<void(BorrowedRef<>)> callback) {
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

void registerProfiledType(PyTypeObject* type) {
  s_live_types.insert(type);

  if (!PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
    return;
  }
  std::string name = typeFullname(type);
  auto it = s_type_dict_keys.find(name);
  if (it == s_type_dict_keys.end()) {
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
  BorrowedRef<PyHeapTypeObject> ht{reinterpret_cast<PyObject*>(type)};
  PyDictKeysObject* old_keys = ht->ht_cached_keys;
  ht->ht_cached_keys = keys;
  PyType_Modified(type);
  _PyType_Lookup(type, dunder_dict);
  if (old_keys != nullptr) {
    _PyDictKeys_DecRef(old_keys);
  }
}

void unregisterProfiledType(PyTypeObject* type) {
  s_live_types.erase(type);
}

} // namespace jit
