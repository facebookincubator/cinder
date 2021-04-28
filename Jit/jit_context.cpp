// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/jit_context.h"

#include "switchboard.h"

#include "Jit/capsule.h"
#include "Jit/codegen/gen_asm.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"

typedef enum { DEOPT_INFO_KIND_FUNC, DEOPT_INFO_KIND_TYPE } DeoptInfoKind;

/* Deoptimization information for a compiled object. */
typedef struct {
  PyObject_HEAD;

  /* Specifies whether this is for a function or a type object */
  DeoptInfoKind kind;

  /* Subscriptions for function dependencies */
  PyObject* func_subscrs;

  /* Subscriptions for type dependencies */
  PyObject* type_subscrs;

  /*
   * Original values for compiled type objects. Only valid when kind is
   * DEOPT_INFO_KIND_TYPE
   */
  ternaryfunc orig_tp_call;
  initproc orig_tp_init;
  reprfunc orig_tp_repr;
  reprfunc orig_tp_str;
  getattrofunc orig_tp_getattro;
  descrgetfunc orig_tp_descr_get;
} DeoptInfo;

static void DeoptInfo_Free(PyObject* deopt_info);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject DeoptInfo_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "DeoptInfo",
    .tp_basicsize = sizeof(DeoptInfo),
    .tp_itemsize = 0,
    .tp_dealloc = DeoptInfo_Free,
    .tp_flags = Py_TPFLAGS_DEFAULT,
};
#pragma GCC diagnostic pop

static inline int DeoptInfo_Check(PyObject* obj) {
  return Py_TYPE(obj) == &DeoptInfo_Type;
}

static PyObject* DeoptInfo_New() {
  DeoptInfo* info = PyObject_New(DeoptInfo, &DeoptInfo_Type);
  if (info == NULL) {
    return NULL;
  }

  info->func_subscrs = PyList_New(0);
  if (info->func_subscrs == NULL) {
    Py_DECREF(info);
    return NULL;
  }

  info->type_subscrs = PyList_New(0);
  if (info->type_subscrs == NULL) {
    Py_DECREF(info);
    return NULL;
  }

  return (PyObject*)info;
}

static PyObject* DeoptInfo_NewForType(PyTypeObject* type) {
  DeoptInfo* info = (DeoptInfo*)DeoptInfo_New();
  if (info == NULL) {
    return NULL;
  }
  info->orig_tp_call = type->tp_call;
  info->orig_tp_init = type->tp_init;
  info->orig_tp_repr = type->tp_repr;
  info->orig_tp_str = type->tp_str;
  info->orig_tp_getattro = type->tp_getattro;
  info->orig_tp_descr_get = type->tp_descr_get;
  return (PyObject*)info;
}

static int DeoptInfo_AddFuncSubscr(
    PyObject* deopt_info,
    PyObject* func,
    Switchboard_Callback cb,
    PyObject* arg) {
  Switchboard* sb = (Switchboard*)_PyFunction_GetSwitchboard();
  JIT_DCHECK(sb != NULL, "function switchboard not initialized");
  auto subscr = Ref<>::steal(Switchboard_Subscribe(sb, func, cb, arg));
  if (subscr == NULL) {
    return -1;
  }
  JIT_DCHECK(DeoptInfo_Check(deopt_info), "got incorrect type for deopt_info");
  return PyList_Append(((DeoptInfo*)deopt_info)->func_subscrs, subscr);
}

static int DeoptInfo_AddTypeSubscr(
    PyObject* deopt_info,
    PyObject* type,
    Switchboard_Callback cb,
    PyObject* arg) {
  Switchboard* sb = (Switchboard*)_PyType_GetSwitchboard();
  JIT_DCHECK(sb != NULL, "type switchboard not initialized");
  auto subscr = Ref<>::steal(Switchboard_Subscribe(sb, type, cb, arg));
  if (subscr == NULL) {
    return -1;
  }
  JIT_DCHECK(DeoptInfo_Check(deopt_info), "got incorrect type for deopt_info");
  return PyList_Append(((DeoptInfo*)deopt_info)->type_subscrs, subscr);
}

static void DeoptInfo_Free(PyObject* deopt_info) {
  JIT_DCHECK(DeoptInfo_Check(deopt_info), "got incorrect type for deopt_info");
  DeoptInfo* info = (DeoptInfo*)deopt_info;

  if (info->func_subscrs != NULL) {
    Switchboard* func_sb = (Switchboard*)_PyFunction_GetSwitchboard();
    JIT_CHECK(func_sb != NULL, "function switchboard not set");
    Switchboard_UnsubscribeAll(func_sb, info->func_subscrs);
    Py_CLEAR(info->func_subscrs);
  }

  if (info->type_subscrs != NULL) {
    Switchboard* type_sb = (Switchboard*)_PyType_GetSwitchboard();
    JIT_CHECK(type_sb != NULL, "type switchboard not set");
    Switchboard_UnsubscribeAll(type_sb, info->type_subscrs);
    Py_CLEAR(info->type_subscrs);
  }

  PyObject_Del(deopt_info);
}

static void deopt_func(BorrowedRef<_PyJITContext> ctx, BorrowedRef<> func_ref);
static void deopt_type(BorrowedRef<_PyJITContext> ctx, BorrowedRef<> type_ref);

static void _PyJITContext_Free(_PyJITContext* ctx);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject _PyJITContext_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "JITContext",
    .tp_basicsize = sizeof(_PyJITContext),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)_PyJITContext_Free,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_weaklistoffset = offsetof(_PyJITContext, weakreflist),
};
#pragma GCC diagnostic pop

int _PyJITContext_Init() {
  if (PyType_Ready(&DeoptInfo_Type) < 0) {
    return -1;
  }

  return PyType_Ready(&_PyJITContext_Type);
}

_PyJITContext* _PyJITContext_New(std::unique_ptr<jit::Compiler> compiler) {
  auto ctx_raw = new _PyJITContext();
  auto ctx =
      Ref<_PyJITContext>::steal(PyObject_INIT(ctx_raw, &_PyJITContext_Type));

  ctx->code_gen = CodeGen_New();
  if (ctx->code_gen == nullptr) {
    return nullptr;
  }

  ctx->jit_compiler = std::move(compiler);

  ctx->deopt_info = PyDict_New();
  if (ctx->deopt_info == nullptr) {
    return nullptr;
  }

  ctx->weakreflist = nullptr;

  return ctx.release();
}

void _PyJITContext_Free(_PyJITContext* ctx) {
  JIT_DLOG("Finalizing _PyJITContext");

  if (ctx->weakreflist != nullptr) {
    PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(ctx));
  }

  /* De-optimize any remaining compiled functions or types. */
  auto objs = Ref<>::steal(PyDict_Keys(ctx->deopt_info));
  if (objs != nullptr) {
    Py_ssize_t num_objs = PyList_Size(objs);
    JIT_DLOG("De-optimizing %ld remaining objects", num_objs);
    for (Py_ssize_t i = 0; i < num_objs; i++) {
      PyObject* obj_ref = PyList_GetItem(objs, i);
      PyObject* obj = PyWeakref_GetObject(obj_ref);
      if (obj == Py_None) {
        JIT_LOG("Referent of %p unexpectedly disappeared", obj_ref);
        continue;
      }
      if (PyFunction_Check(obj)) {
        deopt_func(ctx, obj_ref);
      } else {
        JIT_CHECK(
            PyType_Check(obj), "Object of unknown type '%.200s' in deopt_info");
        deopt_type(ctx, obj_ref);
      }
    }
  }

  Py_ssize_t remaining = PyDict_Size(ctx->deopt_info);
  JIT_CHECK(
      remaining == 0,
      "%d live compiled objects remain after attempting to deopt everything",
      remaining);

  Py_CLEAR(ctx->deopt_info);

  if (ctx->code_gen != NULL) {
    CodeGen_Free(ctx->code_gen);
    ctx->code_gen = NULL;
  }

  delete ctx;
}

static void deopt_func_on_func_change(
    PyObject* /* handle */,
    PyObject* arg,
    PyObject* watched) {
  Ref<_PyJITContext> ctx(PyWeakref_GetObject(arg));
  if (ctx == Py_None) {
    JIT_LOG("_PyJITContext unexpectedly disappeared");
    return;
  }
  deopt_func(ctx, watched);
}

static void deopt_func(BorrowedRef<_PyJITContext> ctx, BorrowedRef<> func_ref) {
  // Unsubscribe from changes to dependencies.
  int result = PyDict_DelItem(ctx->deopt_info, func_ref);
  JIT_CHECK(result == 0, "failed unsubscribing function");

  PyObject* func = PyWeakref_GetObject(func_ref);
  if (func != Py_None) {
    JIT_CHECK(PyFunction_Check(func), "Expected function");

    // Reset the entry point back to the interpreter.
    PyEntry_init((PyFunctionObject*)func);
  }
}

static void deopt_type_on_type_change(
    PyObject* /* handle */,
    PyObject* arg,
    PyObject* watched) {
  Ref<_PyJITContext> ctx(PyWeakref_GetObject(arg));
  if (ctx == Py_None) {
    JIT_LOG("_PyJITContext unexpectedly disappeared");
    return;
  }
  deopt_type(ctx, watched);
}

static void deopt_type(BorrowedRef<_PyJITContext> ctx, BorrowedRef<> type_ref) {
  PyObject* type = PyWeakref_GetObject(type_ref);
  const char* name = "<gone>";
  if (type != Py_None) {
    JIT_DCHECK(PyType_Check(type), "can only deoptimize types");
    name = ((PyTypeObject*)type)->tp_name;
  }

  JIT_DLOG("Deoptimizing type name=%s type=%p ref=%p", name, type, type_ref);

  DeoptInfo* deopt_info = (DeoptInfo*)PyDict_GetItem(ctx->deopt_info, type_ref);
  if (deopt_info == NULL) {
    return;
  }

  /* Reset slots back to their original values */
  if (type != Py_None) {
    PyTypeObject* tp = (PyTypeObject*)type;
    tp->tp_call = deopt_info->orig_tp_call;
    tp->tp_init = deopt_info->orig_tp_init;
    tp->tp_repr = deopt_info->orig_tp_repr;
    tp->tp_str = deopt_info->orig_tp_str;
    tp->tp_getattro = deopt_info->orig_tp_getattro;
    tp->tp_descr_get = deopt_info->orig_tp_descr_get;
  }

  /* Unsubscribe from dependencies */
  int result = PyDict_DelItem(ctx->deopt_info, type_ref);
  JIT_CHECK(result == 0, "failed unsubscribing type %p (%s)", type, name);
}

static inline int check_result(int* ok_count, _PyJIT_Result res) {
  if (res == PYJIT_RESULT_OK) {
    (*ok_count)++;
    return 0;
  } else if (res == PYJIT_RESULT_CANNOT_SPECIALIZE) {
    return 0;
  } else {
    return -1;
  }
}

static inline int
lookup_type_function(PyTypeObject* type, _Py_Identifier* id, PyObject** fn) {
  *fn = _PyType_LookupId(type, id);
  if (*fn == NULL || !PyFunction_Check(*fn)) {
    return -1;
  }
  return 0;
}

static _PyJIT_Result specialize_tp_reprfunc(
    _PyJITContext* ctx,
    PyTypeObject* type,
    reprfunc* target,
    _Py_Identifier* id,
    reprfunc original) {
  JIT_DCHECK(original != nullptr, "slot not initialized");

  if (*target != original) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }
  PyObject* fn = NULL;
  if (lookup_type_function(type, id, &fn) < 0) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }
  reprfunc specialized = CodeGen_GenReprFuncSlot(ctx->code_gen, type, fn);
  if (specialized == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  *target = specialized;
  JIT_DLOG(
      "Jitted reprfunc stub for %s at %p that calls %p",
      type->tp_name,
      (void*)specialized,
      (void*)fn);

  return PYJIT_RESULT_OK;
}

static _PyJIT_Result specialize_generic_tp_call(
    _PyJITContext* ctx,
    PyTypeObject* type,
    ternaryfunc slot_tp_call) {
  JIT_DCHECK(slot_tp_call != nullptr, "slot not initialized");

  if (type->tp_call != slot_tp_call) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }
  _Py_IDENTIFIER(__call__);
  PyObject* fn = NULL;
  if (lookup_type_function(type, &PyId___call__, &fn) < 0) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  ternaryfunc specialized = CodeGen_GenCallSlot(ctx->code_gen, type, fn);
  if (specialized == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  type->tp_call = specialized;
  JIT_DLOG(
      "Jitted tp_call stub for %s as %p that calls %p",
      type->tp_name,
      (void*)specialized,
      (void*)fn);

  return PYJIT_RESULT_OK;
}

static _PyJIT_Result specialize_tp_descr_get(
    _PyJITContext* ctx,
    PyTypeObject* type,
    descrgetfunc slot_tp_descr_get) {
  if (type->tp_descr_get != slot_tp_descr_get) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }
  _Py_IDENTIFIER(__get__);
  PyObject* fn = NULL;
  if (lookup_type_function(type, &PyId___get__, &fn) < 0) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  descrgetfunc specialized = CodeGen_GenGetDescrSlot(ctx->code_gen, type, fn);
  if (specialized == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }
  type->tp_descr_get = specialized;
  JIT_DLOG(
      "Jitted tp_descr_get stub for %s as %p that calls %p",
      type->tp_name,
      (void*)specialized,
      (void*)fn);

  return PYJIT_RESULT_OK;
}

_PyJIT_Result specialize_tp_getattr(
    _PyJITContext* ctx,
    PyTypeObject* type,
    getattrofunc slot_tp_getattro) {
  if (type->tp_getattro != slot_tp_getattro) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }
  _Py_IDENTIFIER(__getattr__);
  _Py_IDENTIFIER(__getattribute__);
  PyObject* fn = NULL;
  if (lookup_type_function(type, &PyId___getattr__, &fn) < 0) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  PyObject* getattribute = _PyType_LookupId(type, &PyId___getattribute__);
  PyObject* objgetattribute =
      _PyType_LookupId(&PyBaseObject_Type, &PyId___getattribute__);
  if (getattribute != objgetattribute) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  getattrofunc specialized = CodeGen_GenGetAttrSlot(ctx->code_gen, type, fn);
  if (specialized == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  type->tp_getattro = specialized;
  JIT_DLOG(
      "Jitted tp_getattr stub for %s as %p that calls %p",
      type->tp_name,
      (void*)specialized,
      (void*)fn);

  return PYJIT_RESULT_OK;
}

_PyJIT_Result _PyJITContext_SpecializeType(
    BorrowedRef<_PyJITContext> ctx,
    PyTypeObject* type,
    _PyJIT_TypeSlots* slots) {
  auto type_ref = Ref<>::steal(PyWeakref_NewRef((PyObject*)type, NULL));
  if (type_ref == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Set up dependency tracking for deoptimization */
  auto deopt_info = Ref<>::steal(DeoptInfo_NewForType(type));
  if (deopt_info == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  int ok_count = 0;
  _PyJIT_Result res;

  /* Specialize tp_repr */
  _Py_IDENTIFIER(__repr__);
  res = specialize_tp_reprfunc(
      ctx, type, &type->tp_repr, &PyId___repr__, slots->tp_repr);
  if (check_result(&ok_count, res) < 0) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_str */
  _Py_IDENTIFIER(__str__);
  res = specialize_tp_reprfunc(
      ctx, type, &type->tp_str, &PyId___str__, slots->tp_str);
  if (check_result(&ok_count, res) < 0) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_call */
  res = specialize_generic_tp_call(ctx, type, slots->tp_call);
  if (check_result(&ok_count, res) < 0) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_descr_get */
  res = specialize_tp_descr_get(ctx, type, slots->tp_descr_get);
  if (check_result(&ok_count, res) < 0) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_getattro */
  res = specialize_tp_getattr(ctx, type, slots->tp_getattro);
  if (check_result(&ok_count, res) < 0) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  if (ok_count > 0) {
    auto ctx_ref = Ref<>::steal(PyWeakref_NewRef(ctx, nullptr));
    if (ctx_ref == nullptr) {
      return PYJIT_RESULT_UNKNOWN_ERROR;
    }
    /* Subscribe to changes to the type */
    if (DeoptInfo_AddTypeSubscr(
            deopt_info, (PyObject*)type, deopt_type_on_type_change, ctx_ref) <
        0) {
      return PYJIT_RESULT_UNKNOWN_ERROR;
    }

    /* Mark the type as jit compiled */
    if (PyDict_SetItem(ctx->deopt_info, type_ref, deopt_info) < 0) {
      return PYJIT_RESULT_UNKNOWN_ERROR;
    }
  }

  return ok_count > 0 ? PYJIT_RESULT_OK : PYJIT_RESULT_CANNOT_SPECIALIZE;
}

// Record per-function metadata and set the function's entrypoint.
static _PyJIT_Result finalizeCompiledFunc(
    BorrowedRef<_PyJITContext> ctx,
    BorrowedRef<PyFunctionObject> func,
    const jit::CompiledFunction& compiled) {
  /* Subscribe to changes in the function to deopt it when needed. */
  auto func_ref = Ref<>::steal(PyWeakref_NewRef(func, nullptr));
  auto deopt_info = Ref<>::steal(DeoptInfo_New());
  auto ctx_ref = Ref<>::steal(PyWeakref_NewRef(ctx, nullptr));
  if (func_ref == nullptr || deopt_info == nullptr || ctx_ref == nullptr ||
      DeoptInfo_AddFuncSubscr(
          deopt_info, func, deopt_func_on_func_change, ctx_ref) < 0 ||
      PyDict_SetItem(ctx->deopt_info, func_ref, deopt_info) < 0) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  func->vectorcall = compiled.entry_point();
  return PYJIT_RESULT_OK;
}

_PyJIT_Result _PyJITContext_CompileFunction(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  if (ctx->jit_compiler == nullptr) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  std::string fullname = jit::funcFullname(func);
  BorrowedRef<PyCodeObject> code = func->func_code;
  CompilationKey key{code, func->func_globals};
  auto it = ctx->compiled_codes.find(key);
  if (it != ctx->compiled_codes.end()) {
    JIT_DLOG("Found compiled code for '%s'", fullname);
    return finalizeCompiledFunc(ctx, func, *it->second);
  }

  /*
   * This is the funnel for compiling Python functions.
   *
   * The basic flow is:
   *
   *   1. Check if the code object's flags disqualify it from being compiled.
   *   2. If not, pattern match on bytecode in the code object contained by
   *      func to see if we have a specialized routine capable of compiling it.
   *   3. If not, invoke the general purpose compiler.
   */
  int required_flags = CO_OPTIMIZED | CO_NEWLOCALS;
  int prohibited_flags = CO_SUPPRESS_JIT;
  // Don't care flags: CO_NOFREE, CO_FUTURE_* (the only still-relevant future
  // is "annotations" which doesn't impact bytecode execution.)
  if (code == nullptr ||
      ((code->co_flags & required_flags) != required_flags) ||
      (code->co_flags & prohibited_flags) != 0) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  std::unique_ptr<jit::CompiledFunction> compiled =
      ctx->jit_compiler->Compile(func);
  if (compiled == nullptr) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  register_pycode_debug_symbol(code, fullname.c_str(), compiled.get());

  // Store the compiled code.
  auto pair = ctx->compiled_codes.emplace(key, std::move(compiled));
  JIT_CHECK(pair.second == true, "CompilationKey already present");
  return finalizeCompiledFunc(ctx, func, *pair.first->second);
}

static jit::CompiledFunction* lookupFunction(
    _PyJITContext* ctx,
    BorrowedRef<PyFunctionObject> func) {
  auto func_ref = Ref<>::steal(PyWeakref_NewRef(func, nullptr));
  if (func_ref == nullptr) {
    return nullptr;
  }

  if (PyDict_GetItem(ctx->deopt_info, func_ref) == nullptr) {
    return nullptr;
  }

  CompilationKey key{func->func_code, func->func_globals};
  auto it = ctx->compiled_codes.find(key);
  return it == ctx->compiled_codes.end() ? nullptr : it->second.get();
}

int _PyJITContext_DidCompile(_PyJITContext* ctx, PyObject* func) {
  return lookupFunction(ctx, func) != nullptr;
}

int _PyJITContext_GetCodeSize(_PyJITContext* ctx, PyObject* func) {
  jit::CompiledFunction* jitfunc = lookupFunction(ctx, func);
  if (jitfunc == nullptr) {
    return -1;
  }

  int size = jitfunc->GetCodeSize();
  return size;
}

int _PyJITContext_GetStackSize(_PyJITContext* ctx, PyObject* func) {
  jit::CompiledFunction* jitfunc = lookupFunction(ctx, func);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetStackSize();
}

int _PyJITContext_GetSpillStackSize(_PyJITContext* ctx, PyObject* func) {
  jit::CompiledFunction* jitfunc = lookupFunction(ctx, func);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetSpillStackSize();
}

PyObject* _PyJITContext_GetCompiledFunctions(_PyJITContext* ctx) {
  auto funcs = Ref<>::steal(PyList_New(0));
  if (funcs == nullptr) {
    return nullptr;
  }

  auto keys = Ref<>::steal(PyDict_Keys(ctx->deopt_info));
  if (keys == nullptr) {
    return nullptr;
  }

  Py_ssize_t num_keys = PyList_Size(keys);
  for (Py_ssize_t i = 0; i < num_keys; i++) {
    PyObject* key = PyWeakref_GetObject(PyList_GetItem(keys, i));
    if (!PyFunction_Check(key)) {
      continue;
    }
    if (PyList_Append(funcs, key) < 0) {
      return nullptr;
    }
  }
  return funcs.release();
}

int _PyJITContext_PrintHIR(_PyJITContext* ctx, PyObject* func) {
  jit::CompiledFunction* jit_func = lookupFunction(ctx, func);
  if (jit_func == nullptr) {
    return -1;
  }
  jit_func->PrintHIR();

  return 0;
}

int _PyJITContext_Disassemble(_PyJITContext* ctx, PyObject* func) {
  jit::CompiledFunction* jit_func = lookupFunction(ctx, func);
  if (jit_func == nullptr) {
    return -1;
  }
  jit_func->Disassemble();

  return 0;
}
