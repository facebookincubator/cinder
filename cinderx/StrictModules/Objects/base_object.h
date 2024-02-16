// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/Common/ref.h"
#include "cinderx/StrictModules/caller_context.h"
#include "cinderx/StrictModules/py_headers.h"
#include "cinderx/StrictModules/rewriter_attributes.h"
#include "cinderx/StrictModules/sequence_map.h"

#include <fmt/format.h>

#include <list>
#include <memory>
#include <optional>
#include <vector>

namespace strictmod::objects {

class StrictType;
class StrictModuleObject;

class BaseStrictObject : public std::enable_shared_from_this<BaseStrictObject> {
 public:
  BaseStrictObject(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator)
      : type_(type), creator_(creator), rewriterAttrs_() {}

  BaseStrictObject(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator)
      : type_(type), creator_(creator), rewriterAttrs_() {}

  virtual ~BaseStrictObject() {}

  /* clear all content in __dict__ that's owned by owner. Use this during
   * shutdown */
  virtual void cleanContent(const StrictModuleObject*) {}

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) = 0;
  virtual std::string getDisplayName() const = 0;
  virtual bool isHashable() const;
  virtual size_t hash() const;
  virtual bool eq(const BaseStrictObject& other) const;

  virtual bool isUnknown() const {
    return false;
  }

  virtual bool isLazy() const {
    return false;
  }

  /** get equivalent python object of this object
   * return nullptr if conversion is not supported
   *
   * Returns new reference to the PyObject
   */
  virtual Ref<> getPyObject() const {
    return nullptr;
  };

  std::shared_ptr<StrictType> getType() {
    return type_;
  }
  const std::shared_ptr<const StrictType> getType() const {
    return type_;
  }
  const StrictType& getTypeRef() const {
    return *type_;
  }
  StrictType& getTypeRef() {
    return *type_;
  }
  void setType(std::shared_ptr<StrictType> type) {
    type_ = std::move(type);
  }

  const std::weak_ptr<const StrictModuleObject> getCreator() const {
    return creator_;
  }
  void setCreator(std::shared_ptr<StrictModuleObject> creator) {
    creator_ = std::move(creator);
  }
  void setCreator(std::weak_ptr<StrictModuleObject> creator) {
    creator_ = std::move(creator);
  }

  void initRewriterAttrs() {
    rewriterAttrs_ = std::make_unique<RewriterAttrs>();
  }

  RewriterAttrs& ensureRewriterAttrs() {
    if (rewriterAttrs_ == nullptr) {
      initRewriterAttrs();
    }
    return *rewriterAttrs_;
  }

  RewriterAttrs& getRewriterAttrs() {
    return *rewriterAttrs_;
  }

  const RewriterAttrs& getRewriterAttrs() const {
    return *rewriterAttrs_;
  }

  bool hasRewritterAttrs() const {
    return rewriterAttrs_ != nullptr;
  }

 protected:
  std::shared_ptr<StrictType> type_;
  std::weak_ptr<StrictModuleObject> creator_;
  std::unique_ptr<RewriterAttrs> rewriterAttrs_;
};

typedef sequence_map<std::string, std::shared_ptr<BaseStrictObject>> DictType;
typedef std::unordered_map<void*, std::shared_ptr<BaseStrictObject>>
    astToResultT;

// format arguments for function call
std::string formatArgs(
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames);

static const std::vector<std::shared_ptr<BaseStrictObject>> kEmptyArgs{};
static const std::vector<std::string> kEmptyArgNames{};

/* check whether `modified` has a owner different from caller
 * and record an error if it does.
 */
void checkExternalModification(
    std::shared_ptr<BaseStrictObject> modified,
    const CallerContext& caller);

size_t hash(const std::shared_ptr<BaseStrictObject>& obj);

struct StrictObjectEqual {
  bool operator()(
      const std::shared_ptr<BaseStrictObject>& lhs,
      const std::shared_ptr<BaseStrictObject>& rhs) const;
};

struct StrictObjectHasher {
  size_t operator()(const std::shared_ptr<BaseStrictObject>& obj) const;
};

} // namespace strictmod::objects

// fmt specialization for BaseStrictObject
template <>
struct fmt::formatter<std::shared_ptr<strictmod::objects::BaseStrictObject>>
    : formatter<std::string> {
  template <typename FormatContext>
  auto format(
      std::shared_ptr<strictmod::objects::BaseStrictObject> c,
      FormatContext& ctx) const {
    std::string name = c->getDisplayName();
    return formatter<std::string>::format(name, ctx);
  }
};
