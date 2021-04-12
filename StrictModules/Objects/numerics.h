#ifndef __STRICTM_NUMERIC_OBJ_H__
#define __STRICTM_NUMERIC_OBJ_H__

#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/object_type.h"

namespace strictmod::objects {

class StrictNumeric : public StrictInstance {
 public:
  using StrictInstance::StrictInstance;
  virtual double getReal() const = 0;
  virtual double getImaginary() const = 0;
};

class StrictInt : public StrictNumeric {
 public:
  StrictInt(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      long value);

  StrictInt(
      std::shared_ptr<StrictType> type,
      std::shared_ptr<StrictModuleObject> creator,
      long value);

  StrictInt(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      PyObject* pyValue // constructor will incref on the object
  );

  virtual ~StrictInt() {
    Py_XDECREF(pyValue_);
  }

  virtual double getReal() const override;
  virtual double getImaginary() const override;

  virtual bool isHashable() const override;
  virtual size_t hash() const override;
  virtual bool eq(const BaseStrictObject& other) const override;

  virtual PyObject* getPyObject() const override;
  virtual std::string getDisplayName() const override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> int__bool__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> int__eq__(
      std::shared_ptr<StrictInt> self,
      const CallerContext& caller,
      std::shared_ptr<BaseStrictObject> rhs);

  long getValue() const {
    return value_;
  }

 protected:
  long value_;
  mutable PyObject* pyValue_;
  mutable std::string displayName_;
};

class StrictIntType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::shared_ptr<StrictModuleObject> caller) override;

  virtual PyObject* getPyObject() const override;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual void addMethods() override;
};

class StrictBool : public StrictInt {
 public:
  using StrictInt::StrictInt;

  virtual PyObject* getPyObject() const override;
  virtual std::string getDisplayName() const override;

  bool getValue() const {
    return value_ != 0;
  }
};

class StrictBoolType : public StrictIntType {
 public:
  using StrictIntType::StrictIntType;

  virtual std::unique_ptr<BaseStrictObject> constructInstance(
      std::shared_ptr<StrictModuleObject> caller) override;

  virtual PyObject* getPyObject() const override;
  virtual bool isBaseType() const override;

  virtual std::shared_ptr<BaseStrictObject> getTruthValue(
      std::shared_ptr<BaseStrictObject> obj,
      const CallerContext& caller) override;

  virtual void addMethods() override;
};

} // namespace strictmod::objects

#endif
