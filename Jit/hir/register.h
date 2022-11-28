// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/hir/type.h"

#include <iosfwd>
#include <string>

namespace jit::hir {

class Instr;

// HIR operates on an infinite number of virtual registers, which are
// represented by the Register class. After SSAify has run on a Function, its
// Registers represent SSA values, and their Types should be kept up-to-date and
// trusted.
class Register {
 public:
  explicit Register(int i) : id_(i) {}

  // An integer identifier for this register. This is unique per `Function`.
  int id() const {
    return id_;
  }

  // The type of this value. Only meaningful for SSA-form HIR.
  Type type() const {
    return type_;
  }
  void set_type(Type type) {
    type_ = type;
  }

  // Shorthand for checking the type of this Register.
  bool isA(Type type) const {
    return type_ <= type;
  }

  // The instruction that defined this value. Always set, but only meaningful
  // for SSA-form HIR.
  Instr* instr() const {
    return instr_;
  }
  void set_instr(Instr* instr) {
    instr_ = instr;
  }

  // A unique name for this value. This name has no connection to the original
  // Python program.
  const std::string& name() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(Register);

  Type type_{TTop};
  Instr* instr_{nullptr};
  int id_{-1};
  mutable std::string name_;
};

std::ostream& operator<<(std::ostream& os, const Register& reg);

// The refcount semantics of a value held in a Register.
enum class RefKind : char {
  // A PyObject* that is either null or points to an immortal object, and
  // doesn't need to be reference counted, or a primitive.
  kUncounted,
  // A PyObject* with a borrowed reference.
  kBorrowed,
  // A PyObject* that owns a reference.
  kOwned,
};
std::ostream& operator<<(std::ostream& os, RefKind kind);

// The kind of value held in a Register.
enum class ValueKind : char {
  // A PyObject*.
  kObject,
  // A signed 64-bit integer.
  kSigned,
  // An unsigned 64-bit integer.
  kUnsigned,
  // A C bool.
  kBool,
  // A C Double
  kDouble,
};
std::ostream& operator<<(std::ostream& os, ValueKind kind);

struct RegState {
  RegState() = default;
  RegState(Register* reg, RefKind ref_kind, ValueKind value_kind)
      : reg{reg}, ref_kind{ref_kind}, value_kind{value_kind} {}

  bool operator==(const RegState& other) const {
    return (reg == other.reg) && (ref_kind == other.ref_kind) &&
        (value_kind == other.value_kind);
  }

  Register* reg{nullptr};
  RefKind ref_kind{RefKind::kUncounted};
  ValueKind value_kind{ValueKind::kObject};
};

} // namespace jit::hir
