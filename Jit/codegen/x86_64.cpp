// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/codegen/x86_64.h"

#include <fmt/ostream.h>

#include <algorithm>
#include <utility>

namespace jit::codegen {

constexpr int PhyLocation::NUM_GP_REGS;
constexpr int PhyLocation::NUM_XMM_REGS;
constexpr int PhyLocation::NUM_REGS;
constexpr int PhyLocation::XMM_REG_BASE;

static const char* REG_NAMES[] = {
#define REG_STRING(r, ...) #r,
    FOREACH_GP(REG_STRING) FOREACH_XMM(REG_STRING)
#undef REG_STRING
};

std::ostream& operator<<(std::ostream& out, const PhyLocation& loc) {
  if (loc.is_register()) {
    out << REG_NAMES[loc];
  } else {
    out << "[RBP - " << -loc.loc << "]";
  }
  return out;
}

// Parse the string given in name to the physical location.
// Currently only parsing physical register names are supported.
PhyLocation PhyLocation::parse(const std::string& name) {
  auto iter = std::find(REG_NAMES, REG_NAMES + NUM_REGS, name);
  return std::distance(REG_NAMES, iter);
}

} // namespace jit::codegen
