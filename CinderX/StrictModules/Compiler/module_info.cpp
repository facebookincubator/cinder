// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Compiler/module_info.h"

#include "StrictModules/Compiler/abstract_module_loader.h"
namespace strictmod::compiler {

inline bool endsWith(const std::string& name, const std::string& suffix) {
  if (name.size() < suffix.size()) {
    return false;
  }
  auto cmp =
      name.compare(name.length() - suffix.length(), suffix.length(), suffix);
  return cmp == 0;
}

StubKind StubKind::getStubKind(const std::string& filename, bool isAllowList) {
  int kind = Ci_STUB_KIND_MASK_NONE;
  if (endsWith(
          filename, getFileSuffixKindName(FileSuffixKind::kStrictStubFile))) {
    return StubKind(Ci_STUB_KIND_MASK_STRICT);
  } else if (endsWith(
                 filename,
                 getFileSuffixKindName(FileSuffixKind::kTypingStubFile))) {
    kind |= Ci_STUB_KIND_MASK_TYPING;
  }
  if (isAllowList) {
    kind |= Ci_STUB_KIND_MASK_ALLOWLIST;
  }
  return StubKind(kind);
}
} // namespace strictmod::compiler
