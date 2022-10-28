// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef incl_JIT_HIR_TYPE_INL_H
#error "hir_type_inl.h should only be included by hir_type.h"
#endif

#include "Jit/log.h"

#include <cstring>

namespace jit {
namespace hir {

inline std::size_t Type::hash() const {
  static_assert(sizeof(std::size_t) == sizeof(int_), "Unexpected size_t size");
  std::size_t i;
  std::memcpy(&i, this, sizeof(i));
  return combineHash(i, int_);
}

inline Type Type::fromCBool(bool b) {
  return Type{kCBool, kLifetimeBottom, kSpecInt, b};
}

inline Type Type::fromCDouble(double_t d) {
  return Type{kCDouble, d};
}

inline bool Type::CIntFitsType(int64_t i, Type t) {
  return t == TCInt64 || (t == TCInt32 && i >= INT32_MIN && i <= INT32_MAX) ||
      (t == TCInt16 && i >= INT64_MIN && i <= INT16_MAX) ||
      (i >= INT8_MIN && i <= INT8_MAX);
}

inline Type Type::fromCInt(int64_t i, Type t) {
  JIT_DCHECK(
      t == TCInt64 || t == TCInt32 || t == TCInt16 || t == TCInt8,
      "expected signed value");
  JIT_DCHECK(CIntFitsType(i, t), "int value out of range");
  return Type{t.bits_, kLifetimeBottom, kSpecInt, i};
}

inline Type Type::fromCPtr(void* p) {
  return Type{
      TCPtr.bits_, kLifetimeBottom, kSpecInt, reinterpret_cast<intptr_t>(p)};
}

inline bool Type::CUIntFitsType(uint64_t i, Type t) {
  return t == TCUInt64 || (t == TCUInt32 && i <= UINT32_MAX) ||
      (t == TCUInt16 && i <= UINT16_MAX) || i <= UINT8_MAX;
}

inline Type Type::fromCUInt(uint64_t i, Type t) {
  JIT_DCHECK(
      t == TCUInt64 || t == TCUInt32 || t == TCUInt16 || t == TCUInt8,
      "expected unsigned value");
  JIT_DCHECK(Type::CUIntFitsType(i, t), "int value out of range");
  return Type{t.bits_, kLifetimeBottom, kSpecInt, (intptr_t)i};
}

inline bool Type::hasTypeSpec() const {
  auto sk = specKind();
  return sk == kSpecType || sk == kSpecTypeExact || sk == kSpecObject;
}

inline bool Type::hasTypeExactSpec() const {
  return specKind() == kSpecTypeExact || specKind() == kSpecObject;
}

inline bool Type::hasObjectSpec() const {
  return specKind() == kSpecObject;
}

inline bool Type::hasIntSpec() const {
  return specKind() == kSpecInt;
}

inline bool Type::hasDoubleSpec() const {
  return specKind() == kSpecDouble;
}

inline bool Type::hasValueSpec(Type ty) const {
  return (hasObjectSpec() || hasIntSpec() || hasDoubleSpec()) && *this <= ty;
}

inline PyTypeObject* Type::typeSpec() const {
  JIT_DCHECK(hasTypeSpec(), "Type has no type specialization");
  return specKind() == kSpecObject ? Py_TYPE(pyobject_) : pytype_;
}

inline PyObject* Type::objectSpec() const {
  JIT_DCHECK(hasObjectSpec(), "Type has invalid value specialization");
  return pyobject_;
}

inline intptr_t Type::intSpec() const {
  JIT_DCHECK(hasIntSpec(), "Type has invalid value specialization");
  return int_;
}

inline double_t Type::doubleSpec() const {
  JIT_DCHECK(hasDoubleSpec(), "Type has invalid value specialization");
  return double_;
}

inline Type Type::unspecialized() const {
  return Type{bits_, lifetime_};
}

inline Type Type::dropMortality() const {
  if (lifetime_ == kLifetimeBottom) {
    return *this;
  }
  return Type{bits_, kLifetimeTop, specKind(), int_};
}

inline bool Type::hasSpec() const {
  return spec_kind_ != kSpecTop && spec_kind_ != kSpecBottom;
}

inline Type::SpecKind Type::specKind() const {
  return static_cast<SpecKind>(spec_kind_);
}

inline bool Type::isExact() const {
  return hasTypeExactSpec() || *this <= TBuiltinExact;
}

inline bool Type::couldBe(Type other) const {
  return (*this & other) != TBottom;
}

inline bool Type::operator==(Type other) const {
  return memcmp(this, &other, sizeof(*this)) == 0;
}

inline bool Type::operator!=(Type other) const {
  return !operator==(other);
}

inline bool Type::operator<(Type other) const {
  return *this != other && *this <= other;
}

inline Type& Type::operator|=(Type other) {
  return *this = *this | other;
}

inline Type& Type::operator&=(Type other) {
  return *this = *this & other;
}

inline Type& Type::operator-=(Type other) {
  return *this = *this - other;
}

} // namespace hir
} // namespace jit
