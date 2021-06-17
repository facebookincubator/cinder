// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef HIR_OPTIMIZATION_H
#define HIR_OPTIMIZATION_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "Jit/hir/hir.h"
#include "Jit/hir/type.h"
#include "Jit/util.h"

namespace jit {
namespace hir {

class BasicBlock;
class Environment;
class Function;
class Instr;
class LoadAttr;
class Register;

// Attempt to load the global with the given index, in the context of the given
// globals, builtins, and names. Returns nullptr if the global is not currently
// defined.
PyObject* loadGlobal(
    PyObject* globals,
    PyObject* builtins,
    PyObject* names,
    int name_idx);

class Pass {
 public:
  explicit Pass(const char* name) : name_(name) {}
  virtual ~Pass() {}

  virtual void Run(Function& irfunc) = 0;

  const char* name() const {
    return name_;
  }

 protected:
  const char* name_;
};

using PassFactory = std::function<std::unique_ptr<Pass>()>;

// Inserts incref/decref instructions.
class RefcountInsertion : public Pass {
 public:
  RefcountInsertion() : Pass("RefcountInsertion") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<RefcountInsertion> Factory() {
    return std::make_unique<RefcountInsertion>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RefcountInsertion);
};

// Eliminate Check(Var|Exc|Field) instructions (null checks) when the input is
// known to not be null.
class NullCheckElimination : public Pass {
 public:
  NullCheckElimination() : Pass("NullCheckElimination") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<NullCheckElimination> Factory() {
    return std::make_unique<NullCheckElimination>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NullCheckElimination);
};

class DynamicComparisonElimination : public Pass {
 public:
  DynamicComparisonElimination() : Pass("DynamicComparisonElimination") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<DynamicComparisonElimination> Factory() {
    return std::make_unique<DynamicComparisonElimination>();
  }

 private:
  Instr* ReplaceCompare(Compare* compare, IsTruthy* truthy);
  Instr* ReplaceVectorCall(
      Function& irfunc,
      CondBranch& cond_branch,
      BasicBlock& block,
      VectorCall* vectorcall,
      IsTruthy* truthy);

  void InitBuiltins();

  DISALLOW_COPY_AND_ASSIGN(DynamicComparisonElimination);
  bool inited_builtins_{false};
  PyCFunction isinstance_func_{nullptr};
};

class CallOptimization : public Pass {
 public:
  CallOptimization() : Pass("CallOptimization") {
    type_type_ = Type::fromObject((PyObject*)&PyType_Type);
  }

  void Run(Function& irfunc) override;

  static std::unique_ptr<CallOptimization> Factory() {
    return std::make_unique<CallOptimization>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CallOptimization);
  Type type_type_{TTop};
};

// Eliminate Assign instructions by propagating copies.
class CopyPropagation : public Pass {
 public:
  CopyPropagation() : Pass("CopyPropagation") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<CopyPropagation> Factory() {
    return std::make_unique<CopyPropagation>();
  }
};

// Specialize Load{Attr,Method} instructions into more efficient versions
class LoadAttrSpecialization : public Pass {
 public:
  LoadAttrSpecialization() : Pass("LoadAttrSpecialization") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<LoadAttrSpecialization> Factory() {
    return std::make_unique<LoadAttrSpecialization>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(LoadAttrSpecialization);

  BasicBlock* specializeForType(Environment& env, LoadAttr* instr);

  int cache_id_ = 0;
};

// Remove Phis that only have one unique input value (other than their output).
class PhiElimination : public Pass {
 public:
  PhiElimination() : Pass("PhiElimination") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<PhiElimination> Factory() {
    return std::make_unique<PhiElimination>();
  }
};

// Eliminate IntConvert operations whose input is already the desired type.
class RedundantConversionElimination : public Pass {
 public:
  RedundantConversionElimination() : Pass("RedundantConversionElimination") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<RedundantConversionElimination> Factory() {
    return std::make_unique<RedundantConversionElimination>();
  }
};

// Convert LoadTupleItem to LoadConst if the tuple is a constant
class LoadConstTupleItemOptimization : public Pass {
 public:
  LoadConstTupleItemOptimization() : Pass("LoadConstTupleItemOptimization") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<LoadConstTupleItemOptimization> Factory() {
    return std::make_unique<LoadConstTupleItemOptimization>();
  }
};

// Convert BinaryOp to LoadArrayItem if the lhs is an exact list
class SpecializeBinarySubscrList : public Pass {
 public:
  SpecializeBinarySubscrList() : Pass("SpecializeBinarySubscrList") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<SpecializeBinarySubscrList> Factory() {
    return std::make_unique<SpecializeBinarySubscrList>();
  }
};

class PassRegistry {
 public:
  PassRegistry();

  std::unique_ptr<Pass> MakePass(const std::string& name);

 private:
  DISALLOW_COPY_AND_ASSIGN(PassRegistry);

  std::unordered_map<std::string, PassFactory> factories_;
};

} // namespace hir
} // namespace jit

#endif
