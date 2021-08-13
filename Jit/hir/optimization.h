// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef HIR_OPTIMIZATION_H
#define HIR_OPTIMIZATION_H

#include "Jit/hir/hir.h"
#include "Jit/hir/type.h"
#include "Jit/util.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

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

// Perform a mixed bag of strength-reduction optimizations: remove redundant
// null checks, conversions, loads from compile-time constant containers, etc.
//
// If your optimization requires no global analysis or state and operates on
// one instruction at a time by inspecting its inputs (and anything reachable
// from them), it may be a good fit for Simplify.
class Simplify : public Pass {
 public:
  Simplify() : Pass("Simplify") {}

  void Run(Function& func) override;

  static std::unique_ptr<Simplify> Factory() {
    return std::make_unique<Simplify>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Simplify);
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

// Eliminate instructions whose outputs are not used in a return or by
// other instructions with side-effects
class DeadCodeElimination : public Pass {
 public:
  DeadCodeElimination() : Pass("DeadCodeElimination") {}

  void Run(Function& irfunc) override;

  static std::unique_ptr<DeadCodeElimination> Factory() {
    return std::make_unique<DeadCodeElimination>();
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
