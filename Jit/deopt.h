// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __JIT_DEOPT_H__
#define __JIT_DEOPT_H__

#include <cstdint>
#include <vector>

#include "Jit/codegen/x86_64.h"
#include "Jit/hir/hir.h"

#include "Python.h"

namespace jit {

class CodeRuntime;

// Return the ValueKind to use for a value with the given Type.
hir::ValueKind deoptValueKind(hir::Type type);

// LiveValue contains metadata about a live value at a specific point in a
// JIT-compiled function.
struct LiveValue {
  jit::codegen::PhyLocation location;
  jit::hir::RefKind ref_kind;
  jit::hir::ValueKind value_kind;

  // The LOAD_METHOD opcode leaves the stack in one of two forms depending
  // on the type of the receiver and the result of the method lookup:
  //
  // <method>              NULL
  // <receiver>     or     <bound method or something else>
  //
  // Unfortunately, our HIR does not yet have support for multiple outputs,
  // so we leave the abstract stack in the following form and rely on backend
  // optimizations to avoid constructing bound method objects.
  //
  // <receiver>
  // <callable>
  //
  // During deoptimization we need to translate this stack layout into the
  // form expected by the interpreter. To do so, we tag the `LiveValue` for
  // the stack slot that contains `<callable>` with this field.
  enum class Source {
    kOptimizableLoadMethod,
    kUnoptimizableLoadMethod,
    kUnknown,
  };
  Source source;

  bool isLoadMethodResult() const {
    return (source == Source::kOptimizableLoadMethod) ||
        (source == Source::kUnoptimizableLoadMethod);
  }
};

#define DEOPT_REASONS(X)   \
  X(GuardFailure)          \
  X(Raise)                 \
  X(RaiseStatic)           \
  X(Reraise)               \
  X(UnhandledException)    \
  X(UnhandledUnboundLocal) \
  X(UnhandledNullField)    \
  X(UnhandledNone)

enum class DeoptReason {
#define REASON(name) k##name,
  DEOPT_REASONS(REASON)
#undef REASON
};

const char* deoptReasonName(DeoptReason reason);

enum class DeoptAction {
  kResumeInInterpreter,
  kUnwind,
};

// DeoptMetadata captures all the information necessary to reconstruct a
// PyFrameObject when deoptimization occurs.
struct DeoptMetadata {
  // Why we are de-opting
  DeoptReason reason{DeoptReason::kUnhandledException};

  // What to do when we de-opt
  DeoptAction action{DeoptAction::kUnwind};

  // The name index of the unbound local, if we are deopting because of an
  // UnboundLocalError.
  int eh_name_index{-1};

  // All live values
  std::vector<LiveValue> live_values;

  // Locals + cellvars + freevars. This contains an index into live_values or
  // -1 to indicate that a variable is dead. This is somewhat oddly named in
  // order to maintain the correspondence with the `f_localsplus` field on
  // `PyFrameObject`.
  std::vector<int> localsplus;

  // Index into live_values for each entry in the operand stack.
  std::vector<int> stack;

  // If not -1, index into live_values for the value most directly responsible
  // for this deopt. Used for tracking deopt reasons.
  int guilty_value{-1};

  jit::hir::BlockStack block_stack;

  // The offset of the next bytecode instruction to execute.
  int next_instr_offset{0};

  // An identifier that can be used to map back to the guard from which
  // this was generated.
  int nonce{-1};

  // Runtime metadata associated with the JIT-compiled function from which this
  // was generated.
  CodeRuntime* code_rt{nullptr};

  // A human-readable description of why this deopt happened.
  const char* descr{nullptr};

  const LiveValue& getStackValue(int i) const {
    return live_values[stack[i]];
  }

  // Returns nullptr if local `i` is dead
  const LiveValue* getLocalValue(int i) const {
    int v = localsplus[i];
    if (v == -1) {
      return nullptr;
    }
    return &live_values[v];
  }

  // Returns nullptr if there is no guilty value.
  const LiveValue* getGuiltyValue() const {
    if (guilty_value == -1) {
      return nullptr;
    }
    return &live_values[guilty_value];
  }

  // Construct a `DeoptMetadata` instance from the information in `instr`.
  //
  // `optimizable_lms` contains the set of `LoadMethod` instructions for which
  // we were able to generate optimized code.
  static DeoptMetadata fromInstr(
      const jit::hir::DeoptBase& instr,
      const std::unordered_set<const jit::hir::Instr*>& optimizable_lms,
      CodeRuntime* code_rt);
};

// Update `frame` so that execution can resume in the interpreter.
//
// `deopt_idx` is the index of `meta` in the Runtime's list of
// DeoptMetadatas. It may be size_t(-1) to indicate that `meta` is transient
// and not in Runtime's list.
//
// The `regs` argument contains the values of all general purpose registers,
// in the same order as they appear in `jit::codegen::PhyLocation`.
//
// After this function is called, ownership of all references specified by
// deopt_meta have been transferred to `frame`.
//
// We expect `frame` to already have `globals`, `code`, and `builtins`
// initialized.
void reifyFrame(
    PyFrameObject* frame,
    std::size_t deopt_idx,
    const DeoptMetadata& meta,
    const uint64_t* regs);

} // namespace jit

#endif
