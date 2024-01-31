// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cinderx/StrictModules/scope.h"
namespace strictmod {
std::string mangle(const std::string& className, const std::string& name) {
  // private names need to start with __
  if (name.size() < 2 || name[0] != '_' || name[1] != '_') {
    return name;
  }
  auto nameLen = name.size();
  if (name[nameLen - 1] == '_' && name[nameLen - 2] == '_') {
    return name;
  }
  if (name.find('.') != std::string::npos) {
    return name;
  }
  auto classStartPos = className.find_first_not_of('_');
  if (classStartPos == std::string::npos) {
    return name;
  }
  // The maximum size of the end result is size(_<className><name>)
  std::string result;
  result.reserve(1 + className.size() + name.size());
  result.append("_");
  result.append(className, classStartPos);
  result.append(name);
  return result;
}
} // namespace strictmod
