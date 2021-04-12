#include "StrictModules/Objects/type.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/exceptions.h"

#include <functional>
#include <list>
#include <numeric>
#include <stdexcept>

namespace strictmod::objects {

StrictType::StrictType(
    std::string name,
    std::shared_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<StrictType> metatype,
    bool immutable)
    : StrictInstance(metatype, creator),
      name_(std::move(name)),
      moduleName_(creator ? creator->getModuleName() : ""),
      baseClasses_(std::move(bases)),
      immutable_(immutable),
      mro_() {}

bool StrictType::is_subtype(std::shared_ptr<StrictType> base) const {
  return std::find(baseClasses_.begin(), baseClasses_.end(), base) !=
      baseClasses_.end();
}

static std::list<std::shared_ptr<const BaseStrictObject>> _mroMerge(
    std::string name,
    std::list<std::list<std::shared_ptr<const BaseStrictObject>>> seqs) {
  std::list<std::shared_ptr<const BaseStrictObject>> result;

  while (true) {
    seqs.remove_if(
        [](const std::list<std::shared_ptr<const BaseStrictObject>>& a) {
          return a.empty();
        });

    if (seqs.empty()) {
      return result;
    }

    std::shared_ptr<const BaseStrictObject> cand = nullptr;

    for (auto& seq : seqs) {
      cand = seq.front();
      bool isHead = true;
      for (auto& s : seqs) {
        auto it = std::find(std::next(s.begin()), s.end(), cand);
        if (it != s.end()) {
          isHead = false;
          break;
        }
      }
      if (isHead) {
        break;
      } else {
        cand = nullptr;
      }
    }

    if (cand == nullptr) {
      throw std::runtime_error(
          "Failed to create a consistent MRO for class " + name);
    }

    for (auto& seq : seqs) {
      if (seq.front() == cand) {
        seq.pop_front();
      }
    }

    result.push_back(std::move(cand));
  }
}

static std::list<std::shared_ptr<const BaseStrictObject>> _mro(
    std::string className,
    std::shared_ptr<const BaseStrictObject> obj) {
  std::shared_ptr<const StrictType> type =
      std::dynamic_pointer_cast<const StrictType>(obj);

  if (!type) {
    return std::list<std::shared_ptr<const BaseStrictObject>>{obj};
  }

  auto allBases = type->getBaseClasses();
  std::list<std::list<std::shared_ptr<const BaseStrictObject>>> toMerge;

  std::list<std::shared_ptr<const BaseStrictObject>> selfList{obj};
  toMerge.push_back(selfList);

  for (auto& base : allBases) {
    toMerge.push_back(_mro(className, base));
  }

  std::list<std::shared_ptr<const BaseStrictObject>> baseList(
      allBases.begin(), allBases.end());
  toMerge.push_back(baseList);

  return _mroMerge(std::move(className), std::move(toMerge));
}

std::vector<std::shared_ptr<const BaseStrictObject>> StrictType::mro() const {
  if (mro_.has_value()) {
    return mro_.value();
  }
  auto mroList = _mro(name_, shared_from_this());
  mro_ = std::vector<std::shared_ptr<const BaseStrictObject>>(
      mroList.begin(), mroList.end());
  return mro_.value();
}

std::shared_ptr<BaseStrictObject> StrictType::typeLookup(
    const std::string& name,
    const CallerContext& caller) {
  for (auto cls : mro()) {
    auto clsP = std::const_pointer_cast<BaseStrictObject>(cls);
    std::shared_ptr<StrictType> typ =
        std::dynamic_pointer_cast<StrictType>(clsP);
    if (!typ) {
      iLoadAttr(clsP, name, nullptr, caller);
      continue;
    }

    auto result = typ->getAttr(name);
    if (result) {
      return result;
    }
  }
  return std::shared_ptr<BaseStrictObject>();
}

std::string StrictType::getDisplayName() const {
  return name_;
}

bool StrictType::isBaseType() const {
  return true;
}

bool StrictType::isDataDescr() const {
  return false;
}

void StrictType::addMethods() {}

} // namespace strictmod::objects
