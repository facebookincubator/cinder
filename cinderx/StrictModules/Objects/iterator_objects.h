// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/instance.h"
#include "cinderx/StrictModules/Objects/iterable_objects.h"
#include "cinderx/StrictModules/Objects/object_type.h"

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
  std::vector<std::shared_ptr<BaseStrictObject>>::const_iterator
      end_; // iterator state
  bool done_;
};

class StrictReverseSequenceIterator final : public StrictIteratorBase {
 public:
  StrictReverseSequenceIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictSequence> obj);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> reverseSequenceIterator__next__(
      std::shared_ptr<StrictReverseSequenceIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> reverseSequenceIterator__iter__(
      std::shared_ptr<StrictReverseSequenceIterator> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<StrictSequence> obj_; // object getting iterated on
  std::vector<std::shared_ptr<BaseStrictObject>>::const_reverse_iterator
      it_; // iterator state
  std::vector<std::shared_ptr<BaseStrictObject>>::const_reverse_iterator
      end_; // iterator state
  bool done_;
};

class StrictVectorIterator final : public StrictIteratorBase {
 public:
  StrictVectorIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> elements);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> vectorIterator__next__(
      std::shared_ptr<StrictVectorIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> vectorIterator__iter__(
      std::shared_ptr<StrictVectorIterator> self,
      const CallerContext& caller);

 private:
  std::vector<std::shared_ptr<BaseStrictObject>> elements_;
  std::vector<std::shared_ptr<BaseStrictObject>>::const_iterator
      it_; // iterator state
  bool done_;
};

class StrictRangeIterator final : public StrictIteratorBase {
 public:
  StrictRangeIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::shared_ptr<StrictRange> rangeObj);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> rangeIterator__next__(
      std::shared_ptr<StrictRangeIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> rangeIterator__iter__(
      std::shared_ptr<StrictRangeIterator> self,
      const CallerContext& caller);

 private:
  std::shared_ptr<StrictRange> range_;
  long current_;
  long stop_;
  long step_;
  bool done_;
};

class StrictGeneratorExp final : public StrictIteratorBase {
 public:
  StrictGeneratorExp(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> data);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> generatorExp__next__(
      std::shared_ptr<StrictGeneratorExp> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> generatorExp__iter__(
      std::shared_ptr<StrictGeneratorExp> self,
      const CallerContext& caller);

 private:
  std::vector<std::shared_ptr<BaseStrictObject>> data_;
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

class StrictZipIterator final : public StrictIteratorBase {
 public:
  StrictZipIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> iterators);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> zipIterator__next__(
      std::shared_ptr<StrictZipIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> zipIterator__iter__(
      std::shared_ptr<StrictZipIterator> self,
      const CallerContext& caller);

 private:
  std::vector<std::shared_ptr<BaseStrictObject>> iterators_;
  bool done_;
};

class StrictMapIterator final : public StrictIteratorBase {
 public:
  StrictMapIterator(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::vector<std::shared_ptr<BaseStrictObject>> iterators,
      std::shared_ptr<BaseStrictObject> func);

  virtual std::shared_ptr<BaseStrictObject> next(
      const CallerContext& caller) override;

  virtual bool isEnd() const override;

  static std::shared_ptr<BaseStrictObject> mapIterator__next__(
      std::shared_ptr<StrictMapIterator> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> mapIterator__iter__(
      std::shared_ptr<StrictMapIterator> self,
      const CallerContext& caller);

 private:
  std::vector<std::shared_ptr<BaseStrictObject>> iterators_;
  std::shared_ptr<BaseStrictObject> func_;
  bool done_;
};

// ------------------------iterator type declarations--------------------
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

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictSequenceIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictReverseSequenceIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictVectorIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictGeneratorExpType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictSetIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictCallableIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictGenericObjectIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictGeneratorFunctionType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictRangeIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictZipIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictMapIteratorType : public StrictIteratorBaseType {
 public:
  using StrictIteratorBaseType::StrictIteratorBaseType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

} // namespace strictmod::objects
