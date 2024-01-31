// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/instance.h"
#include "cinderx/StrictModules/Objects/object_type.h"

#include <optional>

namespace strictmod::objects {

class StrictNumeric : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;
  virtual bool isOverflow() const = 0;
  virtual bool isHashable() const override;
  virtual bool eq(const BaseStrictObject& other) const override;
  virtual std::optional<double> getReal() const = 0;
};

typedef long long int_type;

class StrictInt : public StrictNumeric {
 public:
  StrictInt(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      int_type value);

  StrictInt(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      int_type value);

  StrictInt(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      PyObject* pyValue // constructor will incref on the object
  );

  virtual bool isOverflow() const override;

  virtual size_t hash() const override;
  virtual std::optional<double> getReal() const override;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> int__bool__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__str__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__abs__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__round__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> ndigit = nullptr);

  static std::shared_ptr<BaseStrictObject> int__new__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> instType,
      std::shared_ptr<BaseStrictObject> value = nullptr);

  static std::shared_ptr<BaseStrictObject> int__pow__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs,
      std::shared_ptr<BaseStrictObject> mod = nullptr);

  static std::shared_ptr<BaseStrictObject> int__rpow__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs,
      std::shared_ptr<BaseStrictObject> mod = nullptr);

  static std::shared_ptr<BaseStrictObject> int__divmod__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__rdivmod__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__add__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__and__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__floordiv__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__lshift__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__mod__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__mul__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__or__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__rshift__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__sub__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__truediv__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__xor__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__radd__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__rand__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rfloordiv__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rlshift__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rmod__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rmul__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__ror__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rrshift__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rsub__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rtruediv__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__rxor__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> int__pos__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__neg__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__invert__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__eq__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__ne__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__lt__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__le__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__gt__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> int__ge__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  std::optional<int_type> getValue() const {
    return value_;
  }

  int_type getValueOr(int_type defaultValue) const {
    return value_.value_or(defaultValue);
  }

 protected:
  std::optional<int_type> value_;
  mutable Ref<> pyValue_;
  mutable std::string displayName_;
};

class StrictIntType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual Ref<> getPyObject() const override;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

class StrictBool : public StrictInt {
 public:
  using StrictInt::StrictInt;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  bool getValue() const {
    return value_ != 0;
  }

  static std::shared_ptr<BaseStrictObject> boolFromPyObj(
      Ref<> pyObj,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> boolOrNotImplementedFromPyObj(
      Ref<> pyObj,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> bool__new__(
      std::shared_ptr<StrictBool> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> instType,
      std::shared_ptr<BaseStrictObject> value = nullptr);
};

class StrictBoolType : public StrictIntType {
 public:
  using StrictIntType::StrictIntType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual Ref<> getPyObject() const override;
  virtual bool isBaseType() const override;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};

// Float
class StrictFloat : public StrictNumeric {
 public:
  StrictFloat(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      double value);

  StrictFloat(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      double value);

  StrictFloat(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      PyObject* pyValue // constructor will incref on the object
  );

  virtual bool isOverflow() const override;
  virtual size_t hash() const override;
  virtual std::optional<double> getReal() const override;

  virtual Ref<> getPyObject() const override;
  virtual std::string getDisplayName() const override;

  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> float__bool__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> float__new__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> instType,
      std::shared_ptr<BaseStrictObject> value = nullptr);

  static std::shared_ptr<BaseStrictObject> float__str__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> float__abs__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> float__round__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> ndigit = nullptr);

  static std::shared_ptr<BaseStrictObject> float__pow__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs,
      std::shared_ptr<BaseStrictObject> mod = nullptr);

  static std::shared_ptr<BaseStrictObject> float__rpow__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs,
      std::shared_ptr<BaseStrictObject> mod = nullptr);

  static std::shared_ptr<BaseStrictObject> float__divmod__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__rdivmod__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__add__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__floordiv__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__mod__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__mul__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__sub__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__truediv__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__radd__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__rfloordiv__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__rmod__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__rmul__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__rsub__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__rtruediv__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> lhs);

  static std::shared_ptr<BaseStrictObject> float__pos__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> float__neg__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> float__eq__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__ne__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__lt__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__le__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__gt__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  static std::shared_ptr<BaseStrictObject> float__ge__(
      std::shared_ptr<StrictFloat> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  double getValue() const {
    return value_;
  }

 protected:
  double value_;
  mutable Ref<> pyValue_;
  mutable std::string displayName_;
};

class StrictFloatType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::weak_ptr<StrictModuleObject> caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual Ref<> getPyObject() const override;

  virtual void addMethods() override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;
};
} // namespace strictmod::objects
