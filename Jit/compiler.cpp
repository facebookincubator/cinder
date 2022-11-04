// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/compiler.h"

#include "Python.h"

#include "Jit/disassembler.h"
#include "Jit/hir/analysis.h"
#include "Jit/hir/builder.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/preload.h"
#include "Jit/hir/printer.h"
#include "Jit/hir/ssa.h"
#include "Jit/jit_time_log.h"
#include "Jit/log.h"

#include <json.hpp>

#include <chrono>
#include <fstream>

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

struct PassTimer {
  explicit PassTimer() : start(std::chrono::steady_clock::now()) {}

  std::size_t finish() {
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
        .count();
  }

  std::chrono::steady_clock::time_point start;
};

template <typename T>
static void runPass(hir::Function& func, PostPassFunction callback) {
  T pass;
  COMPILE_TIMER(func.compilation_phase_timer,
                pass.name(),
                JIT_LOGIF(
                    g_dump_hir_passes,
                    "HIR for %s before pass %s:\n%s",
                    func.fullname,
                    pass.name(),
                    func);

                PassTimer timer;
                pass.Run(func);
                std::size_t time_ns = timer.finish();
                callback(func, pass.name(), time_ns);

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

                JIT_DCHECK(
                    funcTypeChecks(func, std::cerr),
                    "Function %s failed type checking after pass %s:\n%s",
                    func.fullname,
                    pass.name(),
                    func);)
}

void Compiler::runPasses(jit::hir::Function& irfunc, PassConfig config) {
  PostPassFunction callback = [](hir::Function&, const char*, std::size_t) {};
  runPasses(irfunc, config, callback);
}

void Compiler::runPasses(
    jit::hir::Function& irfunc,
    PassConfig config,
    PostPassFunction callback) {
  // SSAify must come first; nothing but SSAify should ever see non-SSA HIR.
  runPass<jit::hir::SSAify>(irfunc, callback);
  runPass<jit::hir::Simplify>(irfunc, callback);
  runPass<jit::hir::DynamicComparisonElimination>(irfunc, callback);
  runPass<jit::hir::GuardTypeRemoval>(irfunc, callback);
  runPass<jit::hir::PhiElimination>(irfunc, callback);
  if (config & PassConfig::kEnableHIRInliner) {
    runPass<jit::hir::InlineFunctionCalls>(irfunc, callback);
    runPass<jit::hir::Simplify>(irfunc, callback);
    runPass<jit::hir::BeginInlinedFunctionElimination>(irfunc, callback);
  }
  runPass<jit::hir::BuiltinLoadMethodElimination>(irfunc, callback);
  runPass<jit::hir::Simplify>(irfunc, callback);
  runPass<jit::hir::CleanCFG>(irfunc, callback);
  runPass<jit::hir::DeadCodeElimination>(irfunc, callback);
  runPass<jit::hir::CleanCFG>(irfunc, callback);
  // RefcountInsertion must come last
  runPass<jit::hir::RefcountInsertion>(irfunc, callback);
  JIT_LOGIF(
      g_dump_final_hir, "Optimized HIR for %s:\n%s", irfunc.fullname, irfunc);
}

std::unique_ptr<CompiledFunction> Compiler::Compile(
    BorrowedRef<PyFunctionObject> func) {
  JIT_CHECK(PyFunction_Check(func), "Expected PyFunctionObject");
  JIT_CHECK(
      !g_threaded_compile_context.compileRunning(),
      "multi-thread compile must preload first");
  auto preloader = jit::hir::Preloader::getPreloader(func);
  if (!preloader) {
    return nullptr;
  }
  return Compile(*preloader);
}

PassConfig createConfig() {
  PassConfig result{PassConfig::kDefault};
  if (_PyJIT_IsHIRInlinerEnabled()) {
    result = static_cast<PassConfig>(result | PassConfig::kEnableHIRInliner);
  }
  return result;
}

std::unique_ptr<CompiledFunction> Compiler::Compile(
    const jit::hir::Preloader& preloader) {
  const std::string& fullname = preloader.fullname();
  if (!PyDict_CheckExact(preloader.globals())) {
    JIT_DLOG(
        "Refusing to compile %s: globals is a %.200s, not a dict",
        fullname,
        Py_TYPE(preloader.globals())->tp_name);
    return nullptr;
  }

  PyObject* builtins = preloader.builtins();
  if (!PyDict_CheckExact(builtins)) {
    JIT_DLOG(
        "Refusing to compile %s: builtins is a %.200s, not a dict",
        fullname,
        Py_TYPE(builtins)->tp_name);
    return nullptr;
  }
  JIT_DLOG(
      "Compiling %s @ %p",
      fullname,
      reinterpret_cast<void*>(preloader.code().get()));

  std::unique_ptr<CompilationPhaseTimer> compilation_phase_timer{nullptr};

  if (captureCompilationTimeFor(fullname)) {
    compilation_phase_timer = std::make_unique<CompilationPhaseTimer>(fullname);
    compilation_phase_timer->start("Overall compilation");
    compilation_phase_timer->start("Lowering into HIR");
  }

  PassTimer hir_build_timer;
  std::unique_ptr<jit::hir::Function> irfunc(jit::hir::buildHIR(preloader));
  std::size_t hir_build_time_ns = hir_build_timer.finish();
  if (nullptr != compilation_phase_timer) {
    compilation_phase_timer->end();
  }
  if (irfunc == nullptr) {
    JIT_DLOG("Lowering to HIR failed %s", fullname);
    return nullptr;
  }

  if (g_dump_hir) {
    JIT_LOG("Initial HIR for %s:\n%s", fullname, *irfunc);
  }

  if (nullptr != compilation_phase_timer) {
    irfunc->setCompilationPhaseTimer(std::move(compilation_phase_timer));
  }

  PassConfig config = createConfig();
  std::unique_ptr<nlohmann::json> json{nullptr};
  if (g_dump_hir_passes_json != nullptr) {
    // TODO(emacs): For inlined functions, grab the sources from all the
    // different functions inlined.
    json.reset(new nlohmann::json());
    nlohmann::json passes;
    hir::JSONPrinter hir_printer;
    passes.emplace_back(hir_printer.PrintSource(*irfunc));
    passes.emplace_back(hir_printer.PrintBytecode(*irfunc));
    PostPassFunction dump =
        [&hir_printer, &passes](
            hir::Function& func, const char* pass_name, std::size_t time_ns) {
          hir_printer.Print(passes, func, pass_name, time_ns);
        };
    dump(*irfunc, "Initial HIR", hir_build_time_ns);
    COMPILE_TIMER(
        irfunc->compilation_phase_timer,
        "HIR transformations",
        Compiler::runPasses(*irfunc, config, dump))
    (*json)["fullname"] = fullname;
    (*json)["cols"] = passes;
  } else {
    COMPILE_TIMER(
        irfunc->compilation_phase_timer,
        "HIR transformations",
        Compiler::runPasses(*irfunc, config))
  }

  auto ngen = ngen_factory_(irfunc.get());
  if (ngen == nullptr) {
    return nullptr;
  }

  if (g_dump_hir_passes_json != nullptr) {
    ngen->SetJSONOutput(json.get());
  }

  void* entry = nullptr;
  COMPILE_TIMER(
      irfunc->compilation_phase_timer,
      "Native code Generation",
      entry = ngen->GetEntryPoint())
  if (entry == nullptr) {
    JIT_DLOG("Generating native code for %s failed", fullname);
    return nullptr;
  }

  JIT_DLOG("Finished compiling %s", fullname);
  if (nullptr != irfunc->compilation_phase_timer) {
    irfunc->compilation_phase_timer->end();
    irfunc->setCompilationPhaseTimer(nullptr);
  }

  int func_size = ngen->GetCompiledFunctionSize();
  int stack_size = ngen->GetCompiledFunctionStackSize();
  int spill_stack_size = ngen->GetCompiledFunctionSpillStackSize();

  if (g_dump_hir_passes_json != nullptr) {
    std::string filename =
        fmt::format("{}/function_{}.json", g_dump_hir_passes_json, fullname);
    JIT_DLOG("Dumping JSON for %s to %s", fullname, filename);
    std::ofstream json_file;
    json_file.open(
        filename,
        std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    json_file << json->dump() << std::endl;
    json_file.close();
  }

  if (g_debug) {
    irfunc->setCompilationPhaseTimer(nullptr);
    return std::make_unique<CompiledFunctionDebug>(
        reinterpret_cast<vectorcallfunc>(entry),
        ngen->codeRuntime(),
        func_size,
        stack_size,
        spill_stack_size,
        irfunc->num_inlined_functions,
        std::move(irfunc),
        std::move(ngen));
  } else {
    return std::make_unique<CompiledFunction>(
        reinterpret_cast<vectorcallfunc>(entry),
        ngen->codeRuntime(),
        func_size,
        stack_size,
        spill_stack_size,
        irfunc->num_inlined_functions);
  }
}

} // namespace jit
