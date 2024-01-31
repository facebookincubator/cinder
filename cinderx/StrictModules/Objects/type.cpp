// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/type.h"

#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context_impl.h"
#include "cinderx/StrictModules/exceptions.h"

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
      baseClasses_(std::move(bases)),
      immutable_(immutable),
      mro_(),
      isDataDescr_(),
      basesObj_() {}

StrictType::StrictType(
    std::string name,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool immutable)
    : StrictInstance(metatype, std::move(creator), std::move(members)),
      name_(std::move(name)),
      baseClasses_(std::move(bases)),
      immutable_(immutable),
      mro_(),
      isDataDescr_(),
      basesObj_() {}

std::shared_ptr<BaseStrictObject> StrictType::copy(const CallerContext&) {
  return shared_from_this();
}

bool StrictType::isSubType(std::shared_ptr<StrictType> base) const {
  auto mroVec = mro();
  return std::find(mroVec.begin(), mroVec.end(), base) != mroVec.end();
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

bool StrictType::isCallable(const CallerContext& caller) {
  if (isCallable_.has_value()) {
    return isCallable_.value();
  }
  auto dunderCall = typeLookup(kDunderCall, caller);
  isCallable_ = dunderCall != nullptr;
  return isCallable_.value();
}

bool StrictType::isDataDescr(const CallerContext& caller) {
  if (isDataDescr_.has_value()) {
    return isDataDescr_.value();
  }
  auto setDescr = typeLookup(kDunderSet, caller);
  isDataDescr_ = setDescr != nullptr;
  return isDataDescr_.value();
}

void StrictType::addMethods() {}

void StrictType::cleanContent(const StrictModuleObject* owner) {
  if ((!creator_.expired() && owner != creator_.lock().get())) {
    return;
  }
  StrictInstance::cleanContent(owner);
  baseClasses_.clear();
  mro_.reset();
  basesObj_.reset();
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
  auto initFunc = instance->getTypeRef().typeLookup(kDunderInit, caller);
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
  if (initSubclassItem != members.map_end()) {
    // __init_sublcass__ is automatically treated as a class method
    auto initSubclassFunc = std::dynamic_pointer_cast<StrictFunction>(
        initSubclassItem->second.first);
    if (initSubclassFunc != nullptr) {
      auto initSubclassMethod = std::make_shared<StrictClassMethod>(
          ClassMethodType(), caller.caller, std::move(initSubclassFunc));
      initSubclassItem->second.first = std::move(initSubclassMethod);
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
  auto super = std::make_shared<StrictSuper>(
      SuperType(), caller.caller, resultType, resultType, resultType, true);
  auto superInitSubclass =
      iLoadAttr(std::move(super), "__init_subclass__", nullptr, caller);

  if (superInitSubclass != nullptr) {
    std::vector<std::shared_ptr<BaseStrictObject>> argsToInitSubclass;
    argsToInitSubclass.reserve(namedArgs.size());
    argsToInitSubclass.insert(
        argsToInitSubclass.end(),
        std::move_iterator(args.begin() + posArgSize),
        std::move_iterator(args.end()));
    iCall(superInitSubclass, std::move(argsToInitSubclass), namedArgs, caller);
  }

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

std::shared_ptr<StrictTuple> getBasesHelper(
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> cls) {
  try {
    auto bases = iLoadAttr(std::move(cls), "__bases__", nullptr, caller);
    auto basesTuple = std::dynamic_pointer_cast<StrictTuple>(bases);
    if (basesTuple) {
      return basesTuple;
    }
    return nullptr;
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    if (e.getWrapped()->getType() == AttributeErrorType()) {
      // swallow the attribute error. This follows CPython implementation
      return nullptr;
    }
    throw;
  }
}

bool issubclassHelper(
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> derived,
    std::shared_ptr<StrictTuple> derivedBases,
    std::shared_ptr<StrictType> self) {
  if (derived == std::static_pointer_cast<BaseStrictObject>(self)) {
    return true;
  }
  if (!derivedBases) {
    derivedBases = getBasesHelper(caller, derived);
    if (!derivedBases) {
      return false;
    }
  }

  for (auto& base : derivedBases->getData()) {
    if (issubclassHelper(caller, base, nullptr, self)) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<BaseStrictObject> StrictType::type__subclasscheck__(
    std::shared_ptr<StrictType> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> derived) {
  // shortcut if the type(inst) == cls_info
  auto derivedType = std::dynamic_pointer_cast<StrictType>(derived);
  if (derivedType) {
    return caller.makeBool(derivedType->isSubType(self));
  }

  std::shared_ptr<StrictTuple> selfBases = getBasesHelper(caller, self);
  if (!selfBases) {
    caller.raiseTypeError(
        "issubclass() arg 1 must be a class, not {} object",
        self->getTypeRef().getName());
  }

  std::shared_ptr<StrictTuple> derivedBases = getBasesHelper(caller, derived);
  if (!derivedBases) {
    caller.raiseTypeError(
        "issubclass() arg 2 must be a class, tuple of classes or union, not {} "
        "object",
        derived->getTypeRef().getName());
  }

  if (issubclassHelper(caller, derived, derivedBases, self)) {
    return StrictTrue();
  }
  return StrictFalse();
}

std::shared_ptr<BaseStrictObject> StrictType::type__or__(
    std::shared_ptr<StrictType> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> rhs) {
  return unionOrHelper(caller, std::move(self), std::move(rhs));
}

std::shared_ptr<BaseStrictObject> StrictType::type__ror__(
    std::shared_ptr<StrictType> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs) {
  return unionOrHelper(caller, std::move(lhs), std::move(self));
}

std::shared_ptr<BaseStrictObject> StrictType::type__bases__Getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext&) {
  auto cls = assertStaticCast<StrictType>(std::move(inst));
  if (!cls->basesObj_) {
    cls->basesObj_ = std::make_shared<StrictTuple>(
        TupleType(), cls->creator_, cls->baseClasses_);
  }
  return cls->basesObj_;
}

std::shared_ptr<BaseStrictObject> StrictType::type__module__Getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto cls = assertStaticCast<StrictType>(std::move(inst));
  auto mod = cls->getAttr("__module__");
  if (mod) {
    return mod;
  }
  return caller.makeStr("builtins");
}

void StrictType::type__module__Setter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller) {
  checkExternalModification(inst, caller);
  auto cls = assertStaticCast<StrictType>(std::move(inst));
  cls->setAttr("__module__", std::move(value));
  // cls->moduleName_ = value;
}

std::shared_ptr<BaseStrictObject> StrictType::type__mro__Getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto cls = assertStaticCast<StrictType>(std::move(inst));
  return StrictType::typeMro(std::move(cls), caller);
}

std::shared_ptr<BaseStrictObject> StrictType::type__qualname__Getter(
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType>,
    const CallerContext& caller) {
  auto cls = assertStaticCast<StrictType>(std::move(inst));
  return caller.makeStr(cls->getName());
}

} // namespace strictmod::objects
