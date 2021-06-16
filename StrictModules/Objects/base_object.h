// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_BASE_OBJ_H__
#define __STRICTM_BASE_OBJ_H__

#include "StrictModules/py_headers.h"

#include "Jit/ref.h"
#include "StrictModules/caller_context.h"

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

namespace strictmod::objects {

class StrictType;
class StrictModuleObject;

class BaseStrictObject : public std::enable_shared_from_this<BaseStrictObject> {
 public:
  BaseStrictObject(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator)
      : type_(type), creator_(creator) {}

  BaseStrictObject(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator)
      : type_(type), creator_(creator) {}

  virtual ~BaseStrictObject() {}

  /* clear all content in __dict__ that's owned by owner. Use this during
   * shutdown */
  virtual void cleanContent(const StrictModuleObject*) {}

  virtual std::unique_ptr<BaseStrictObject> copy() const = 0;
  virtual std::string getDisplayName() const = 0;
  virtual bool isHashable() const;
  virtual size_t hash() const;
  virtual bool eq(const BaseStrictObject& other) const;

  virtual bool isUnknown() const {return false;}

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

 protected:
  std::shared_ptr<StrictType> type_;
  std::weak_ptr<StrictModuleObject> creator_;
};

typedef std::unordered_map<std::string, std::shared_ptr<BaseStrictObject>>
    DictType;

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
      FormatContext& ctx) {
    std::string name = c->getDisplayName();
    return formatter<std::string>::format(name, ctx);
  }
};
#endif // !__STRICTM_BASE_OBJ_H__
