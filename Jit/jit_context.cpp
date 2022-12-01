// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/jit_context.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"

static void deopt_func(_PyJITContext* ctx, BorrowedRef<PyFunctionObject> func) {
  if (ctx->compiled_funcs.erase(func) == 0) {
    return;
  }

  // Reset the entry point.
  func->vectorcall = (vectorcallfunc)PyEntry_LazyInit;
}

_PyJITContext::~_PyJITContext() {
  /* De-optimize any remaining compiled functions. */
  for (auto it = compiled_funcs.begin(); it != compiled_funcs.end();) {
    PyFunctionObject* func = *it;
    ++it;
    deopt_func(this, func);
  }
}

void _PyJITContext_ClearCache(_PyJITContext* ctx) {
  for (auto& entry : ctx->compiled_codes) {
    ctx->orphaned_compiled_codes.emplace_back(std::move(entry.second));
  }
  ctx->compiled_codes.clear();
}

// Record per-function metadata and set the function's entrypoint.
static _PyJIT_Result finalizeCompiledFunc(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func,
    const jit::CompiledFunction& compiled) {
  jit::ThreadedCompileSerialize guard;
  if (!ctx->compiled_funcs.emplace(func).second) {
    // Someone else compiled the function between when our caller checked and
    // called us.
    return PYJIT_RESULT_OK;
  }

  func->vectorcall = compiled.vectorcallEntry();
  jit::Runtime* rt = jit::Runtime::get();
  if (rt->hasFunctionEntryCache(func)) {
    void** indirect = rt->findFunctionEntryCache(func);
    *indirect = compiled.staticEntry();
  }
  return PYJIT_RESULT_OK;
}

namespace {
struct CompilationResult {
  jit::CompiledFunction* compiled;
  _PyJIT_Result result;
};

jit::CompiledFunction* lookupCompiledCode(
    _PyJITContext* ctx,
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<PyDictObject> globals) {
  jit::ThreadedCompileSerialize guard;
  auto it = ctx->compiled_codes.find(CompilationKey{code, globals});
  return it == ctx->compiled_codes.end() ? nullptr : it->second.get();
}

CompilationResult compilePreloader(
    _PyJITContext* ctx,
    const jit::hir::Preloader& preloader) {
  BorrowedRef<PyCodeObject> code = preloader.code();
  BorrowedRef<PyDictObject> globals = preloader.globals();

  int required_flags = CO_OPTIMIZED | CO_NEWLOCALS;
  int prohibited_flags = CO_SUPPRESS_JIT;
  // Don't care flags: CO_NOFREE, CO_FUTURE_* (the only still-relevant future
  // is "annotations" which doesn't impact bytecode execution.)
  if (code == nullptr ||
      ((code->co_flags & required_flags) != required_flags) ||
      (code->co_flags & prohibited_flags) != 0) {
    return {nullptr, PYJIT_RESULT_CANNOT_SPECIALIZE};
  }

  // We maintain a set of compilations that are active in all threads, as well
  // as a per-thread recursion limit (since the JIT can invoke itself to try
  // and statically bind calls).
  static std::unordered_set<CompilationKey> active_compiles;
  static thread_local int compile_depth = 0;
  const int kMaxCompileDepth = 10;
  if (compile_depth == kMaxCompileDepth) {
    return {nullptr, PYJIT_RESULT_RETRY};
  }

  CompilationKey key{code, globals};
  {
    // Attempt to atomically transition the code from "not compiled" to "in
    // progress".
    jit::ThreadedCompileSerialize guard;
    if (jit::CompiledFunction* compiled =
            lookupCompiledCode(ctx, code, globals)) {
      return {compiled, PYJIT_RESULT_OK};
    }
    if (!active_compiles.insert(key).second) {
      return {nullptr, PYJIT_RESULT_RETRY};
    }
  }

  compile_depth++;
  std::unique_ptr<jit::CompiledFunction> compiled =
      ctx->jit_compiler.Compile(preloader);
  compile_depth--;

  jit::ThreadedCompileSerialize guard;
  active_compiles.erase(key);
  if (compiled == nullptr) {
    return {nullptr, PYJIT_RESULT_UNKNOWN_ERROR};
  }

  register_pycode_debug_symbol(
      code, preloader.fullname().c_str(), compiled.get());

  // Store the compiled code.
  auto pair = ctx->compiled_codes.emplace(key, std::move(compiled));
  JIT_CHECK(pair.second == true, "CompilationKey already present");
  return {pair.first->second.get(), PYJIT_RESULT_OK};
}

// Compile the given code object.
//
// Returns the CompiledFunction* and PYJIT_RESULT_OK if successful, or nullptr
// and a failure reason if not.
CompilationResult compileCode(
    _PyJITContext* ctx,
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<PyDictObject> globals,
    const std::string& fullname) {
  JIT_CHECK(
      !jit::g_threaded_compile_context.compileRunning(),
      "multi-thread compile must preload first");
  auto preloader = jit::hir::Preloader::getPreloader(code, globals, fullname);
  if (!preloader) {
    return {nullptr, PYJIT_RESULT_UNKNOWN_ERROR};
  }
  return compilePreloader(ctx, *preloader);
}
} // namespace

_PyJIT_Result _PyJITContext_CompileFunction(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  if (_PyJITContext_DidCompile(ctx, func) == 1) {
    return PYJIT_RESULT_OK;
  }
  BorrowedRef<PyCodeObject> code = func->func_code;
  std::string fullname = jit::funcFullname(func);
  CompilationResult result =
      compileCode(ctx, code, func->func_globals, fullname);
  if (result.compiled == nullptr) {
    return result.result;
  }

  return finalizeCompiledFunc(ctx, func, *result.compiled);
}

_PyJIT_Result _PyJITContext_CompileCode(
    _PyJITContext* ctx,
    BorrowedRef<> module,
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<PyDictObject> globals) {
  std::string fullname = jit::codeFullname(module, code);
  return compileCode(ctx, code, globals, fullname).result;
}

_PyJIT_Result _PyJITContext_CompilePreloader(
    _PyJITContext* ctx,
    const jit::hir::Preloader& preloader) {
  CompilationResult result = compilePreloader(ctx, preloader);
  if (result.compiled == nullptr) {
    return result.result;
  }
  if (preloader.func() != nullptr) {
    return finalizeCompiledFunc(ctx, preloader.func(), *result.compiled);
  }
  return PYJIT_RESULT_OK;
}

_PyJIT_Result _PyJITContext_AttachCompiledCode(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  JIT_DCHECK(
      _PyJITContext_DidCompile(ctx, func) == 0, "Function is already compiled");

  if (jit::CompiledFunction* compiled =
          lookupCompiledCode(ctx, func->func_code, func->func_globals)) {
    return finalizeCompiledFunc(ctx, func, *compiled);
  }
  return PYJIT_RESULT_CANNOT_SPECIALIZE;
}

void _PyJITContext_FuncModified(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  deopt_func(ctx, func);
}

void _PyJITContext_FuncDestroyed(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  ctx->compiled_funcs.erase(func);
}

void _PyJITContext_TypeModified(_PyJITContext*, BorrowedRef<PyTypeObject>) {}

void _PyJITContext_TypeDestroyed(_PyJITContext*, BorrowedRef<PyTypeObject>) {}

int _PyJITContext_DidCompile(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::ThreadedCompileSerialize guard;

  return ctx->compiled_funcs.count(func) != 0;
}

int _PyJITContext_GetCodeSize(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::CompiledFunction* jitfunc =
      lookupCompiledCode(ctx, func->func_code, func->func_globals);
  if (jitfunc == nullptr) {
    return -1;
  }

  int size = jitfunc->GetCodeSize();
  return size;
}

int _PyJITContext_GetStackSize(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::CompiledFunction* jitfunc =
      lookupCompiledCode(ctx, func->func_code, func->func_globals);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetStackSize();
}

int _PyJITContext_GetSpillStackSize(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::CompiledFunction* jitfunc =
      lookupCompiledCode(ctx, func->func_code, func->func_globals);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetSpillStackSize();
}

int _PyJITContext_GetNumInlinedFunctions(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::CompiledFunction* jitfunc =
      lookupCompiledCode(ctx, func->func_code, func->func_globals);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetNumInlinedFunctions();
}

PyObject* _PyJITContext_GetCompiledFunctions(_PyJITContext* ctx) {
  auto funcs = Ref<>::steal(PyList_New(0));
  if (funcs == nullptr) {
    return nullptr;
  }

  for (BorrowedRef<PyFunctionObject> func : ctx->compiled_funcs) {
    if (PyList_Append(funcs, func) < 0) {
      return nullptr;
    }
  }
  return funcs.release();
}

int _PyJITContext_PrintHIR(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::CompiledFunction* jit_func =
      lookupCompiledCode(ctx, func->func_code, func->func_globals);
  if (jit_func == nullptr) {
    return -1;
  }
  jit_func->PrintHIR();

  return 0;
}

int _PyJITContext_Disassemble(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  jit::CompiledFunction* jit_func =
      lookupCompiledCode(ctx, func->func_code, func->func_globals);
  if (jit_func == nullptr) {
    return -1;
  }
  jit_func->Disassemble();

  return 0;
}
