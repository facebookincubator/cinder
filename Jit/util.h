// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include <cstdint>
#include <limits>
#include <type_traits>

#ifdef __cplusplus
#include "Jit/log.h"

#include <charconv>
#include <cstdarg>
#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#define DISALLOW_COPY_AND_ASSIGN(klass) \
  klass(const klass&) = delete;         \
  klass& operator=(const klass&) = delete

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

const int kKiB = 1024;
const int kMiB = kKiB * kKiB;
const int kGiB = kKiB * kKiB * kKiB;

#if defined(__x86_64__) || defined(__aarch64__)
const int kPageSize = 4 * kKiB;
#else
#error Please define kPageSize for the current architecture
#endif

template <typename T>
constexpr bool isPowerOfTwo(T x) {
  return (x & (x - 1)) == 0;
}

template <typename T>
constexpr T roundDown(T x, size_t n) {
  JIT_DCHECK(isPowerOfTwo(n), "must be power of 2");
  return (x & -n);
}

template <typename T>
constexpr T roundUp(T x, size_t n) {
  return roundDown(x + n - 1, n);
}

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

template <class T>
std::optional<T> parseInt(std::string_view s) {
  T n = 0;
  auto result = std::from_chars(s.begin(), s.end(), n);
  if (result.ec == std::errc{}) {
    return n;
  }
  return std::nullopt;
}

std::string codeFullname(PyObject* module, PyCodeObject* code);
std::string funcFullname(PyFunctionObject* func);

// When possible, return the fully qualified name of the given type (including
// its module). Falls back to the type's bare name.
std::string typeFullname(PyTypeObject* type);

// Return the given PyUnicodeObject as a std::string, or "" if an error occurs.
std::string unicodeAsString(PyObject* str);

// Given a code object and an index into f_localsplus, compute which of
// code->co_varnames, code->cellvars, or code->freevars contains the name of
// the variable. Return that tuple and adjust idx as needed.
PyObject* getVarnameTuple(PyCodeObject* code, int* idx);

// Similar to getVarnameTuple, but return the name itself rather than the
// containing tuple.
PyObject* getVarname(PyCodeObject* code, int idx);

inline int popcount(unsigned i) {
  return __builtin_popcount(i);
}

inline int popcount(unsigned long i) {
  return __builtin_popcountl(i);
}

inline int popcount(unsigned long long i) {
  return __builtin_popcountll(i);
}

// Look up an item in the given map. Always abort if key doesn't exist.
template <typename M, typename K>
auto& map_get_strict(M& map, const K& key) {
  auto it = map.find(key);
  JIT_CHECK(it != map.end(), "Key not found in map");
  return it->second;
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
std::enable_if_t<std::is_integral_v<T>, bool> fitsInt32(T val) {
  int64_t v = val;
  return (
      v <= std::numeric_limits<int32_t>::max() &&
      v >= std::numeric_limits<int32_t>::min());
}

template <typename T>
std::enable_if_t<std::is_pointer_v<T>, bool> fitsInt32(T val) {
  return fitsInt32(reinterpret_cast<intptr_t>(val));
}

// std::unique_ptr for objects created with std::malloc() rather than new.
struct FreeDeleter {
  void operator()(void* ptr) const {
    std::free(ptr);
  }
};
template <typename T>
using unique_c_ptr = std::unique_ptr<T, FreeDeleter>;

// TODO(T132146975) The clangd we're using at Meta for LSP gets very upset if we
// try to use C++20 Concepts and fails to compile most further code. So we
// shoehorn -D__BUILT_VIA_COMPILE_COMMANDS_JSON into our compile_commands.json
// to avoid this. Not a great solution but at least gets most things working.
#ifdef __BUILT_VIA_COMPILE_COMMANDS_JSON

#define REQUIRES_CALLABLE(...)

#else // not defined __BUILT_VIA_COMPILE_COMMANDS_JSON
#include <concepts>

#define REQUIRES_CALLABLE(...) requires jit::Callable<__VA_ARGS__>
// Similar to std::invocable<F, Args...>, but also constrains the return type.
template <typename F, typename Ret, typename... Args>
concept Callable = requires(F f, Args&&... args) {
  { f(std::forward<Args>(args)...) } -> std::convertible_to<Ret>;
};

#endif

template <class T>
class ScopeExit {
 public:
  ScopeExit(T&& action) : lambda_(std::move(action)) {}
  ~ScopeExit() {
    lambda_();
  }

 private:
  T lambda_;
};

#define SCOPE_EXIT_INTERNAL2(lname, aname, ...) \
  auto lname = [&]() { __VA_ARGS__; };          \
  jit::ScopeExit<decltype(lname)> aname(std::move(lname));

#define SCOPE_EXIT_TOKENPASTE(x, y) SCOPE_EXIT_##x##y

#define SCOPE_EXIT_INTERNAL1(ctr, ...)       \
  SCOPE_EXIT_INTERNAL2(                      \
      SCOPE_EXIT_TOKENPASTE(func_, ctr),     \
      SCOPE_EXIT_TOKENPASTE(instance_, ctr), \
      __VA_ARGS__)

#define SCOPE_EXIT(...) SCOPE_EXIT_INTERNAL1(__COUNTER__, __VA_ARGS__)

// Simulate _PyType_Lookup(), but in a way that should avoid any heap mutations
// (caches, refcount operations, arbitrary code execution).
//
// Since this function is very conservative in the operations it will perform,
// it may return false negatives; a nullptr return does *not* mean that
// _PyType_Lookup() will also return nullptr. However, a non-nullptr return
// value should be the same value _PyType_Lookup() would return.
BorrowedRef<> typeLookupSafe(
    BorrowedRef<PyTypeObject> type,
    BorrowedRef<> name);

} // namespace jit

template <typename D, typename S>
inline constexpr D bit_cast(const S& src) {
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
