// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/profile_runtime.h"

#include "Python.h"
#include "frameobject.h"
#include "opcode.h"

#include "Jit/hir/type.h"
#include "Jit/profile_data.h"

#include <istream>
#include <ostream>

namespace jit {

std::vector<hir::Type> ProfileRuntime::getProfiledTypes(
    BorrowedRef<PyCodeObject> code,
    BCOffset bc_off) const {
  auto code_profile = getProfileData(code);
  if (code_profile == nullptr) {
    return {};
  }

  // Ignore polymorphic bytecodes, for now.
  auto types = jit::getProfiledTypes(*code_profile, bc_off);
  if (types.size() != 1) {
    return {};
  }

  // PyTypeObject -> hir::Type.
  std::vector<hir::Type> result;
  for (auto const& type : types[0]) {
    // If there's no type recorded for the given value, then we fall back to
    // TTop.
    auto hir_type =
        type != nullptr ? hir::Type::fromTypeExact(type) : hir::TTop;
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
    }
  }
}

void ProfileRuntime::countProfiledInstrs(
    BorrowedRef<PyCodeObject> code,
    Py_ssize_t count) {
  profiles_[Ref<PyCodeObject>::create(code)].total_hits += count;
}

// TODO(alexanderm): All the following code that calls out to functions from
// profile_data.h is implicitly depending on ProfileRuntime and ProfileData both
// being singletons.

bool ProfileRuntime::hasPrimedDictKeys(BorrowedRef<PyTypeObject> type) const {
  return jit::hasPrimedDictKeys(type);
}

int ProfileRuntime::numCachedKeys(BorrowedRef<PyTypeObject> type) const {
  return jit::numCachedKeys(type);
}

void ProfileRuntime::enumerateCachedKeys(
    BorrowedRef<PyTypeObject> type,
    std::function<void(BorrowedRef<>)> callback) const {
  jit::enumerateCachedKeys(type, std::move(callback));
}

void ProfileRuntime::registerType(BorrowedRef<PyTypeObject> type) {
  registerProfiledType(type);
}

void ProfileRuntime::unregisterType(BorrowedRef<PyTypeObject> type) {
  unregisterProfiledType(type);
}

void ProfileRuntime::setStripPattern(std::regex regex) {
  profileDataStripPattern = std::move(regex);
}

bool ProfileRuntime::serialize(const std::string& filename) const {
  return writeProfileData(filename);
}

bool ProfileRuntime::serialize(std::ostream& stream) const {
  return writeProfileData(stream);
}

bool ProfileRuntime::deserialize(const std::string& filename) {
  can_profile_ = false;
  return readProfileData(filename);
}

bool ProfileRuntime::deserialize(std::istream& stream) {
  can_profile_ = false;
  return readProfileData(stream);
}

void ProfileRuntime::clear() {
  clearProfileData();
  profiles_.clear();

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

} // namespace jit
