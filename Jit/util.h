#ifndef JIT_UTIL_H
#define JIT_UTIL_H

#include <cstdint>
#include <limits>
#include <type_traits>
#include "Python.h"

#ifdef __cplusplus
#include "Jit/log.h"

#include <cstdarg>
#include <cstddef>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#define DISALLOW_COPY_AND_ASSIGN(klass) \
  klass(const klass&) = delete;         \
  klass& operator=(const klass&) = delete

#define GET_STRUCT_MEMBER_OFFSET(__type, __mem) \
  (reinterpret_cast<uintptr_t>(&(reinterpret_cast<__type*>(0)->__mem)))

#define UNUSED __attribute__((unused))

extern "C" {
#endif

struct jit_string_t* ss_alloc(void);
void ss_free(struct jit_string_t* ss);
void ss_reset(struct jit_string_t* ss);
int ss_is_empty(const struct jit_string_t* ss);
const char* ss_get_string(const struct jit_string_t* ss);
int ss_vsprintf(struct jit_string_t* ss, const char* format, va_list args);
int ss_sprintf(struct jit_string_t* ss, const char* format, ...);
struct jit_string_t* ss_sprintf_alloc(const char* format, ...);

#ifdef __cplusplus
}

const bool py_debug =
#ifdef Py_DEBUG
    true;
#else
    false;
#endif

struct jit_string_deleter {
  void operator()(jit_string_t* ss) const {
    ss_free(ss);
  }
};

using auto_jit_string_t = std::unique_ptr<jit_string_t, jit_string_deleter>;

const char* ss_get_string(const auto_jit_string_t& ss);

namespace jit {

const int kPointerSize = sizeof(void*);

const int kCoFlagsAnyGenerator =
    CO_ASYNC_GENERATOR | CO_COROUTINE | CO_GENERATOR | CO_ITERABLE_COROUTINE;

// If stable pointers are enabled (with a call to setUseStablePointers(true))
// return 0xdeadbeef. Otherwise, return the original pointer.
const void* getStablePointer(const void* ptr);

// Enable or disable pointer sanitization.
void setUseStablePointers(bool enable);

inline std::size_t combineHash(std::size_t seed, std::size_t hash) {
  return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

std::string funcFullname(PyFunctionObject* func);

inline int popcount(unsigned i) {
  return __builtin_popcount(i);
}

inline int popcount(unsigned long i) {
  return __builtin_popcountl(i);
}

inline int popcount(unsigned long long i) {
  return __builtin_popcountll(i);
}

// Look up an item in the given map, aborting if the key doesn't exist. Similar
// to map.at(key) but with a less opaque failure mode.
template <typename M, typename K>
auto& map_get(M& map, const K& key) {
  auto it = map.find(key);
  JIT_DCHECK(it != map.end(), "Key not found in map");
  return it->second;
}

// Look up an item in the given map. If the key doesn't exist, return the
// default value.
template <typename M>
const typename M::mapped_type map_get(
    M& map,
    const typename M::key_type& key,
    const typename M::mapped_type& def) {
  auto it = map.find(key);
  if (it == map.end()) {
    return def;
  }
  return it->second;
}

// A queue that doesn't enqueue items that are already present. Items must be
// hashable with std::hash.
template <typename T>
class Worklist {
 public:
  bool empty() const {
    return queue_.empty();
  }

  const T& front() const {
    JIT_DCHECK(!empty(), "Worklist is empty");
    return queue_.front();
  }

  void push(const T& item) {
    if (set_.insert(item).second) {
      queue_.push(item);
    }
  }

  void pop() {
    set_.erase(front());
    queue_.pop();
  }

 private:
  std::queue<T> queue_;
  std::unordered_set<T> set_;
};

template <typename T>
bool fitsInt32(T val) {
  static_assert(std::is_integral<T>::value, "Integral input only.");
  int64_t v = val;
  return (
      v <= std::numeric_limits<int32_t>::max() &&
      v >= std::numeric_limits<int32_t>::min());
}

// std::unique_ptr for objects created with std::malloc() rather than new.
struct FreeDeleter {
  void operator()(void* ptr) const {
    std::free(ptr);
  }
};
template <typename T>
using unique_c_ptr = std::unique_ptr<T, FreeDeleter>;

} // namespace jit

template <typename D, typename S>
inline D safe_cast(const S& src) {
  static_assert(sizeof(S) == sizeof(D), "src and dst must be the same size");
  static_assert(
      std::is_scalar_v<D> && std::is_scalar_v<S>,
      "both src and dst must be of scalar type.");
  D dst;
  std::memcpy(&dst, &src, sizeof(dst));
  return dst;
}

#endif

// this is for non-test builds. define FRIEND_TEST here so we don't
// have to include the googletest header in our headers to be tested.
#ifndef FRIEND_TEST
#define FRIEND_TEST(test_case_name, test_name)
#endif

#endif
