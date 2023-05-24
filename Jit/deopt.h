// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/codegen/x86_64.h"
#include "Jit/hir/hir.h"
#include "Jit/jit_rt.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <cstdint>
#include <vector>

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
  enum class Source : char {
    kLoadMethod,
    kUnknown,
  };
  static const char* sourceName(Source source) {
    switch (source) {
      case Source::kLoadMethod:
        return "LoadMethod";
      case Source::kUnknown:
        return "Unknown";
    }
    JIT_ABORT("Unknown source");
  }
  Source source;

  bool isLoadMethodResult() const {
    return source == Source::kLoadMethod;
  }

  std::string toString() const {
    return fmt::format(
        "{}:{}:{}:{}",
        location.toString(),
        ref_kind,
        value_kind,
        sourceName(source));
  }
};

#define DEOPT_REASONS(X)     \
  X(GuardFailure)            \
  X(YieldFrom)               \
  X(Raise)                   \
  X(RaiseStatic)             \
  X(Reraise)                 \
  X(UnhandledException)      \
  X(UnhandledUnboundLocal)   \
  X(UnhandledUnboundFreevar) \
  X(UnhandledNullField)

enum class DeoptReason : char {
#define REASON(name) k##name,
  DEOPT_REASONS(REASON)
#undef REASON
};

const char* deoptReasonName(DeoptReason reason);

// Deopt metadata that is specific to a particular (shadow) frame whose code
// may have been inlined.
struct DeoptFrameMetadata {
  // Locals + cellvars + freevars. This contains an index into live_values or
  // -1 to indicate that a variable is dead. This is somewhat oddly named in
  // order to maintain the correspondence with the `f_localsplus` field on
  // `PyFrameObject`.
  std::vector<int> localsplus;

  // Index into live_values for each entry in the operand stack.
  std::vector<int> stack;

  jit::hir::BlockStack block_stack;

  // Code object associated with the JIT-compiled inlined function from which
  // this was generated.
  PyCodeObject* code{nullptr};

  // The offset of the next bytecode instruction to execute.
  BCOffset next_instr_offset{0};

  BCOffset instr_offset() const {
    return std::max(
        next_instr_offset - int{sizeof(_Py_CODEUNIT)},
        BCOffset{-int{sizeof(_Py_CODEUNIT)}});
  }
};

// DeoptMetadata captures all the information necessary to reconstruct a
// PyFrameObject when deoptimization occurs.
struct DeoptMetadata {
  // The name index of the unbound local or attribute, if we are deopting
  // because of an undefined value.
  BorrowedRef<> eh_name;

  // All live values
  std::vector<LiveValue> live_values;

  // Stack of inlined frame metadata unwound from the deopting instruction.
  std::vector<DeoptFrameMetadata> frame_meta;

  // Runtime metadata associated with the JIT-compiled function from which this
  // was generated.
  CodeRuntime* code_rt{nullptr};

  // A human-readable description of why this deopt happened.
  const char* descr{nullptr};

  // If not -1, index into live_values for a context-dependent value that is
  // relevant to this deopt event.
  int guilty_value{-1};

  // An identifier that can be used to map back to the guard from which
  // this was generated.
  int nonce{-1};

  // Why we are de-opting
  DeoptReason reason{DeoptReason::kUnhandledException};

  // If part of an inlined function, the depth into the call stack that this
  // code *would* be (1, 2, 3, ...). If not inlined, 0.
  int inline_depth() const {
    return frame_meta.size() - 1;
  }

  const LiveValue& getStackValue(int i, const DeoptFrameMetadata& frame) const {
    return live_values[frame.stack[i]];
  }

  // Returns nullptr if local `i` is dead
  const LiveValue* getLocalValue(int i, const DeoptFrameMetadata& frame) const {
    int v = frame.localsplus[i];
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

  std::string toString() const {
    std::vector<std::string> live_value_strings;
    for (const LiveValue& lv : live_values) {
      live_value_strings.push_back(lv.toString());
    }
    return fmt::format(
        "DeoptMetadata {{ reason={}, descr={}, inline_depth={}, "
        "live_values=<{}> }}",
        deoptReasonName(reason),
        descr,
        inline_depth(),
        fmt::join(live_value_strings, ", "));
  }

  // Construct a `DeoptMetadata` instance from the information in `instr`.
  //
  // `optimizable_lms` contains the set of `LoadMethod` instructions for which
  // we were able to generate optimized code.
  static DeoptMetadata fromInstr(
      const jit::hir::DeoptBase& instr,
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
//
// May return a reference to an object that is relevant to the deopt event. The
// meaning of this object depends on meta.reason.
void reifyFrame(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const uint64_t* regs);

// Like reifyFrame(), but for a suspended generator. Takes a single base
// pointer for spill data rather than a full set of registers.
void reifyGeneratorFrame(
    PyFrameObject* frame,
    const DeoptMetadata& meta,
    const DeoptFrameMetadata& frame_meta,
    const void* base);

// A simple interface for reading the contents of registers + memory
struct MemoryView {
  const uint64_t* regs;

  BorrowedRef<> readBorrowed(const LiveValue& value) const;
  Ref<> readOwned(const LiveValue& value) const;

 private:
  uint64_t readRaw(const LiveValue& value) const {
    jit::codegen::PhyLocation loc = value.location;
    if (loc.is_register()) {
      return regs[loc.loc];
    } else {
      uint64_t rbp = regs[jit::codegen::PhyLocation::RBP];
      // loc.loc is relative offset from RBP (i.e. negative as stack grows down)
      return *(reinterpret_cast<uint64_t*>(rbp + loc.loc));
    }
  }
};

// Release any owned references in the given set of registers or spill data.
void releaseRefs(const DeoptMetadata& meta, const MemoryView& mem);
void releaseRefs(const DeoptMetadata& meta, const void* base);

// Call once per deopt.
Ref<> profileDeopt(
    std::size_t deopt_idx,
    const DeoptMetadata& meta,
    const MemoryView& mem);

} // namespace jit
