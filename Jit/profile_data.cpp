#include "Jit/profile_data.h"

#include "Jit/containers.h"
#include "Jit/hir/type.h"
#include "Jit/ref.h"

#include <zlib.h>

#include <cstdio>
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

UnorderedMap<std::string, UnorderedMap<int, std::vector<std::string>>> s_types;

template <typename T>
T read(std::FILE* file) {
  // TODO(bsimmers) Use std::endian::native when we have C++20.
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Integers in profile data files are little endian.
#endif

  static_assert(
      std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  T val;
  if (std::fread(&val, sizeof(val), 1, file) != 1) {
    throw std::runtime_error("Couldn't read enough bytes from file");
  }
  return val;
}

std::string readStr(std::FILE* file) {
  auto len = read<uint16_t>(file);
  std::string result(len, '\0');
  if (std::fread(result.data(), len, 1, file) != 1) {
    throw std::runtime_error("Couldn't read enough bytes from file");
  }
  return result;
}

void readVersion1(std::FILE* file) {
  auto num_code_keys = read<uint32_t>(file);
  for (size_t i = 0; i < num_code_keys; ++i) {
    std::string code_key = readStr(file);
    auto& code_map = s_types[code_key];

    auto num_locations = read<uint16_t>(file);
    for (size_t j = 0; j < num_locations; ++j) {
      auto bc_offset = read<uint16_t>(file);

      auto& type_list = code_map[bc_offset];
      auto num_types = read<uint8_t>(file);
      for (size_t k = 0; k < num_types; ++k) {
        type_list.emplace_back(readStr(file));
      }
    }
  }
}

} // namespace

bool loadProfileData(const char* filename) {
  std::FILE* file = std::fopen(filename, "r");
  if (file == nullptr) {
    return false;
  }

  try {
    auto magic = read<uint64_t>(file);
    if (magic != kMagicHeader) {
      JIT_LOG("Bad magic value %d in file %s", magic, filename);
      return false;
    }
    auto version = read<uint32_t>(file);
    if (version == 1) {
      readVersion1(file);
    } else {
      JIT_LOG("Unknown profile data version %d", version);
      return false;
    }
  } catch (const std::runtime_error& e) {
    JIT_LOG("Failed to load profile data from %s: %s", filename, e.what());
    s_types.clear();
    return false;
  }

  long pos = std::ftell(file);
  if (pos < 0 || std::fseek(file, 0, SEEK_END) != 0) {
    JIT_LOG("Failed to seek in %f", filename);
    return false;
  }
  long end = std::ftell(file);
  if (pos != end) {
    JIT_LOG(
        "File %s has %d bytes of extra data at the end", filename, end - pos);
  }

  JIT_LOG("Loaded data for %d code objects from %s", s_types.size(), filename);
  return true;
}

} // namespace jit
