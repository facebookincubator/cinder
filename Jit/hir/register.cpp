// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/register.h"

#include "Jit/log.h"

#include <fmt/format.h>

#include <ostream>

namespace jit::hir {

const std::string& Register::name() const {
  if (name_.empty()) {
    name_ = fmt::format("v{}", id_);
  }
  return name_;
}

std::ostream& operator<<(std::ostream& os, RefKind kind) {
  switch (kind) {
    case RefKind::kUncounted:
      return os << "Uncounted";
    case RefKind::kBorrowed:
      return os << "Borrowed";
    case RefKind::kOwned:
      return os << "Owned";
  }
  JIT_CHECK(false, "Bad RefKind %d", static_cast<int>(kind));
}

std::ostream& operator<<(std::ostream& os, ValueKind kind) {
  switch (kind) {
    case ValueKind::kObject:
      return os << "Object";
    case ValueKind::kSigned:
      return os << "Signed";
    case ValueKind::kUnsigned:
      return os << "Unsigned";
    case ValueKind::kBool:
      return os << "Bool";
    case ValueKind::kDouble:
      return os << "Double";
  }
  JIT_CHECK(false, "Bad ValueKind %d", static_cast<int>(kind));
}

} // namespace jit::hir
