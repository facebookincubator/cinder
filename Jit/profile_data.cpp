#include "Jit/profile_data.h"

#include "Jit/ref.h"

#include <zlib.h>

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

} // namespace jit
