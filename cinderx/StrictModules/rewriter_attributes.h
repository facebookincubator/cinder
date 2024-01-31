// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <string>
#include <vector>

enum class CachedPropertyKind { kNone, kCachedAsync, kCached };

class RewriterAttrs {
 public:
  // default constructor
  RewriterAttrs()
      : slotsDisabled_(true),
        looseSlots_(false),
        extraSlots_(),
        mutable_(false),
        hasCachedProperty_(false),
        CachedPropertyKind_(CachedPropertyKind::kNone) {}

  void setSlotsEnabled(bool enabled) {
    slotsDisabled_ = !enabled;
  }

  void setLooseSlots(bool enabled) {
    looseSlots_ = enabled;
  }

  void setExtraSlots(std::vector<std::string> extraSlots) {
    extraSlots_ = std::move(extraSlots);
  }

  void addExtraSlots(const std::string& attr) {
    extraSlots_.emplace_back(attr);
  }

  void setMutable(bool isMutable) {
    mutable_ = isMutable;
  }

  // whether function is decorated with cached_property
  void setHasCachedProp(bool hasCachedProp) {
    hasCachedProperty_ = hasCachedProp;
  }

  // whether a decorator is cached_property and what kind it is
  void setCachedPropKind(CachedPropertyKind kind) {
    CachedPropertyKind_ = kind;
  }

  bool isSlotDisabled() const {
    return slotsDisabled_;
  }

  bool isLooseSlots() const {
    return looseSlots_;
  }

  const std::vector<std::string>& getExtraSlots() const {
    return extraSlots_;
  }

  bool isMutable() const {
    return mutable_;
  }

  bool hasCachedProperty() const {
    return hasCachedProperty_;
  }

  CachedPropertyKind getCachedPropKind() const {
    return CachedPropertyKind_;
  }

 private:
  bool slotsDisabled_;
  bool looseSlots_;
  std::vector<std::string> extraSlots_;
  bool mutable_;
  // whether function is decorated with cached_property
  bool hasCachedProperty_;
  // this should be set on the decorator itself
  CachedPropertyKind CachedPropertyKind_;
};
