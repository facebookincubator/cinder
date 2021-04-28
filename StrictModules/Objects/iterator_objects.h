// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_ITERATOR_OBJ___
#define __STRICTM_ITERATOR_OBJ___

#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/iterable_objects.h"
#include "StrictModules/Objects/object_type.h"

namespace strictmod::objects {
class StrictIteratorBase : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) = 0;

  virtual bool isEnd() const = 0;

  static std::shared_ptr<BaseStrictObject> iterator__contains__(
      std::shared_ptr<StrictIteratorBase> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> element);
};

class StrictSequenceIterator final : public StrictIteratorBase {
 public:
  StrictSequenceIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictSequence> obj);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> sequenceIterator__next__(
      std::shared_ptr<StrictSequenceIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> sequenceIterator__iter__(
      std::shared_ptr<StrictSequenceIterator> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<StrictSequence> obj_; // object getting iterated on
  std::vector<std::shared_ptr<BaseStrictObject>>::const_iterator
      it_; // iterator state
  bool done_;
};

class StrictSetIterator final : public StrictIteratorBase {
 public:
  StrictSetIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictSetLike> obj);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> setIterator__next__(
      std::shared_ptr<StrictSetIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> setIterator__iter__(
      std::shared_ptr<StrictSetIterator> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<StrictSetLike> obj_; // object getting iterated on
  SetDataT::const_iterator it_; // iterator state
  bool done_;
};

class StrictCallableIterator final : public StrictIteratorBase {
 public:
  StrictCallableIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> callable,
      std::shared_ptr<BaseStrictObject> sentinel);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;
  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> callableIterator__next__(
      std::shared_ptr<StrictCallableIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> callableIterator__iter__(
      std::shared_ptr<StrictCallableIterator> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<BaseStrictObject> callable_;
  std::shared_ptr<BaseStrictObject> sentinel_;
  int count_; // prevent infinite iteration
  bool done_;
};

class StrictGeneratorFunction final : public StrictIteratorBase {
 public:
  StrictGeneratorFunction(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> callable);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;
  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> generatorFuncIterator__next__(
      std::shared_ptr<StrictGeneratorFunction> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> generatorFuncIterator__iter__(
      std::shared_ptr<StrictGeneratorFunction> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<BaseStrictObject> callable_;
  bool called_; // generate unknown on first call and stop after that
};

class StrictGenericObjectIterator final : public StrictIteratorBase {
 public:
  StrictGenericObjectIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<BaseStrictObject> obj);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;
  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> objectIterator__next__(
      std::shared_ptr<StrictGeneratorFunction> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> objectIterator__iter__(
      std::shared_ptr<StrictGeneratorFunction> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<BaseStrictObject> obj_;
  int count_; // prevent infinite iteration
  bool done_;
};

class StrictIteratorBaseType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual void addMethods() override;

  virtual std::shared_ptr<StrictIteratorBase> getElementsIter(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual std::vector<std::shared_ptr<BaseStrictObject>> getElementsVec(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;
};

class StrictSequenceIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;
  virtual void addMethods() override;
};

class StrictSetIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;
  virtual void addMethods() override;
};

class StrictCallableIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;
  virtual void addMethods() override;
};

class StrictGenericObjectIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;
  virtual void addMethods() override;
};

class StrictGeneratorFunctionType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;
  virtual void addMethods() override;
};

} // namespace strictmod::objects
#endif // __STRICTM_ITERATOR_OBJ___
