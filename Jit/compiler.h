// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef JIT_COMPILER_H
#define JIT_COMPILER_H

#include "Python.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/hir/hir.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

namespace jit {

// CompiledFunction contains the native code that was compiled for a python
// function
//
// It is responsible for managing the lifetime of the executable memory and
// binding the lifetime of anything it depends on to it.
class CompiledFunction {
 public:
  CompiledFunction(
      vectorcallfunc entry,
      CodeRuntime* code_runtime,
      int func_size,
      int stack_size,
      int spill_stack_size)
      : entry_point_(entry),
        code_runtime_(code_runtime),
        code_size_(func_size),
        stack_size_(stack_size),
        spill_stack_size_(spill_stack_size) {}

  virtual ~CompiledFunction() {}

  vectorcallfunc entry_point() const {
    return entry_point_;
  }

  PyObject* Invoke(PyObject* func, PyObject** args, Py_ssize_t nargs) {
    return entry_point_(func, args, nargs, NULL);
  }

  virtual void PrintHIR() const;
  virtual void Disassemble() const;

  CodeRuntime* codeRuntime() const {
    return code_runtime_;
  }

  int GetCodeSize() const {
    return code_size_;
  }
  int GetStackSize() const {
    return stack_size_;
  }
  int GetSpillStackSize() const {
    return spill_stack_size_;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CompiledFunction);

  vectorcallfunc entry_point_;
  CodeRuntime* code_runtime_;
  int code_size_;
  int stack_size_;
  int spill_stack_size_;
};

// same as CompiledFunction class but keeps HIR and LIR classes for debug
// purposes
class CompiledFunctionDebug : public CompiledFunction {
 public:
  CompiledFunctionDebug(
      vectorcallfunc entry,
      CodeRuntime* code_runtime,
      int func_size,
      int stack_size,
      int spill_stack_size,
      std::unique_ptr<hir::Function> irfunc,
      std::unique_ptr<codegen::NativeGenerator> ngen)
      : CompiledFunction(
            entry,
            code_runtime,
            func_size,
            stack_size,
            spill_stack_size),
        irfunc_(std::move(irfunc)),
        ngen_(std::move(ngen)) {}

  void Disassemble() const override;
  void PrintHIR() const override;

 private:
  std::unique_ptr<hir::Function> irfunc_;
  std::unique_ptr<codegen::NativeGenerator> ngen_;
};

// Compiler is the high-level interface for translating Python functions into
// native code.
//
// NB: This API will likely change quite a bit. It's currently just enough to
// enable us to start testing in Instalab.
class Compiler {
 public:
  Compiler() = default;

  // Compile the given code object, with the given globals dict. The fullname
  // string is only used for internal debugging and logging and does not affect
  // the generated code.
  std::unique_ptr<CompiledFunction> Compile(
      BorrowedRef<PyCodeObject> code,
      BorrowedRef<PyDictObject> globals,
      const std::string& fullname);

  // Convenience wrapper to extract the code, globals, and fullname from a
  // PyFunctionObject.
  std::unique_ptr<CompiledFunction> Compile(BorrowedRef<PyFunctionObject> func);

  static void runPasses(jit::hir::Function& irfunc);

 private:
  DISALLOW_COPY_AND_ASSIGN(Compiler);
  codegen::NativeGeneratorFactory ngen_factory_;
};

} // namespace jit
#endif // JIT_COMPILER_H
