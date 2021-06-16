// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/type.h"

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/exceptions.h"

#include <functional>
#include <list>
#include <numeric>
#include <stdexcept>
#include <typeinfo>

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

StrictType::StrictType(
    std::string name,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool immutable)
    : StrictInstance(metatype, creator, std::move(members)),
      name_(std::move(name)),
      moduleName_(
          (creator.expired() || creator.lock() == nullptr)
              ? ""
              : creator.lock()->getModuleName()),
      baseClasses_(std::move(bases)),
      immutable_(immutable),
      mro_() {}

bool StrictType::isSubType(std::shared_ptr<StrictType> base) const {
  if (base.get() == this) {
    return true;
  }
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

const std::vector<std::shared_ptr<const BaseStrictObject>>& StrictType::mro()
    const {
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

bool StrictType::hasSubLayout(std::shared_ptr<StrictType> other) const {
  std::vector<std::type_index> baseTypeinfo = getBaseTypeinfos();
  StrictType& otherVal = *other;
  auto found = std::find(
      baseTypeinfo.begin(),
      baseTypeinfo.end(),
      std::type_index(typeid(otherVal)));
  return found != baseTypeinfo.end();
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

void StrictType::cleanContent(const StrictModuleObject* owner) {
  StrictInstance::cleanContent(owner);
  if (creator_.expired() || owner == creator_.lock().get()) {
    baseClasses_.clear();
    mro_.reset();
  }
}

std::shared_ptr<BaseStrictObject> StrictType::type__call__(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  std::shared_ptr<StrictType> self =
      assertStaticCast<StrictType>(std::move(obj));
  auto newFunc = self->typeLookup("__new__", caller);
  if (newFunc == nullptr) {
    caller.raiseTypeError("unsupported MRO type: {}", self->getName());
  }
  std::vector<std::shared_ptr<BaseStrictObject>> newArgs;
  newArgs.reserve(args.size() + 1);
  newArgs.push_back(self);
  newArgs.insert(newArgs.end(), args.begin(), args.end());
  auto instance = iCall(std::move(newFunc), newArgs, namedArgs, caller);
  auto initFunc = instance->getTypeRef().typeLookup("__init__", caller);
  if (initFunc != nullptr) {
    auto initMethod = iGetDescr(initFunc, instance, self, caller);
    iCall(initMethod, args, namedArgs, caller);
  }
  return instance;
}

static std::shared_ptr<BaseStrictObject> verifyBases(
    const std::vector<std::shared_ptr<BaseStrictObject>>& bases) {
  for (auto& base : bases) {
    if (base->getType() == UnknownType()) {
      continue;
    }
    if (std::dynamic_pointer_cast<StrictType>(base) != nullptr) {
      continue;
    }
    return base;
  }
  return nullptr;
}

static std::shared_ptr<StrictType> calcMetaclass(
    std::shared_ptr<StrictType> metaType,
    const std::vector<std::shared_ptr<BaseStrictObject>>& bases,
    const CallerContext& caller) {
  for (auto& base : bases) {
    std::shared_ptr<StrictType> baseType =
        std::dynamic_pointer_cast<StrictType>(base);
    if (baseType == nullptr) {
      continue;
    }
    std::shared_ptr<StrictType> baseMetaType = baseType->getType();
    if (metaType->isSubType(baseMetaType)) {
      continue;
    } else if (baseMetaType->isSubType(metaType)) {
      // use the most basic meta type
      metaType = std::move(baseMetaType);
      continue;
    }
    caller.raiseTypeError("metaclass conflict");
  }
  return metaType;
}

static std::shared_ptr<StrictType> bestBase(
    const std::vector<std::shared_ptr<BaseStrictObject>>& bases,
    const CallerContext& caller) {
  std::shared_ptr<StrictType> winner;
  for (auto& base : bases) {
    std::shared_ptr<StrictType> baseType =
        std::dynamic_pointer_cast<StrictType>(base);
    if (baseType == nullptr) {
      // inheriting from an unknown type, just construct a generic object
      return ObjectType();
    }
    if (!baseType->isBaseType()) {
      caller.raiseTypeError(
          "type '{}' is not a base type", baseType->getName());
    }
    if (winner == nullptr) {
      winner = std::move(baseType);
    } else if (baseType->hasSubLayout(winner)) {
      winner = std::move(baseType);
    } else if (!winner->hasSubLayout(baseType)) {
      caller.raiseTypeError("multiple bases have layout conflict");
    }
  }
  return winner;
}

std::shared_ptr<BaseStrictObject> StrictType::type__new__(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller) {
  int posArgSize = args.size() - namedArgs.size();
  if (posArgSize != 2 && posArgSize != 4) {
    caller.raiseTypeError("type() takes 1 or 3 arguments");
  }
  std::shared_ptr<StrictType> metaType = assertStaticCast<StrictType>(args[0]);
  std::shared_ptr<BaseStrictObject> nameOrVal = args[1];
  if (posArgSize == 2) {
    // special case of type(v) / type.__new__(X, v),
    // where we return type of v
    return nameOrVal->getType();
  }

  auto name = std::dynamic_pointer_cast<StrictString>(nameOrVal);
  if (name == nullptr) {
    caller.raiseTypeError(
        "type.__new__() first arg must be str, not {} object",
        nameOrVal->getTypeRef().getName());
  }

  // Decide and verify base classes
  auto baseClassObj = args[2];
  auto baseClassTuple = std::dynamic_pointer_cast<StrictTuple>(baseClassObj);
  if (baseClassTuple == nullptr) {
    caller.raiseTypeError(
        "type.__new__() second arg must be tuple, not {} object",
        baseClassObj->getTypeRef().getName());
  }
  auto baseClassVec = baseClassTuple->getData();
  if (auto badBase = verifyBases(baseClassVec); badBase != nullptr) {
    caller.error<UnsafeBaseClassException>(badBase->getDisplayName());
    return makeUnknown(caller, "<bad type {}>", name->getValue());
  }
  if (baseClassVec.empty()) {
    // default base type is object
    baseClassVec.push_back(ObjectType());
  }

  auto membersObj = args[3];
  auto membersDict = std::dynamic_pointer_cast<StrictDict>(membersObj);
  if (membersDict == nullptr) {
    caller.raiseTypeError(
        "type.__new__() third arg must be dict, not {} object",
        membersObj->getTypeRef().getName());
  }
  const DictDataInterface& membersData = membersDict->getData();
  std::shared_ptr<DictType> membersPtr = std::make_shared<DictType>();
  DictType& members = *membersPtr;
  members.reserve(membersData.size());
  membersData.const_iter([&members](
                             std::shared_ptr<BaseStrictObject> k,
                             std::shared_ptr<BaseStrictObject> v) {
    auto keyStr = std::dynamic_pointer_cast<StrictString>(std::move(k));
    if (keyStr != nullptr) {
      members[keyStr->getValue()] = std::move(v);
    }
    return true;
  });

  auto initSubclassItem = members.find("__init_subclass__");
  if (initSubclassItem != members.end()) {
    // __init_sublcass__ is automatically treated as a class method
    auto initSubclassFunc =
        std::dynamic_pointer_cast<StrictFunction>(initSubclassItem->second);
    if (initSubclassFunc != nullptr) {
      auto initSubclassMethod = std::make_shared<StrictClassMethod>(
          caller.caller, std::move(initSubclassFunc));
      initSubclassItem->second = std::move(initSubclassMethod);
    }
  }

  // decide which metaclass to use
  std::shared_ptr<StrictType> bestMeta =
      calcMetaclass(metaType, baseClassVec, caller);
  assert(bestMeta != nullptr);
  // decide which strict type instance to create
  std::shared_ptr<StrictType> bestConstructor = bestBase(baseClassVec, caller);
  // layout conflict
  if (bestConstructor == nullptr) {
    return makeUnknown(caller, "<bad type {}>", name->getValue());
  }
  std::shared_ptr<StrictType> resultType = bestConstructor->recreate(
      name->getValue(),
      caller.caller,
      std::move(baseClassVec),
      std::move(membersPtr),
      std::move(bestMeta),
      false);
  // check for mro conflict
  try {
    resultType->mro();
  } catch (const std::runtime_error&) {
    caller.raiseTypeError(
        "Cannot create a consistent method resolution order (MRO) for class {}",
        name->getValue());
  }

  // handle __init_subclass__ from superclass
  // TODO
  return resultType;
}

std::shared_ptr<BaseStrictObject> StrictType::typeMro(
    std::shared_ptr<StrictType> self,
    const CallerContext& caller) {
  const auto& mroObj = self->mro();
  std::vector<std::shared_ptr<BaseStrictObject>> result;
  result.reserve(mroObj.size());
  for (const std::shared_ptr<const BaseStrictObject>& b : mroObj) {
    result.push_back(std::const_pointer_cast<BaseStrictObject>(b));
  }
  return std::make_shared<StrictList>(
      ListType(), caller.caller, std::move(result));
}

} // namespace strictmod::objects
