// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/type.h"

#include "Jit/log.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace jit {
namespace hir {

static_assert(sizeof(Type) == 16, "Type should fit in two registers");
static_assert(sizeof(intptr_t) == sizeof(int64_t), "Expected 64-bit pointers");

#define TY(name, ...) constexpr Type::bits_t Type::k##name;
HIR_TYPES(TY, TY)
#undef TY

namespace {

// For Types where it makes sense, map them to their corresponding
// PyTypeObject*.
const std::unordered_map<Type, PyTypeObject*>& typeToPyType() {
  static auto const map = [] {
    const std::unordered_map<Type, PyTypeObject*> map{
        {TObject, &PyBaseObject_Type},
        {TArray, &PyArray_Type},
        {TBool, &PyBool_Type},
        {TBytes, &PyBytes_Type},
        {TCell, &PyCell_Type},
        {TCode, &PyCode_Type},
        {TDict, &PyDict_Type},
        {TBaseException, reinterpret_cast<PyTypeObject*>(PyExc_BaseException)},
        {TFloat, &PyFloat_Type},
        {TFrame, &PyFrame_Type},
        {TFunc, &PyFunction_Type},
        {TGen, &PyGen_Type},
        {TList, &PyList_Type},
        {TLong, &PyLong_Type},
        {TSet, &PySet_Type},
        {TSlice, &PySlice_Type},
        {TTuple, &PyTuple_Type},
        {TType, &PyType_Type},
        {TUnicode, &PyUnicode_Type},
        {TWaitHandle, &PyWaitHandle_Type},
        {TNoneType, &_PyNone_Type},
    };

    // After construction, verify that all builtin base and final types have an
    // entry in this table.
#define CHECK_TY(name)                           \
  JIT_CHECK(                                     \
      map.count(T##name) == 1,                   \
      "Type %s missing entry in typeToPyType()", \
      T##name);
// HIR_NOP2 is an awful workaround for the lack of recursive macros in C:
// HIR_BASE_TYPES needs 2 transform macros and HIR_NOP can't expand itself.
#define HIR_NOP2(X, ...) X(__VA_ARGS__)
    HIR_BASE_TYPES(HIR_NOP, HIR_NOP2, CHECK_TY)
#undef HIR_NOP2
    HIR_BASIC_FINAL_TYPES(HIR_NOP, CHECK_TY)
    CHECK_TY(Long)
    CHECK_TY(Object)
#undef CHECK_TY

    return map;
  }();

  return map;
}

// Like typeToPyType(), but including Exact types in the key set (e.g., mapping
// TListExact -> PyList_Type).
const std::unordered_map<Type, PyTypeObject*>& typeToPyTypeWithExact() {
  static auto const map = [] {
    auto map = typeToPyType();
    for (auto& pair : typeToPyType()) {
      if (pair.first == TObject) {
        map.emplace(TObjectExact, &PyBaseObject_Type);
      } else if (pair.first == TLong) {
        map.emplace(TLongExact, &PyLong_Type);
      } else {
        map.emplace(pair.first & TBuiltinExact, pair.second);
      }
    }
    return map;
  }();

  return map;
}

// The inverse of typeToPyType().
const std::unordered_map<PyTypeObject*, Type>& pyTypeToType() {
  static auto const map = [] {
    std::unordered_map<PyTypeObject*, Type> map;
    for (auto& pair : typeToPyType()) {
      bool inserted = map.emplace(pair.second, pair.first).second;
      JIT_CHECK(inserted, "Duplicate key type: %s", pair.second->tp_name);
    }
    return map;
  }();

  return map;
}

// Like pyTypeToType(), but for Type::fromTypeExact(). It wants only the
// components of a type that can represent an exact type: the builtin exact
// type, or user-defined subtypes for exact specialization. These can be
// selected for most types by intersecting with TBuiltinExact or TUser,
// respectively.
//
// The only exceptions that we have to adjust for in this map are predefined
// Types that have other predefined Types as subtypes: TObject (where we leave
// out all other types) and TLong (where we leave out TBool).
const std::unordered_map<PyTypeObject*, Type>& pyTypeToTypeForExact() {
  static auto const map = [] {
    auto map = pyTypeToType();
    map.at(&PyBaseObject_Type) = TObjectExact | TObjectUser;
    map.at(&PyLong_Type) = TLongExact | TLongUser;
    return map;
  }();

  return map;
}

static std::string
truncatedStr(const char* data, std::size_t size, char delim) {
  const Py_ssize_t kMaxStrChars = 20;
  if (size <= kMaxStrChars) {
    return fmt::format("{}{}{}", delim, fmt::string_view{data, size}, delim);
  }
  return fmt::format(
      "{}{}{}...", delim, fmt::string_view{data, kMaxStrChars}, delim);
}

} // namespace

std::string Type::specString() const {
  if (hasIntSpec()) {
    if (*this <= TCBool) {
      return int_ ? "true" : "false";
    }
    JIT_DCHECK(
        *this <= TCInt8 || *this <= TCInt16 || *this <= TCInt32 ||
            *this <= TCInt64 || *this <= TCUInt8 || *this <= TCUInt16 ||
            *this <= TCUInt32 || *this <= TCUInt64,
        "Invalid specialization");
    return fmt::format("{}", int_);
  }

  if (hasDoubleSpec()) {
    return fmt::format("{}", double_);
  }

  if (!hasObjectSpec()) {
    if (hasTypeExactSpec()) {
      return fmt::format("{}:Exact", typeSpec()->tp_name);
    }
    return typeSpec()->tp_name;
  }

  if (*this <= TUnicode) {
    Py_ssize_t size;
    auto utf8 = PyUnicode_AsUTF8AndSize(objectSpec(), &size);
    if (utf8 == nullptr) {
      PyErr_Clear();
      return "encoding error";
    }
    return truncatedStr(utf8, size, '"');
  }

  if (*this <= TType) {
    return fmt::format(
        "{}:obj", reinterpret_cast<PyTypeObject*>(objectSpec())->tp_name);
  }

  if (*this <= TBytes) {
    char* buffer;
    Py_ssize_t size;
    if (PyBytes_AsStringAndSize(objectSpec(), &buffer, &size) < 0) {
      PyErr_Clear();
      return "unknown error";
    }
    return truncatedStr(buffer, size, '\'');
  }

  if (*this <= TBool) {
    return objectSpec() == Py_True ? "True" : "False";
  }

  if (*this <= TLong) {
    int overflow = 0;
    auto value = PyLong_AsLongLongAndOverflow(objectSpec(), &overflow);
    if (value == -1) {
      if (overflow == -1) {
        return "underflow";
      }
      if (overflow == 1) {
        return "overflow";
      }
      if (PyErr_Occurred()) {
        PyErr_Clear();
        return "error";
      }
    }
    return fmt::format("{}", value);
  }

  if (*this <= TFloat) {
    auto value = PyFloat_AsDouble(objectSpec());
    if (value == -1.0 && PyErr_Occurred()) {
      return "error";
    }
    return fmt::format("{}", value);
  }

  if (*this <= TCode) {
    auto name = reinterpret_cast<PyCodeObject*>(objectSpec())->co_name;
    if (name != nullptr && PyUnicode_Check(name)) {
      return fmt::format("\"{}\"", PyUnicode_AsUTF8(name));
    }
  }

  // We want to avoid invoking arbitrary Python during compilation, so don't
  // call PyObject_Repr() or anything similar.
  return fmt::format(
      "{}:{}", typeSpec()->tp_name, getStablePointer(objectSpec()));
}

static auto typeToName() {
  std::unordered_map<Type::bits_t, std::string> map{
#define TY(name, ...) {Type::k##name, #name},
      HIR_TYPES(TY, TY)
#undef TY
  };
  return map;
}

static auto nameToType() {
  std::unordered_map<std::string, Type> map{
#define TY(name, ...) {#name, T##name},
      HIR_TYPES(TY, TY)
#undef TY
  };
  return map;
}

static auto makeSortedBits() {
  std::vector<std::pair<Type::bits_t, std::string>> vec{
#define TY(name, ...) {Type::k##name, #name},
      HIR_TYPES(TY, TY)
#undef TY
  };

  // Sort the vector so types with the most bits set show up first.
  auto pred = [](auto& a, auto& b) {
    return popcount(a.first) > popcount(b.first);
  };
  std::sort(vec.begin(), vec.end(), pred);
  JIT_CHECK(
      vec.back().first == Type::kBottom, "Bottom should be at end of vec");
  vec.pop_back();
  return vec;
}

std::string Type::toString() const {
  std::string base;

  static auto const type_names = typeToName();
  auto it = type_names.find(bits_);
  if (it != type_names.end()) {
    base = it->second;
  } else {
    // Search the list of predefined type names, starting with the ones
    // containing the most bits.
    static auto const sorted_bits = makeSortedBits();
    bits_t bits_left = bits_;
    std::vector<std::string> parts;
    for (auto& pair : sorted_bits) {
      auto bits = pair.first;
      if ((bits_left & bits) == bits) {
        parts.emplace_back(pair.second);
        bits_left ^= bits;
        if (bits_left == 0) {
          break;
        }
      }
    }
    JIT_CHECK(bits_left == 0, "Type contains invalid bits");

    // Always show the parts in alphabetical order, regardless of which has the
    // most bits.
    std::sort(parts.begin(), parts.end());
    base = "{";
    auto sep = "";
    for (auto& part : parts) {
      base += sep;
      sep = "|";
      base += part;
    }
    base += "}";
  }

  return hasSpec() ? fmt::format("{}[{}]", base, specString()) : base;
}

Type Type::parse(std::string str) {
  static auto const name_types = nameToType();

  std::string spec_string;
  auto open_bracket = str.find('[');
  if (open_bracket != std::string::npos) {
    auto close_bracket = str.find(']');
    auto spec_len = close_bracket - (open_bracket + 1);
    if (close_bracket == std::string::npos || spec_len < 1) {
      return TBottom;
    }
    spec_string = str.substr(open_bracket + 1, spec_len);
    str = str.substr(0, open_bracket);
  }

  auto it = name_types.find(str);
  if (it == name_types.end()) {
    return TBottom;
  }

  Type base = it->second;
  if (spec_string.empty()) {
    return base;
  }

  intptr_t spec_value;
  if (base <= TCInt8 || base <= TCInt16 || base <= TCInt32 || base <= TCInt64) {
    errno = 0;
    spec_value = strtoll(spec_string.data(), nullptr, 10);
  } else if (
      base <= TCUInt8 || base <= TCUInt16 || base <= TCUInt32 ||
      base <= TCUInt64) {
    errno = 0;
    spec_value = strtoull(spec_string.data(), nullptr, 10);
  } else {
    return TBottom;
  }

  if (errno != 0) {
    return TBottom;
  }
  return Type{base.bits_, SpecKind::kSpecInt, spec_value};
}

Type Type::fromTypeImpl(PyTypeObject* type, bool exact) {
  auto& type_map = exact ? pyTypeToTypeForExact() : pyTypeToType();

  auto it = type_map.find(type);
  if (it != type_map.end()) {
    return exact ? it->second & TBuiltinExact : it->second;
  }

  PyObject* mro = type->tp_mro;
  for (ssize_t i = 0; i < PyTuple_GET_SIZE(mro); ++i) {
    auto ty = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(mro, i));
    auto it = type_map.find(ty);
    if (it != type_map.end()) {
      auto bits = it->second.bits_;
      return Type{bits & kUser, type, exact};
    }
  }
  JIT_CHECK(
      false, "Type %s(%p) doesn't have object in its mro", type->tp_name, type);
}

Type Type::fromType(PyTypeObject* type) {
  return fromTypeImpl(type, false);
}

Type Type::fromTypeExact(PyTypeObject* type) {
  return fromTypeImpl(type, true);
}

Type Type::fromObject(PyObject* obj) {
  if (obj == Py_None) {
    // There's only one value of type NoneType, so we don't need the result to
    // be specialized.
    return TNoneType;
  }

  return Type{fromTypeExact(Py_TYPE(obj)).bits_, obj};
}

PyTypeObject* Type::uniquePyType() const {
  if (hasObjectSpec()) {
    return nullptr;
  }
  if (hasTypeSpec()) {
    return typeSpec();
  }
  auto& type_map = typeToPyTypeWithExact();
  auto it = type_map.find(*this);
  if (it != type_map.end()) {
    return it->second;
  }
  return nullptr;
}

PyObject* Type::asObject() const {
  if (*this <= TNoneType) {
    return Py_None;
  }
  if (hasObjectSpec()) {
    return objectSpec();
  }
  return nullptr;
}

bool Type::isSingleValue() const {
  return *this <= TNoneType || *this <= TNullptr || hasObjectSpec() ||
      hasIntSpec() || hasDoubleSpec();
}

bool Type::operator<=(Type other) const {
  return (bits_ & other.bits_) == bits_ && specSubtype(other);
}

bool Type::specSubtype(Type other) const {
  if (!other.hasSpec()) {
    // The Top specialization is a supertype of all specializations.
    return true;
  }
  if (!hasSpec()) {
    // The Top specialization is only a subtype of itself, which is covered by
    // the previous case.
    return false;
  }
  if ((hasIntSpec() || other.hasIntSpec()) ||
      (hasDoubleSpec() || other.hasDoubleSpec())) {
    // Primitive specializations don't support subtypes other than exact
    // equality.
    return *this == other;
  }

  // Check other's specialization type in decreasing order of specificity.
  if (other.hasObjectSpec()) {
    return hasObjectSpec() && objectSpec() == other.objectSpec();
  }
  if (other.hasTypeExactSpec()) {
    return hasTypeExactSpec() && typeSpec() == other.typeSpec();
  }
  return PyType_IsSubtype(typeSpec(), other.typeSpec());
}

Type Type::operator|(Type other) const {
  bits_t bits = bits_ | other.bits_;

  if (!hasSpec() || !other.hasSpec()) {
    // If either type isn't specialized, the result isn't specialized.
    return Type{bits};
  }
  if ((hasIntSpec() || other.hasIntSpec()) ||
      ((hasDoubleSpec() || other.hasDoubleSpec()))) {
    // Primitive specializations only survive unification when the types are
    // equal.
    return *this == other ? *this : Type{bits};
  }

  if (hasObjectSpec() && other.hasObjectSpec() &&
      objectSpec() == other.objectSpec()) {
    JIT_DCHECK(
        *this == other,
        "Types with identical object specializations aren't equal");
    return *this;
  }

  auto type_a = typeSpec();
  auto type_b = other.typeSpec();
  if (hasTypeExactSpec() && other.hasTypeExactSpec() && type_a == type_b) {
    // We only return an exact specialization if we're unifying the same exact
    // type with itself.
    return Type{bits, type_a, true};
  }
  if (PyType_IsSubtype(type_a, type_b)) {
    return Type{bits, type_b, false};
  }
  if (PyType_IsSubtype(type_b, type_a)) {
    return Type{bits, type_a, false};
  }
  return Type{bits};
}

Type Type::operator&(Type other) const {
  bits_t bits = bits_ & other.bits_;
  if (bits == kBottom) {
    return TBottom;
  }
  if (specSubtype(other)) {
    return Type{bits, specKind(), int_};
  }
  if (other.specSubtype(*this)) {
    return Type{bits, other.specKind(), other.int_};
  }

  // Two different, non-exact type specializations can still have a non-empty
  // intersection thanks to multiple inheritance. We can't represent the
  // intersection of two arbitrary classes, and we want to avoid returning a
  // type that's wider than either input type.
  //
  // Returning either the lhs or rhs would be correct within our constraints,
  // so keep this operation commutative by returning the type with the name
  // that's alphabetically first. Fall back to pointer comparison if they have
  // the same name.
  if (specKind() == kSpecType && other.specKind() == kSpecType) {
    auto type_a = typeSpec();
    auto type_b = other.typeSpec();
    auto cmp = std::strcmp(type_a->tp_name, type_b->tp_name);
    if (cmp < 0 || (cmp == 0 && type_a < type_b)) {
      return Type{bits, type_a, false};
    }
    return Type{bits, type_b, false};
  }

  return TBottom;
}

Type Type::operator-(Type other) const {
  if (*this <= other) {
    return TBottom;
  }

  // We only want to remove parts of *this that are subsumed by other, which
  // can't happen unless our specialization is a subtype of other's.
  if (!specSubtype(other)) {
    return *this;
  }
  bits_t bits = bits_ & ~other.bits_;
  return Type{bits, specKind(), int_};
}

} // namespace hir
} // namespace jit
