// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/preload.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

#include <utility>

namespace jit {

// CompiledFunction contains the native code that was compiled for a python
// function
//
// It is responsible for managing the lifetime of the executable memory and
// binding the lifetime of anything it depends on to it.
class CompiledFunction {
 public:
  CompiledFunction(
      vectorcallfunc vectorcall_entry,
      void* static_entry,
      CodeRuntime* code_runtime,
      int func_size,
      int stack_size,
      int spill_stack_size,
      hir::Function::InlineFunctionStats inline_function_stats,
      const hir::OpcodeCounts& hir_opcode_counts)
      : vectorcall_entry_(vectorcall_entry),
        static_entry_(static_entry),
        code_runtime_(code_runtime),
        code_size_(func_size),
        stack_size_(stack_size),
        spill_stack_size_(spill_stack_size),
        inline_function_stats_(std::move(inline_function_stats)),
        hir_opcode_counts_(hir_opcode_counts) {}

  virtual ~CompiledFunction() {}

  vectorcallfunc vectorcallEntry() const {
    return vectorcall_entry_;
  }

  void* staticEntry() const {
    return static_entry_;
  }

  PyObject* invoke(PyObject* func, PyObject** args, Py_ssize_t nargs) const {
    return vectorcall_entry_(func, args, nargs, NULL);
  }

  virtual void printHIR() const;
  virtual void disassemble() const;

  CodeRuntime* codeRuntime() const {
    return code_runtime_;
  }

  int codeSize() const {
    return code_size_;
  }
  int stackSize() const {
    return stack_size_;
  }
  int spillStackSize() const {
    return spill_stack_size_;
  }
  const hir::Function::InlineFunctionStats& inlinedFunctionsStats() const {
    return inline_function_stats_;
  }
  const hir::OpcodeCounts& hirOpcodeCounts() const {
    return hir_opcode_counts_;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CompiledFunction);

  vectorcallfunc const vectorcall_entry_;
  void* const static_entry_;
  CodeRuntime* const code_runtime_;
  const int code_size_;
  const int stack_size_;
  const int spill_stack_size_;
  hir::Function::InlineFunctionStats inline_function_stats_;
  hir::OpcodeCounts hir_opcode_counts_;
};

// same as CompiledFunction class but keeps HIR and LIR classes for debug
// purposes
class CompiledFunctionDebug : public CompiledFunction {
 public:
  template <typename... Args>
  CompiledFunctionDebug(
      std::unique_ptr<hir::Function> irfunc,
      std::unique_ptr<codegen::NativeGenerator> ngen,
      Args&&... args)
      : CompiledFunction(std::forward<Args>(args)...),
        irfunc_(std::move(irfunc)),
        ngen_(std::move(ngen)) {}

  void disassemble() const override;
  void printHIR() const override;

 private:
  std::unique_ptr<hir::Function> irfunc_;
  std::unique_ptr<codegen::NativeGenerator> ngen_;
};

typedef std::function<
    void(hir::Function& func, const char* pass_name, std::size_t time_ns)>
    PostPassFunction;

enum PassConfig : uint64_t {
  kDefault = 0,
  kEnableHIRInliner = 1 << 0,
};

// Compiler is the high-level interface for translating Python functions into
// native code.
class Compiler {
 public:
  Compiler() = default;

  // Compile the function / code object preloaded by the given Preloader.
  std::unique_ptr<CompiledFunction> Compile(const hir::Preloader& preloader);

  // Convenience wrapper to create and compile a preloader from a
  // PyFunctionObject.
  std::unique_ptr<CompiledFunction> Compile(BorrowedRef<PyFunctionObject> func);

  // Runs all the compiler passes on the HIR function.
  static void runPasses(hir::Function&, PassConfig config);
  // Runs the compiler passes, calling callback on the HIR function after each
  // pass.
  static void runPasses(
      hir::Function& irfunc,
      PassConfig config,
      PostPassFunction callback);

 private:
  DISALLOW_COPY_AND_ASSIGN(Compiler);
  codegen::NativeGeneratorFactory ngen_factory_;
};

} // namespace jit
