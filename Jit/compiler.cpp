// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/compiler.h"

#include "Python.h"

#include "Jit/disassembler.h"
#include "Jit/hir/builder.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/jit_x_options.h"
#include "Jit/log.h"

namespace jit {

ThreadedCompileContext g_threaded_compile_context;

void CompiledFunction::Disassemble() const {
  JIT_CHECK(false, "Disassemble() cannot be called in a release build.");
}

void CompiledFunction::PrintHIR() const {
  JIT_CHECK(false, "PrintHIR() cannot be called in a release build.");
}

void CompiledFunctionDebug::Disassemble() const {
  disassemble(
      reinterpret_cast<const char*>(entry_point()),
      GetCodeSize(),
      reinterpret_cast<vma_t>(entry_point()));
}

void CompiledFunctionDebug::PrintHIR() const {
  jit::hir::HIRPrinter printer;
  printer.Print(*irfunc_.get());
}

template <typename T>
static void runPass(hir::Function& func) {
  T pass;
  JIT_LOGIF(
      g_dump_hir_passes,
      "HIR for %s before pass %s:\n%s",
      func.fullname,
      pass.name(),
      func);
  pass.Run(func);
  JIT_LOGIF(
      g_dump_hir_passes,
      "HIR for %s after pass %s:\n%s",
      func.fullname,
      pass.name(),
      func);

  JIT_DCHECK(
      checkFunc(func, std::cerr),
      "Function %s failed verification after pass %s:\n%s",
      func.fullname,
      pass.name(),
      func);
}

void Compiler::runPasses(jit::hir::Function& irfunc) {
  // SSAify must come first; nothing but SSAify should ever see non-SSA HIR.
  runPass<jit::hir::SSAify>(irfunc);
  runPass<jit::hir::Simplify>(irfunc);
  runPass<jit::hir::LoadAttrSpecialization>(irfunc);
  runPass<jit::hir::DynamicComparisonElimination>(irfunc);
  runPass<jit::hir::CallOptimization>(irfunc);
  runPass<jit::hir::PhiElimination>(irfunc);
  runPass<jit::hir::DeadCodeElimination>(irfunc);
  runPass<jit::hir::RefcountInsertion>(irfunc);

  JIT_LOGIF(
      g_dump_final_hir, "Optimized HIR for %s:\n%s", irfunc.fullname, irfunc);
}

std::unique_ptr<CompiledFunction> Compiler::Compile(
    BorrowedRef<PyFunctionObject> func) {
  JIT_CHECK(PyFunction_Check(func), "Expected PyFunctionObject");
  return Compile(func->func_code, func->func_globals, funcFullname(func));
}

std::unique_ptr<CompiledFunction> Compiler::Compile(
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<PyDictObject> globals,
    const std::string& fullname) {
  JIT_CHECK(PyCode_Check(code), "Expected PyCodeObject");

  if (!PyDict_CheckExact(globals)) {
    JIT_DLOG(
        "Refusing to compile %s: globals is a %.200s, not a dict",
        fullname,
        Py_TYPE(globals)->tp_name);
    return nullptr;
  }

  PyObject* builtins = PyEval_GetBuiltins();
  if (!PyDict_CheckExact(builtins)) {
    JIT_DLOG(
        "Refusing to compile %s: builtins is a %.200s, not a dict",
        fullname,
        Py_TYPE(builtins)->tp_name);
    return nullptr;
  }

  JIT_DLOG("Compiling %s @ %p", fullname, code);
  jit::hir::HIRBuilder hir_builder;
  std::unique_ptr<jit::hir::Function> irfunc(
      hir_builder.BuildHIR(code, globals, fullname));
  if (irfunc == nullptr) {
    JIT_DLOG("Lowering to HIR failed %s", fullname);
    return nullptr;
  }

  if (g_dump_hir) {
    JIT_LOG("Initial HIR for %s:\n%s", fullname, *irfunc);
  }

  Compiler::runPasses(*irfunc);

  auto ngen = ngen_factory_(irfunc.get());
  if (ngen == nullptr) {
    return nullptr;
  }

  auto entry = ngen->GetEntryPoint();
  if (entry == nullptr) {
    JIT_DLOG("Generating native code for %s failed", fullname);
    return nullptr;
  }

  JIT_DLOG("Finished compiling %s", fullname);

  int func_size = ngen->GetCompiledFunctionSize();
  int stack_size = ngen->GetCompiledFunctionStackSize();
  int spill_stack_size = ngen->GetCompiledFunctionSpillStackSize();

  if (g_debug) {
    return std::make_unique<CompiledFunctionDebug>(
        reinterpret_cast<vectorcallfunc>(entry),
        ngen->codeRuntime(),
        func_size,
        stack_size,
        spill_stack_size,
        std::move(irfunc),
        std::move(ngen));
  } else {
    return std::make_unique<CompiledFunction>(
        reinterpret_cast<vectorcallfunc>(entry),
        ngen->codeRuntime(),
        func_size,
        stack_size,
        spill_stack_size);
  }
}

} // namespace jit
