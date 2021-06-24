// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/signature.h"

#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context_impl.h"

#include <unordered_map>

namespace strictmod::objects {

// FuncSignature
FuncSignature::FuncSignature(
    const std::string& name,
    const std::vector<std::string>& posonlyArgs_,
    const std::vector<std::string>& posArgs_,
    const std::vector<std::string>& kwonlyArgs_,
    const std::optional<std::string>& varArg_,
    const std::optional<std::string>& kwVarArg_,
    const std::vector<std::shared_ptr<BaseStrictObject>>& posDefaults_,
    const std::vector<std::shared_ptr<BaseStrictObject>>& kwDefaults_)
    : funcName_(name),
      posonlyArgs_(posonlyArgs_),
      posArgs_(posArgs_),
      kwonlyArgs_(kwonlyArgs_),
      varArg_(varArg_),
      kwVarArg_(kwVarArg_),
      posDefaults_(posDefaults_),
      kwDefaults_(kwDefaults_) {}

std::unique_ptr<DictType> FuncSignature::bind(
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& names,
    const CallerContext& caller) {
  std::unique_ptr<DictType> mapPtr = std::make_unique<DictType>();
  DictType& map = *mapPtr;
  map.reserve(args.size());

  int posonlyCount = posonlyArgs_.size();
  int posCount = posArgs_.size() + posonlyCount;
  int posDefaultCount = posDefaults_.size();

  int nonNamedArgsCount = args.size() - names.size();
  // The number of passed pos args must cover all pos only parameters
  if (nonNamedArgsCount < posonlyCount) {
    caller.raiseTypeError(
        "{} got some positional only arguments passed as keyword arguments",
        funcName_);
  }

  std::vector<std::shared_ptr<BaseStrictObject>> varArgValues;
  // populate positional args without defaults
  for (int i = 0; i < nonNamedArgsCount; ++i) {
    if (i >= posCount) {
      // unnamed argument but no more positional parameters
      // check vararg
      if (varArg_.has_value()) {
        // put the rest of non-named args into varArg
        varArgValues.reserve(nonNamedArgsCount - posCount);
        for (int j = i; j < nonNamedArgsCount; ++j) {
          varArgValues.push_back(args[j]);
        }
        break;
      } else {
        // error
        caller.raiseTypeError(
            "{} takes {} positional arguments but {} were given",
            funcName_,
            posCount,
            nonNamedArgsCount);
      }
    }
    // still have unnamed argument to process against positional params
    if (i < posonlyCount) {
      map[posonlyArgs_[i]] = std::move(args[i]);
    } else {
      map[posArgs_[i - posonlyCount]] = std::move(args[i]);
    }
  }

  std::unordered_map<std::string, std::shared_ptr<BaseStrictObject>> kwMap;
  kwMap.reserve(names.size());
  for (size_t i = nonNamedArgsCount; i < args.size(); ++i) {
    kwMap[names[i - nonNamedArgsCount]] = args[i];
  }
  // process the rest of posArgs_
  int posDefaultsOffset = posArgs_.size() - posDefaultCount;
  assert(posDefaultsOffset >= 0);
  assert(map.size() - posonlyCount >= 0);
  for (size_t i = map.size() - posonlyCount; i >= 0 && i < posArgs_.size();
       ++i) {
    const std::string& posArgName = posArgs_[i];
    auto got = kwMap.find(posArgName);
    if (got == kwMap.end()) {
      // no default for this arg, error
      if (i < size_t(posDefaultsOffset)) {
        caller.raiseTypeError(
            "{} missing required positional argument {}",
            funcName_,
            posArgName);
      }
      // this positional arg will use default value
      map[posArgName] = posDefaults_[i - posDefaultsOffset];
    } else {
      // caller provided value for this posArg
      map[posArgName] = got->second;
      kwMap.erase(got);
    }
  }
  // add VarArg if it exists
  if (varArg_.has_value()) {
    std::shared_ptr<BaseStrictObject> varArgObj = std::make_shared<StrictTuple>(
        TupleType(), caller.caller, std::move(varArgValues));
    map[varArg_.value()] = std::move(varArgObj);
  }

  // process kwonlyArgs. Note that kwonlyDefaults always
  // have the same size as kwonlyArgs
  for (size_t i = 0; i < kwonlyArgs_.size(); ++i) {
    const std::string& kwArgName = kwonlyArgs_[i];
    auto got = kwMap.find(kwArgName);
    if (got == kwMap.end()) {
      const std::shared_ptr<BaseStrictObject> kwArgDefault = kwDefaults_[i];
      if (kwArgDefault == nullptr) {
        // error
        caller.raiseTypeError(
            "{} missing required keyword argument {}", funcName_, kwArgName);
      }
      map[kwArgName] = kwArgDefault;
    } else {
      map[kwArgName] = got->second;
      kwMap.erase(got);
    }
  }
  // the rest of the kwMap elements are not used in any posArgs_ or
  // kwArgs, these are used as kwVararg
  if (kwVarArg_.has_value()) {
    // check that there are no duplicate args
    bool duplicated = false;
    std::string duplicatedName;
    for (auto& posName : posArgs_) {
      if (kwMap.find(posName) != kwMap.end()) {
        duplicated = true;
        duplicatedName = posName;
        break;
      }
    }
    if (!duplicated) {
      for (auto& kwName : kwonlyArgs_) {
        if (kwMap.find(kwName) != kwMap.end()) {
          duplicated = true;
          duplicatedName = kwName;
          break;
        }
      }
    }
    if (duplicated) {
      caller.raiseTypeError(
          "{} got multiple values for argument '{}'",
          funcName_,
          duplicatedName);
    }

    // create strict object for kwVarArg_
    DictDataT kwDict;
    kwDict.reserve(kwMap.size());
    for (auto& item : kwMap) {
      kwDict[caller.makeStr(item.first)] = item.second;
    }
    map[kwVarArg_.value()] = std::make_shared<StrictDict>(
        DictObjectType(), caller.caller, std::move(kwDict));
  } else if (!kwMap.empty()) {
    // error
    auto got = kwMap.begin();
    caller.raiseTypeError(
        "{} got unexpected keyword argument {}", funcName_, got->first);
  }
  return mapPtr;
}
} // namespace strictmod::objects
