// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/iterator_objects.h"
#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

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
      it_(obj_->getData().begin()),
      end_(obj_->getData().end()),
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

// StrictSetIterator final
StrictSetIterator::StrictSetIterator(
    std::shared_ptr<StrictType> type,
    std::weak_ptr<StrictModuleObject> creator,
    std::shared_ptr<StrictSetLike> obj)
    : StrictIteratorBase(std::move(type), std::move(creator)),
      obj_(std::move(obj)),
      it_(obj_->getData().begin()),
      done_(false) {}

std::shared_ptr<BaseStrictObject> StrictSetIterator::next(
    const CallerContext&) {
  if (it_ == obj_->getData().end()) {
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
    if (exc.getWrapped() == StopIterationType()) {
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

// StrictSequenceIteratorType
void StrictSequenceIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictSequenceIterator::sequenceIterator__iter__);
  addMethod(kDunderNext, StrictSequenceIterator::sequenceIterator__next__);
}

// StrictSetIteratorType
void StrictSetIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictSetIterator::setIterator__iter__);
  addMethod(kDunderNext, StrictSetIterator::setIterator__next__);
}

// StrictCallableIteratorType
void StrictCallableIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictCallableIterator::callableIterator__iter__);
  addMethod(kDunderNext, StrictCallableIterator::callableIterator__next__);
}

// StrictGenericObjectIteratorType
void StrictGenericObjectIteratorType::addMethods() {
  StrictIteratorBaseType::addMethods();
  addMethod(kDunderIter, StrictGenericObjectIterator::objectIterator__iter__);
  addMethod(kDunderNext, StrictGenericObjectIterator::objectIterator__next__);
}

// StrictGeneratorFunctionType
void StrictGeneratorFunctionType::addMethods() {
  addMethod(
      kDunderIter, StrictGeneratorFunction::generatorFuncIterator__iter__);
  addMethod(
      kDunderNext, StrictGeneratorFunction::generatorFuncIterator__next__);
}
} // namespace strictmod::objects
