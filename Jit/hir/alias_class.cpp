// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/alias_class.h"

#include <fmt/ostream.h>

#include <ostream>
#include <unordered_map>

namespace jit::hir {

namespace {
auto makeAclsNames() {
  std::unordered_map<AliasClass, std::string> map{
#define X(name, ...) {A##name, #name},
      HIR_ACLS(X)
#undef X
  };
  return map;
}
} // namespace

std::string AliasClass::toString() const {
  static auto const names = makeAclsNames();
  auto it = names.find(*this);
  if (it != names.end()) {
    return it->second;
  }

  std::string result = "{";
  auto sep = "";
#define X(name)                         \
  if (bits_ & AliasClass::k##name) {    \
    format_to(result, "{}" #name, sep); \
    sep = "|";                          \
  }
  HIR_BASIC_ACLS(X)
#undef X
  return result + "}";
}

std::ostream& operator<<(std::ostream& os, const AliasClass& acls) {
  return os << acls.toString();
}

} // namespace jit::hir
