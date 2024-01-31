// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Objects/iterator_objects.h"

#include "cinderx/StrictModules/Objects/callable_wrapper.h"
#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/objects.h"

namespace strictmod::objects {
// StrictIteratorBase
std::shared_ptr<BaseStrictObject> StrictIteratorBase::iterator__contains__(
    std::shared_ptr<StrictIteratorBase> self,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> element) {
  while (true) {
    auto e = self->next(caller);
    if (self->isEnd()) {
      break;
    }
    if (iStrictObjectEq(element, e, caller)) {
      return StrictTrue();
    }
  }
  return StrictFalse();
}

// StrictSequenceIterator final
StrictSequenceIterator::StrictSequenceIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictSequence> obj)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      obj_(std::move(obj)),
      it_(obj_->getData().cbegin()),
      end_(obj_->getData().cend()),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictSequenceIterator::next(
    const CallerContext&) {
  if (it_ == end_) {
    done_ = true;
    return nullptr;
  }
  return *(it_++);
}

bool StrictSequenceIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject>
StrictSequenceIterator::sequenceIterator__next__(
    std::shared_ptr<StrictSequenceIterator> self,
    const CallerContext& caller) {
  // follow python protocols, raise StopIteration when iteration ends
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject>
StrictSequenceIterator::sequenceIterator__iter__(
    std::shared_ptr<StrictSequenceIterator> self,
    const CallerContext&) {
  return self;
}

// ReverseStrictSequenceIterator final
StrictReverseSequenceIterator::StrictReverseSequenceIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictSequence> obj)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      obj_(std::move(obj)),
      it_(obj_->getData().rbegin()),
      end_(obj_->getData().rend()),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictReverseSequenceIterator::next(
    const CallerContext&) {
  if (it_ == end_) {
    done_ = true;
    return nullptr;
  }
  return *(it_++);
}

bool StrictReverseSequenceIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject>
StrictReverseSequenceIterator::reverseSequenceIterator__next__(
    std::shared_ptr<StrictReverseSequenceIterator> self,
    const CallerContext& caller) {
  // follow python protocols, raise StopIteration when iteration ends
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject>
StrictReverseSequenceIterator::reverseSequenceIterator__iter__(
    std::shared_ptr<StrictReverseSequenceIterator> self,
    const CallerContext&) {
  return self;
}

// StrictVectorIterator
StrictVectorIterator::StrictVectorIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> elements)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      elements_(std::move(elements)),
      it_(elements_.cbegin()),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictVectorIterator::next(
    const CallerContext&) {
  if (it_ == elements_.cend()) {
    done_ = true;
    return nullptr;
  }
  return *(it_++);
}

bool StrictVectorIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject> StrictVectorIterator::vectorIterator__next__(
    std::shared_ptr<StrictVectorIterator> self,
    const CallerContext& caller) {
  // follow python protocols, raise StopIteration when iteration ends
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject> StrictVectorIterator::vectorIterator__iter__(
    std::shared_ptr<StrictVectorIterator> self,
    const CallerContext&) {
  return self;
}
// StrictRangeIterator

StrictRangeIterator::StrictRangeIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictRange> rangeObj)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      range_(std::move(rangeObj)),
      done_(false) {
  current_ = assertStaticCast<StrictInt>(range_->getStart())->getValueOr(0);
  stop_ = assertStaticCast<StrictInt>(range_->getStop())->getValueOr(-1);
  step_ = assertStaticCast<StrictInt>(range_->getStep())->getValueOr(1);
}

std::shared_ptr<BaseStrictObject> StrictRangeIterator::next(
    const CallerContext& caller) {
  if (current_ >= stop_) {
    done_ = true;
    return nullptr;
  }
  long result = current_;
  current_ += step_;
  return caller.makeInt(result);
}

bool StrictRangeIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject> StrictRangeIterator::rangeIterator__next__(
    std::shared_ptr<StrictRangeIterator> self,
    const CallerContext& caller) {
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject> StrictRangeIterator::rangeIterator__iter__(
    std::shared_ptr<StrictRangeIterator> self,
    const CallerContext&) {
  return self;
}

// GeneratorExp
StrictGeneratorExp::StrictGeneratorExp(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> data)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      data_(std::move(data)),
      it_(data_.cbegin()),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictGeneratorExp::next(
    const CallerContext&) {
  if (it_ == data_.cend()) {
    done_ = true;
    return nullptr;
  }
  return *(it_++);
}

bool StrictGeneratorExp::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject> StrictGeneratorExp::generatorExp__next__(
    std::shared_ptr<StrictGeneratorExp> self,
    const CallerContext& caller) {
  if (self->isEnd()) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->isEnd()) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject> StrictGeneratorExp::generatorExp__iter__(
    std::shared_ptr<StrictGeneratorExp> self,
    const CallerContext&) {
  return self;
}

// StrictSetIterator final
StrictSetIterator::StrictSetIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictSetLike> obj)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      obj_(std::move(obj)),
      it_(obj_->getData().cbegin()),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictSetIterator::next(
    const CallerContext&) {
  if (it_ == obj_->getData().cend()) {
    done_ = true;
    return nullptr;
  }
  return *(it_++);
}

bool StrictSetIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject> StrictSetIterator::setIterator__next__(
    std::shared_ptr<StrictSetIterator> self,
    const CallerContext& caller) {
  // follow python protocols, raise StopIteration when iteration ends
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject> StrictSetIterator::setIterator__iter__(
    std::shared_ptr<StrictSetIterator> self,
    const CallerContext&) {
  return self;
}

// StrictCallableIterator final
StrictCallableIterator::StrictCallableIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> callable,
    std::shared_ptr<BaseStrictObject> sentinel)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      callable_(callable),
      sentinel_(sentinel),
      count_(0),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictCallableIterator::next(
    const CallerContext& caller) {
  if (done_) {
    return nullptr;
  }
  if (count_ > kIterationLimit) {
    caller.error<StrictModuleTooManyIterationsException>();
    done_ = true;
    return nullptr;
  }
  count_++;
  auto nextValue = iCall(callable_, kEmptyArgs, kEmptyArgNames, caller);
  if (iStrictObjectEq(nextValue, sentinel_, caller)) {
    done_ = true;
    return nullptr;
  }

  return nextValue;
}

bool StrictCallableIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject>
StrictCallableIterator::callableIterator__next__(
    std::shared_ptr<StrictCallableIterator> self,
    const CallerContext& caller) {
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto result = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return result;
}

std::shared_ptr<BaseStrictObject>
StrictCallableIterator::callableIterator__iter__(
    std::shared_ptr<StrictCallableIterator> self,
    const CallerContext&) {
  return self;
}

// StrictGenericObjectIterator final
StrictGenericObjectIterator::StrictGenericObjectIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> obj)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      obj_(std::move(obj)),
      count_(0),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictGenericObjectIterator::next(
    const CallerContext& caller) {
  if (count_ > kIterationLimit) {
    caller.error<StrictModuleTooManyIterationsException>();
    done_ = true;
    return nullptr;
  }
  count_++;
  try {
    return iCall(obj_, kEmptyArgs, kEmptyArgNames, caller);
  } catch (const StrictModuleUserException<BaseStrictObject>& exc) {
    const auto wrapped = exc.getWrapped();
    if (wrapped == StopIterationType() ||
        wrapped->getType() == StopIterationType()) {
      done_ = true;
      return nullptr;
    } else {
      throw;
    }
  }
}

bool StrictGenericObjectIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject>
StrictGenericObjectIterator::objectIterator__next__(
    std::shared_ptr<StrictGeneratorFunction> self,
    const CallerContext& caller) {
  if (self->isEnd()) {
    caller.raiseException(StopIterationType());
  }
  auto result = self->next(caller);
  if (self->isEnd()) {
    caller.raiseException(StopIterationType());
  }
  return result;
}

std::shared_ptr<BaseStrictObject>
StrictGenericObjectIterator::objectIterator__iter__(
    std::shared_ptr<StrictGeneratorFunction> self,
    const CallerContext&) {
  return self;
}

// StrictGeneratorFunction final
StrictGeneratorFunction::StrictGeneratorFunction(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<BaseStrictObject> callable)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      callable_(callable),
      called_(false) {}

std::shared_ptr<BaseStrictObject> StrictGeneratorFunction::next(
    const CallerContext& caller) {
  caller.error<CoroutineFunctionNotSupportedException>(
      callable_->getDisplayName());
  called_ = true;
  auto unknown = makeUnknown(caller, "{}.__next__()", callable_);
  return unknown;
}

bool StrictGeneratorFunction::isEnd() const {
  return called_;
}

std::shared_ptr<BaseStrictObject>
StrictGeneratorFunction::generatorFuncIterator__next__(
    std::shared_ptr<StrictGeneratorFunction> self,
    const CallerContext& caller) {
  if (self->called_) {
    caller.raiseException(StopIterationType());
  }
  return self->next(caller);
}

std::shared_ptr<BaseStrictObject>
StrictGeneratorFunction::generatorFuncIterator__iter__(
    std::shared_ptr<StrictGeneratorFunction> self,
    const CallerContext&) {
  return self;
}

// StrictZipIterator
StrictZipIterator::StrictZipIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> iterators)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      iterators_(std::move(iterators)),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictZipIterator::next(
    const CallerContext& caller) {
  std::vector<std::shared_ptr<BaseStrictObject>> resultVec;
  for (auto it : iterators_) {
    try {
      auto value = nextImpl(nullptr, caller, std::move(it));
      resultVec.push_back(std::move(value));
    } catch (const StrictModuleUserException<BaseStrictObject>& e) {
      const auto wrapped = e.getWrapped();
      if (wrapped == StopIterationType() ||
          wrapped->getType() == StopIterationType()) {
        done_ = true;
        return nullptr;
      }
      throw;
    }
  }
  return std::make_shared<StrictTuple>(
      TupleType(), caller.caller, std::move(resultVec));
}

bool StrictZipIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject> StrictZipIterator::zipIterator__next__(
    std::shared_ptr<StrictZipIterator> self,
    const CallerContext& caller) {
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject> StrictZipIterator::zipIterator__iter__(
    std::shared_ptr<StrictZipIterator> self,
    const CallerContext&) {
  return self;
}

// StrictMapIterator
StrictMapIterator::StrictMapIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::vector<std::shared_ptr<BaseStrictObject>> iterators,
    std::shared_ptr<BaseStrictObject> func)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      iterators_(std::move(iterators)),
      func_(std::move(func)),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictMapIterator::next(
    const CallerContext& caller) {
  std::vector<std::shared_ptr<BaseStrictObject>> argsVec;
  for (auto it : iterators_) {
    try {
      auto value = nextImpl(nullptr, caller, std::move(it));
      argsVec.push_back(std::move(value));
    } catch (const StrictModuleUserException<BaseStrictObject>& e) {
      const auto wrapped = e.getWrapped();
      if (wrapped == StopIterationType() ||
          wrapped->getType() == StopIterationType()) {
        done_ = true;
        return nullptr;
      }
      throw;
    }
  }
  return iCall(func_, argsVec, kEmptyArgNames, caller);
}

bool StrictMapIterator::isEnd() const {
  return done_;
}

std::shared_ptr<BaseStrictObject> StrictMapIterator::mapIterator__next__(
    std::shared_ptr<StrictMapIterator> self,
    const CallerContext& caller) {
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  auto res = self->next(caller);
  if (self->done_) {
    caller.raiseException(StopIterationType());
  }
  return res;
}

std::shared_ptr<BaseStrictObject> StrictMapIterator::mapIterator__iter__(
    std::shared_ptr<StrictMapIterator> self,
    const CallerContext&) {
  return self;
}

//-----------------------------Type declarations--------------------------

// StrictIteratorBaseType
void StrictIteratorBaseType::addMethods() {
  addMethod(kDunderContains, StrictIteratorBase::iterator__contains__);
}

std::shared_ptr<StrictIteratorBase> StrictIteratorBaseType::getElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext&) {
  std::shared_ptr<StrictIteratorBase> it =
      assertStaticCast<StrictIteratorBase>(obj);
  return it;
}

std::vector<std::shared_ptr<BaseStrictObject>>
StrictIteratorBaseType::getElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller) {
  std::shared_ptr<StrictIteratorBase> it =
      assertStaticCast<StrictIteratorBase>(obj);
  std::vector<std::shared_ptr<BaseStrictObject>> vec;
  while (true) {
    auto nextVal = it->next(caller);
    if (it->isEnd()) {
      break;
    }
    vec.push_back(std::move(nextVal));
  }
  return vec;
}

std::shared_ptr<StrictType> StrictIteratorBaseType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictIteratorBaseType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictIteratorBaseType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec = StrictObjectType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictIteratorBaseType));
  return baseVec;
}

// StrictSequenceIteratorType
void StrictSequenceIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictSequenceIterator::sequenceIterator__iter__);
  addMethod(kDunderNext, StrictSequenceIterator::sequenceIterator__next__);
}

std::shared_ptr<StrictType> StrictSequenceIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictSequenceIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictSequenceIteratorType::getBaseTypeinfos()
    const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictSequenceIteratorType));
  return baseVec;
}

// StrictReverseSequenceIteratorType
void StrictReverseSequenceIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(
      kDunderIter,
      StrictReverseSequenceIterator::reverseSequenceIterator__iter__);
  addMethod(
      kDunderNext,
      StrictReverseSequenceIterator::reverseSequenceIterator__next__);
}

std::shared_ptr<StrictType> StrictReverseSequenceIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictReverseSequenceIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index>
StrictReverseSequenceIteratorType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictReverseSequenceIteratorType));
  return baseVec;
}

// StrictVectorIteratorType
void StrictVectorIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictVectorIterator::vectorIterator__iter__);
  addMethod(kDunderNext, StrictVectorIterator::vectorIterator__next__);
}

std::shared_ptr<StrictType> StrictVectorIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictVectorIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictVectorIteratorType::getBaseTypeinfos()
    const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictVectorIteratorType));
  return baseVec;
}

// StrictRangeIteratorType
void StrictRangeIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictRangeIterator::rangeIterator__iter__);
  addMethod(kDunderNext, StrictRangeIterator::rangeIterator__next__);
}

std::shared_ptr<StrictType> StrictRangeIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictRangeIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictRangeIteratorType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictRangeIteratorType));
  return baseVec;
}

// StrictGeneratorExpType
void StrictGeneratorExpType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictGeneratorExp::generatorExp__iter__);
  addMethod(kDunderNext, StrictGeneratorExp::generatorExp__next__);
}

std::shared_ptr<StrictType> StrictGeneratorExpType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictGeneratorExpType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictGeneratorExpType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictGeneratorExpType));
  return baseVec;
}

// StrictSetIteratorType
void StrictSetIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictSetIterator::setIterator__iter__);
  addMethod(kDunderNext, StrictSetIterator::setIterator__next__);
}

std::shared_ptr<StrictType> StrictSetIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictSetIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictSetIteratorType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictSetIteratorType));
  return baseVec;
}

// StrictCallableIteratorType
void StrictCallableIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictCallableIterator::callableIterator__iter__);
  addMethod(kDunderNext, StrictCallableIterator::callableIterator__next__);
}

std::shared_ptr<StrictType> StrictCallableIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictCallableIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictCallableIteratorType::getBaseTypeinfos()
    const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictCallableIteratorType));
  return baseVec;
}

// StrictGenericObjectIteratorType
void StrictGenericObjectIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictGenericObjectIterator::objectIterator__iter__);
  addMethod(kDunderNext, StrictGenericObjectIterator::objectIterator__next__);
}

std::shared_ptr<StrictType> StrictGenericObjectIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictGenericObjectIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictGenericObjectIteratorType::getBaseTypeinfos()
    const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictGenericObjectIteratorType));
  return baseVec;
}

// StrictGeneratorFunctionType
void StrictGeneratorFunctionType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(
      kDunderIter, StrictGeneratorFunction::generatorFuncIterator__iter__);
  addMethod(
      kDunderNext, StrictGeneratorFunction::generatorFuncIterator__next__);
}

std::shared_ptr<StrictType> StrictGeneratorFunctionType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictGeneratorFunctionType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictGeneratorFunctionType::getBaseTypeinfos()
    const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictGeneratorFunctionType));
  return baseVec;
}

// StrictZipIteratorType
void StrictZipIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictZipIterator::zipIterator__iter__);
  addMethod(kDunderNext, StrictZipIterator::zipIterator__next__);
}

std::shared_ptr<StrictType> StrictZipIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictZipIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictZipIteratorType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictZipIteratorType));
  return baseVec;
}

// StrictMapIteratorType
void StrictMapIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictMapIterator::mapIterator__iter__);
  addMethod(kDunderNext, StrictMapIterator::mapIterator__next__);
}

std::shared_ptr<StrictType> StrictMapIteratorType::recreate(
    std::string name,
    std::weak_ptr<StrictModuleObject> caller,
    std::vector<std::shared_ptr<BaseStrictObject>> bases,
    std::shared_ptr<DictType> members,
    std::shared_ptr<StrictType> metatype,
    bool isImmutable) {
  return createType<StrictMapIteratorType>(
      std::move(name),
      std::move(caller),
      std::move(bases),
      std::move(members),
      std::move(metatype),
      isImmutable);
}

std::vector<std::type_index> StrictMapIteratorType::getBaseTypeinfos() const {
  std::vector<std::type_index> baseVec =
      StrictIteratorBaseType::getBaseTypeinfos();
  baseVec.emplace_back(typeid(StrictMapIteratorType));
  return baseVec;
}
} // namespace strictmod::objects
