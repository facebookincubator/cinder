// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef JIT_HIR_TYPE_H
#define JIT_HIR_TYPE_H

#include "Python.h"
#include "frameobject.h"

#include "Jit/log.h"
#include "Jit/util.h"

#include <cstddef>
#include <cstdint>
#include <ostream>

// This file defines jit::hir::Type, which represents types of objects in HIR,
// both Python objects and primitive C types (some of which are exposed to
// Python code in Static Python). For a high-level overview, see
// Jit/hir_type.md.

namespace jit {
namespace hir {

// These macros describe the predefined types supported by Type, which we use
// to generate various bits of code and data.

//////////////////////////////////////////////////////////////////////////////
// Basic Types
//
// First, define the basic types, which each get one bit in the bitset. Every
// basic type is a predefined type, but many predefined types are composed of
// more than one basic type.

// Built-in types that can be subclassed by user types. This is a three-level X
// macro: T1 and T2 are transform macros that derive one or more related names
// from the base type name. T1 calls T2 with X and each derived name, and T2
// calls X with further derived names. This composability is used to support
// optional types (TOptFoo == TFoo | TNullptr).
//
// Long is defined down in HIR_UNION_TYPES rather than here in HIR_BASE_TYPES
// because it has another predefined type as a subtype (Bool). It's possible to
// encode that pattern by adding another macro, but that's not worth the
// complexity as long as it only applies to one type.
#define HIR_BASE_TYPES(T1, T2, X) \
  T1(T2, X, Array)                \
  T1(T2, X, BaseException)        \
  T1(T2, X, Bytes)                \
  T1(T2, X, Dict)                 \
  T1(T2, X, Float)                \
  T1(T2, X, List)                 \
  T1(T2, X, Set)                  \
  T1(T2, X, Tuple)                \
  T1(T2, X, Type)                 \
  T1(T2, X, Unicode)

// Meant to be used as the T1 argument to the *BASE_TYPES() macros, these
// expand each name to its *Exact, *User, or both variants.
#define HIR_BASE_TYPE_EXACT(T, X, name) T(X, name##Exact)
#define HIR_BASE_TYPE_USER(T, X, name) T(X, name##User)
#define HIR_BASE_TYPE(T, X, name) \
  HIR_BASE_TYPE_EXACT(T, X, name) HIR_BASE_TYPE_USER(T, X, name)

// *Exact and *User basic types that don't fit the standard base type pattern.
#define HIR_BASIC_EXACT_TYPES(T, X) \
  T(X, LongExact)                   \
  T(X, ObjectExact)

#define HIR_BASIC_USER_TYPES(T, X) \
  T(X, LongUser)                   \
  T(X, ObjectUser)

// Built-in types that can't be subclassed.
#define HIR_BASIC_FINAL_TYPES(T, X) \
  T(X, Bool)                        \
  T(X, Cell)                        \
  T(X, Code)                        \
  T(X, Frame)                       \
  T(X, Func)                        \
  T(X, Gen)                         \
  T(X, NoneType)                    \
  T(X, Slice)                       \
  T(X, WaitHandle)

// All basic Python types.
#define HIR_BASIC_PYTYPES(T, X) \
  HIR_BASIC_EXACT_TYPES(T, X)   \
  HIR_BASIC_USER_TYPES(T, X)    \
  HIR_BASIC_FINAL_TYPES(T, X)   \
  HIR_BASE_TYPES(HIR_BASE_TYPE, T, X)

// Basic types; not visible to Python unless Static Python.
#define HIR_BASIC_PRIMITIVE_TYPES(X) \
  X(CBool)                           \
  X(CInt8)                           \
  X(CInt16)                          \
  X(CInt32)                          \
  X(CInt64)                          \
  X(CUInt8)                          \
  X(CUInt16)                         \
  X(CUInt32)                         \
  X(CUInt64)                         \
  X(CPtr)                            \
  X(CDouble)                         \
  X(Nullptr)

// Call X with all arguments. Used as the transform macro in situations where
// no derived names are needed.
#define HIR_NOP(X, ...) X(__VA_ARGS__)

// All basic types, Python and primitive.
#define HIR_BASIC_TYPES(X) \
  HIR_BASIC_PYTYPES(HIR_NOP, X) HIR_BASIC_PRIMITIVE_TYPES(X)

//////////////////////////////////////////////////////////////////////////////
// Union types
//
// Next, define built-in union types in terms of the basic types.

// Helper to create unions for each member of an X macro.
#define HIR_OR_BITS(name) | k##name

// Create a union type for standard built-in base types.
#define HIR_BASE_TYPE_UNION(T, X, name) \
  T(X, name, k##name##Exact | k##name##User)

// Call X to create both name and Optname.
#define HIR_OPT_UNION(X, name, bits) \
  X(name, bits) X(Opt##name, (bits) | kNullptr)

// Define the union types: a number of special types like Top, Bottom,
// BuiltinExact, Primitive, unions for each base type combining the *Exact and
// *User types as appropriate, and optional variants of Object subtype unions.
#define HIR_UNION_TYPES(X)                                                    \
  X(Top, 0 HIR_BASIC_TYPES(HIR_OR_BITS))                                      \
  X(Bottom, 0)                                                                \
  X(Primitive, 0 HIR_BASIC_PRIMITIVE_TYPES(HIR_OR_BITS))                      \
  X(CUnsigned, kCUInt8 | kCUInt16 | kCUInt32 | kCUInt64)                      \
  X(CSigned, kCInt8 | kCInt16 | kCInt32 | kCInt64)                            \
  HIR_OPT_UNION(                                                              \
      X,                                                                      \
      BuiltinExact,                                                           \
      0 HIR_BASE_TYPES(HIR_BASE_TYPE_EXACT, HIR_NOP, HIR_OR_BITS)             \
          HIR_BASIC_EXACT_TYPES(HIR_NOP, HIR_OR_BITS)                         \
              HIR_BASIC_FINAL_TYPES(HIR_NOP, HIR_OR_BITS))                    \
  HIR_BASE_TYPES(HIR_BASE_TYPE_UNION, HIR_OPT_UNION, X)                       \
  HIR_OPT_UNION(X, Long, kLongExact | kBool | kLongUser)                      \
  HIR_OPT_UNION(                                                              \
      X,                                                                      \
      User,                                                                   \
      kObjectUser |                                                           \
          kLongUser HIR_BASE_TYPES(HIR_BASE_TYPE_USER, HIR_NOP, HIR_OR_BITS)) \
  HIR_OPT_UNION(X, Object, 0 HIR_BASIC_PYTYPES(HIR_NOP, HIR_OR_BITS))

// Transform macro used to create optional versions of each basic pytype.
#define HIR_OPT_BASIC_TYPE(X, name) X(Opt##name, k##name | kNullptr)

//////////////////////////////////////////////////////////////////////////////
// All HIR Types
//
// BASIC(name) will be called for basic types.
// UNION(name, bits) will be called for union types.
#define HIR_TYPES(BASIC, UNION)                \
  HIR_BASIC_TYPES(BASIC)                       \
  HIR_BASIC_PYTYPES(HIR_OPT_BASIC_TYPE, UNION) \
  HIR_UNION_TYPES(UNION)

class Type {
  // Assign each basic type its index in the bitset. These values are only
  // needed to construct the predefined kFoo bitsets a few lines down, and are
  // private because they're an implementation detail of those kFoo values.
  enum BitIndexes {
#define TY(name) k##name##Bit,
    HIR_BASIC_TYPES(TY)
#undef TY
  };
#define TY(name) +1
  static constexpr int kNumBits = HIR_BASIC_TYPES(TY);
#undef TY

 public:
  using bits_t = uint64_t;

  // Construct a bits_t for all predefined types. The union types are defined
  // in terms of the basic types and other unions, so the order of entries in
  // HIR_UNION_TYPES() above is significant.
#define BASIC_TY(name) static constexpr bits_t k##name = 1UL << k##name##Bit;
#define UNION_TY(name, bits) static constexpr bits_t k##name = (bits);
  HIR_TYPES(BASIC_TY, UNION_TY)
#undef BASIC_TY
#undef UNION_TY

  // Create a Type with the given bits. This isn't intended for general
  // consumption and is only public for the TFoo predefined Types (created near
  // the bottom of this file).
  explicit constexpr Type(bits_t bits) : Type{bits, kSpecTop, 0} {}

  std::size_t hash() const;
  std::string toString() const;

  // Parse a Type from the given string. Unions and PyObject* specializations
  // are not supported. Returns TBottom on error.
  static Type parse(std::string str);

  // Create a Type from a PyTypeObject, optionally flagged as not allowing
  // subtypes. The resulting Type is not guaranteed to be specialized (for
  // example, fromType(&PyLong_Type) == TLong).
  static Type fromType(PyTypeObject* type);
  static Type fromTypeExact(PyTypeObject* type);

  // Create a Type from a PyObject. The resulting Type is not guaranteed to be
  // specialized (for example, fromObject(Py_None) == TNoneType).
  static Type fromObject(PyObject* obj);

  // Create a Type specialized with a C value.
  static Type fromCBool(bool b);
  static Type fromCDouble(double d);

  static Type fromCInt(int64_t i, Type t);
  static Type fromCUInt(uint64_t i, Type t);

  // Return the PyTypeObject* that uniquely represents this type, or nullptr if
  // there isn't one. The PyTypeObject* may be from a type
  // specialization. "Uniquely" here means that there should be no loss of
  // information in the Type -> PyTypeObject* conversion, other than whether or
  // not this Type is exact.
  //
  // Some examples:
  // TLong.uniquePyType() == &PyLong_Type
  // TLongExact.uniquePyType() == &PyLong_Type
  // TLongUser.uniquePyType() == nullptr
  // TLongExact[123].uniquePyType() == nullptr
  // TBool.uniquePyType() == &PyBool_Type
  // TObject.uniquePyType() == &PyBaseObject_Type
  // (TObject - TLong).uniquePyType() == nullptr
  PyTypeObject* uniquePyType() const;

  // Return the PyObject* that this type represents, or nullptr if it
  // represents more than one object (or a non-object type). This is similar to
  // objectSpec() (but with support for NoneType) and is the inverse of
  // fromObject(): Type::fromObject(obj).asObject() == obj.
  PyObject* asObject() const;

  // Does this Type represent a single value?
  bool isSingleValue() const;

  // Does this Type have a type specialization, including from an object
  // specialization?
  bool hasTypeSpec() const;

  // Does this Type have an exact type specialization, including from an object
  // specialization?
  bool hasTypeExactSpec() const;

  // Does this Type have an object specialization?
  bool hasObjectSpec() const;

  // Does this Type have a primitive specialization?
  bool hasIntSpec() const;
  bool hasDoubleSpec() const;

  // Does this Type have an object or primitive specialization, and is it a
  // subtype of the given Type?
  bool hasValueSpec(Type ty) const;

  // If this Type has a type specialization, return it. If this Type has an
  // object specialization, return its type.
  PyTypeObject* typeSpec() const;

  // Return this Type's object specialization.
  PyObject* objectSpec() const;

  // Return this Type's primitive specialization.
  intptr_t intSpec() const;
  double_t doubleSpec() const;

  // Return a copy of this Type with its specialization removed.
  Type unspecialized() const;

  // Return true iff this Type is specialized with an exact PyTypeObject* or is
  // a subtype of all builtin exact types.
  bool isExact() const;

  // Equality.
  bool operator==(Type other) const;
  bool operator!=(Type other) const;

  // Strict and non-strict subtype checking.
  bool operator<(Type other) const;
  bool operator<=(Type other) const;

  // Shortcut for (*this & other) != TBottom.
  bool couldBe(Type other) const;

  // Set operations: union, intersection, and subtraction.
  Type operator|(Type other) const;
  Type operator&(Type other) const;
  Type operator-(Type other) const;

  Type& operator|=(Type other);
  Type& operator&=(Type other);
  Type& operator-=(Type other);

 private:
  // Validity and kind of specialization. Note that this is a regular enum
  // rather than a bitset, so the bit values of each kind aren't important.
  enum SpecKind : bits_t {
    // No specialization: the Top type in the specialization lattice, and a
    // supertype of all specializations. See Type::specSubtype() for details on
    // subtype relationships between the other kinds.
    kSpecTop,

    // Type specialization: pytype_ is valid.
    kSpecType,

    // Exact type specialization: pytype_ is valid and its subtypes are
    // excluded.
    kSpecTypeExact,

    // Object specialization: pyobject_ is valid.
    kSpecObject,

    // Integral specialization: int_ is valid
    kSpecInt,

    // Double specialization: double_ is valid
    kSpecDouble,
  };

  // Constructors used to create specialized Types.
  constexpr Type(bits_t bits, PyTypeObject* type_spec, bool exact)
      : Type{
            bits,
            exact ? kSpecTypeExact : kSpecType,
            reinterpret_cast<intptr_t>(type_spec)} {}
  constexpr Type(bits_t bits, PyObject* value_spec)
      : Type{bits, kSpecObject, reinterpret_cast<intptr_t>(value_spec)} {}
  constexpr Type(bits_t bits, double_t spec)
      : bits_{bits}, spec_kind_{kSpecDouble}, padding_{}, double_{spec} {}

  constexpr Type(bits_t bits, SpecKind spec_kind, intptr_t spec)
      : bits_{bits}, spec_kind_{spec_kind}, padding_{}, int_{spec} {
    JIT_DCHECK(
        bits != kBottom || (spec_kind == kSpecTop && spec == 0),
        "Bottom can't be specialized");
    JIT_DCHECK(padding_ == 0, "Invalid padding");
  }

  // What is this Type's specialization kind?
  SpecKind specKind() const;

  // Shorthand for specKind() != kSpecTop: does this Type have a non-Top
  // specialization?
  bool hasSpec() const;

  // String representation of this Type's specialization, which must not be
  // kSpecTop.
  std::string specString() const;

  // Is this Type's specialization a subtype of the other Type's
  // specialization?
  bool specSubtype(Type other) const;

  static Type fromTypeImpl(PyTypeObject* type, bool exact);

  // Bit field sizes, computed to fill any padding with zeros to make comparing
  // cheaper.
  static constexpr int kSpecBits = 3;
  static constexpr int kPaddingBits =
      int{sizeof(bits_t) * CHAR_BIT} - kNumBits - kSpecBits;
  static_assert(
      kPaddingBits > 0,
      "Too many basic types and/or specialization kinds");

  bits_t bits_ : kNumBits;
  bits_t spec_kind_ : kSpecBits;
  bits_t padding_ : kPaddingBits;

  // Specialization. Active field determined by spec_kind_.
  union {
    PyTypeObject* pytype_;
    PyObject* pyobject_;
    intptr_t int_;
    double_t double_;
  };
};

inline std::ostream& operator<<(std::ostream& os, const Type& ty) {
  return os << ty.toString();
}

// Define TFoo constants for all built-in types.
#define TY(name, ...) constexpr Type T##name{Type::k##name};
HIR_TYPES(TY, TY)
#undef TY

} // namespace hir
} // namespace jit

template <>
struct std::hash<jit::hir::Type> {
  std::size_t operator()(const jit::hir::Type& ty) const {
    return ty.hash();
  }
};

#define incl_JIT_HIR_TYPE_INL_H
#include "Jit/hir/type_inl.h"
#undef incl_JIT_HIR_TYPE_INL_H

#endif
