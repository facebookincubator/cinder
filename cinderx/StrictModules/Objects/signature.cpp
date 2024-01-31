// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/signature.h"

#include "cinderx/StrictModules/Objects/objects.h"
#include "cinderx/StrictModules/caller_context_impl.h"
#include "cinderx/StrictModules/sequence_map.h"

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

  size_t posonlyCount = posonlyArgs_.size();
  size_t posCount = posArgs_.size() + posonlyCount;
  size_t posDefaultCount = posDefaults_.size();
  assert(args.size() >= names.size());
  size_t nonNamedArgsCount = args.size() - names.size();

  std::vector<std::shared_ptr<BaseStrictObject>> varArgValues;
  // populate positional args without defaults
  for (size_t i = 0; i < nonNamedArgsCount; ++i) {
    if (i >= posCount) {
      // unnamed argument but no more positional parameters
      // check vararg
      if (varArg_.has_value()) {
        // put the rest of non-named args into varArg
        varArgValues.reserve(nonNamedArgsCount - posCount);
        for (size_t j = i; j < nonNamedArgsCount; ++j) {
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
  // still have positional only parameters without provided args
  // these must come from defaults.
  if (nonNamedArgsCount < posonlyCount) {
    size_t posOnlyNeedDefaultOffset = nonNamedArgsCount;
    assert(posCount >= posDefaultCount);
    size_t posDefaultsOffset = posCount - posDefaultCount;
    if (posOnlyNeedDefaultOffset < posDefaultsOffset) {
      caller.raiseTypeError(
          "{} missing required positional argument {}",
          funcName_,
          posonlyArgs_[posOnlyNeedDefaultOffset]);
    }
    for (size_t i = posOnlyNeedDefaultOffset; i < posonlyCount; ++i) {
      map[posonlyArgs_[i]] = std::move(posDefaults_[i - posDefaultsOffset]);
    }
  }

  sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwMap;
  kwMap.reserve(names.size());
  for (size_t i = nonNamedArgsCount; i < args.size(); ++i) {
    kwMap[names[i - nonNamedArgsCount]] = args[i];
  }
  // process the rest of posArgs_

  // this can be negative when posDefaults also covers posonly args
  int posDefaultsOffset = posArgs_.size() - posDefaultCount;
  assert(map.size() - posonlyCount >= 0); // since we processed all posonly args
  for (size_t i = map.size() - posonlyCount; i < posArgs_.size(); ++i) {
    const std::string& posArgName = posArgs_[i];
    auto got = kwMap.find(posArgName);
    if (got == kwMap.map_end()) {
      // no default for this arg, error
      if (int(i) < posDefaultsOffset) {
        caller.raiseTypeError(
            "{} missing required positional argument {}",
            funcName_,
            posArgName);
      }
      // this positional arg will use default value
      map[posArgName] = posDefaults_[i - posDefaultsOffset];
    } else {
      // caller provided value for this posArg
      map[posArgName] = got->second.first;
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
    if (got == kwMap.map_end()) {
      const std::shared_ptr<BaseStrictObject> kwArgDefault = kwDefaults_[i];
      if (kwArgDefault == nullptr) {
        // error
        caller.raiseTypeError(
            "{} missing required keyword argument {}", funcName_, kwArgName);
      }
      map[kwArgName] = kwArgDefault;
    } else {
      map[kwArgName] = got->second.first;
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
      if (kwMap.find(posName) != kwMap.map_end()) {
        duplicated = true;
        duplicatedName = posName;
        break;
      }
    }
    if (!duplicated) {
      for (auto& kwName : kwonlyArgs_) {
        if (kwMap.find(kwName) != kwMap.map_end()) {
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
      kwDict[caller.makeStr(item.first)] = item.second.first;
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
