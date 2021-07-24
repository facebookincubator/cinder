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
  int kind = kNone;
  if (endsWith(
          filename, getFileSuffixKindName(FileSuffixKind::kStrictStubFile))) {
    return StubKind(kStrict);
  } else if (endsWith(
                 filename,
                 getFileSuffixKindName(FileSuffixKind::kTypingStubFile))) {
    kind |= kTyping;
  }
  if (isAllowList) {
    kind |= kAllowList;
  }
  return StubKind(kind);
}
} // namespace strictmod::compiler
