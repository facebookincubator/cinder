// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "cinderx/StrictModules/Objects/instance.h"
#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/Objects/signature.h"
#include "cinderx/StrictModules/analyzer.h"

namespace strictmod::compiler {
class ModuleLoader;
}

namespace strictmod::objects {

class StrictFuncType;

class FunctionReturnException {
 public:
  FunctionReturnException(std::shared_ptr<BaseStrictObject> val);

  std::shared_ptr<BaseStrictObject> getVal() {
    return val_;
  }

 private:
  std::shared_ptr<BaseStrictObject> val_;
};

class YieldReachedException {};

class StrictFunction : public StrictInstance {
 public:
  friend class StrictFuncType;
  StrictFunction(
      std::shared_ptr<StrictType> type,
      std::weak_ptr<StrictModuleObject> creator,
      std::string funcName,
      std::string qualName,
      int lineno,
      int col,
      std::vector<stmt_ty> body,
      EnvT closure,
      SymtableEntry symbols,
      std::vector<std::string> posonlyArgs,
      std::vector<std::string> posArgs,
      std::vector<std::string> kwonlyArgs,
      std::optional<std::string> varArg,
      std::optional<std::string> kwVarArg,
      std::vector<std::shared_ptr<BaseStrictObject>> posDefaults,
      std::vector<std::shared_ptr<BaseStrictObject>> kwDefaults,
      compiler::ModuleLoader* loader,
      std::string fileName,
      std::string modName,
      std::optional<std::string> doc,
      std::shared_ptr<BaseStrictObject> annotations,
      bool useFutureAnnotations = true,
      bool isCoroutine = false);

  virtual void cleanContent(const StrictModuleObject* owner) override;
  // accessors
  bool isCoroutine() const {
    return isCoroutine_;
  }

  bool useFutureAnnotations() const {
    return useFutureAnnotations_;
  }

  SymtableEntry getSymtableEntry() const {
    return symbols_;
  }

  const std::vector<stmt_ty>& getBody() const {
    return body_;
  }

  const std::string& getFuncName() const {
    return funcName_;
  }

  const std::string& getModName() const {
    return modName_;
  }

  const FuncSignature& getSignature() const {
    return signature_;
  }

  Analyzer getFuncAnalyzer(
      const CallerContext& caller,
      BaseErrorSink* errorSink);

  virtual std::string getDisplayName() const override;
  virtual std::shared_ptr<BaseStrictObject> copy(
      const CallerContext& caller) override;

  // wrapped methods
  static std::shared_ptr<BaseStrictObject> function__annotations__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static void function__annotations__setter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> function__defaults__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static void function__defaults__setter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<BaseStrictObject> value,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> function__kwdefaults__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

  static std::shared_ptr<BaseStrictObject> function__code__getter(
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller);

 private:
  std::string funcName_;
  std::string qualName_;
  int lineno_; // line and col at which this function is defined
  int col_;

  std::vector<stmt_ty> body_;
  EnvT closure_;
  SymtableEntry symbols_;

  std::vector<std::string> posonlyArgs_;
  std::vector<std::string> posArgs_;
  std::vector<std::string> kwonlyArgs_;
  std::optional<std::string> varArg_;
  std::optional<std::string> kwVarArg_;
  std::vector<std::shared_ptr<BaseStrictObject>> posDefaults_;
  std::vector<std::shared_ptr<BaseStrictObject>>
      kwDefaults_; // same size as kwonlyArgs_

  compiler::ModuleLoader* loader_;
  std::string fileName_;
  std::string modName_; // metadata used by sub analyzer to decide things like
                        // import path
  std::optional<std::string> doc_; // the __doc__ field
  std::optional<std::string> modNameField_; // the __module__ field

  std::shared_ptr<BaseStrictObject> annotations_;

  bool useFutureAnnotations_; // whether function is defined with future
                              // annotations
  bool isCoroutine_;
  FuncSignature signature_;

  std::shared_ptr<BaseStrictObject> kwDefaultsObj_;
  std::shared_ptr<BaseStrictObject> codeObj_;

  void makeCodeObjHelper(const CallerContext& caller);
};

class StrictFuncType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;
  virtual std::shared_ptr<BaseStrictObject> getDescr(
      std::shared_ptr<BaseStrictObject> obj,
      std::shared_ptr<BaseStrictObject> inst,
      std::shared_ptr<StrictType> type,
      const CallerContext& caller) override;

  virtual std::shared_ptr<BaseStrictObject> call(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& argNames,
      const CallerContext& caller) override;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual bool isCallable(const CallerContext& caller) override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;

  virtual void addMethods() override;
};

// async call
class StrictAsyncCall : public StrictInstance {
 public:
  StrictAsyncCall(
      std::weak_ptr<StrictModuleObject> creator,
      std::string funcName);

  virtual std::string getDisplayName() const override;
  // wrapped method
  static std::shared_ptr<BaseStrictObject> asyncCallClose(
      std::shared_ptr<StrictAsyncCall> self,
      const CallerContext& caller);

 private:
  std::string funcName_;
};

class StrictAsyncCallType : public StrictObjectType {
 public:
  using StrictObjectType::StrictObjectType;

  virtual std::shared_ptr<StrictType> recreate(
      std::string name,
      std::weak_ptr<StrictModuleObject> caller,
      std::vector<std::shared_ptr<BaseStrictObject>> bases,
      std::shared_ptr<DictType> members,
      std::shared_ptr<StrictType> metatype,
      bool isImmutable) override;

  virtual bool isBaseType() const override;

  virtual std::vector<std::type_index> getBaseTypeinfos() const override;

  virtual void addMethods() override;
};

} // namespace strictmod::objects
