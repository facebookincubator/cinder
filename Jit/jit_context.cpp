#include "Jit/jit_context.h"

#include "switchboard.h"

#include "Jit/capsule.h"
#include "Jit/codegen/gen_asm.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"

/*
 * This contains a mapping of jit compiled functions to the _PyJITContext that
 * was used to compile them. This ensures that the jit context (and associated
 * executable memory) is around as long as the function is.
 *
 * The key is a weakref to the function. We're using this map to avoid extending
 * PyFuncObject.
 */
static PyObject* g_compiled_funcs = NULL;

/*
 * This contains a mapping of types with jit compiled slots to the
 * _PyJITContext that was used to compile them. This ensures that the jit
 * context (and associated executable memory) is around as long as the type
 * is.
 *
 * The key is a weakref to the type. We're using this map to avoid extending
 * PyTypeObject.
 */
static PyObject* g_compiled_types = NULL;

typedef enum { DEOPT_INFO_KIND_FUNC, DEOPT_INFO_KIND_TYPE } DeoptInfoKind;

/* Deoptimization information for a compiled object. */
typedef struct {
  PyObject_HEAD

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

static void deopt_func(PyObject* func_ref);
static void deopt_type(PyObject* type_ref);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PyTypeObject _PyJITContext_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "JITContext",
    .tp_basicsize = sizeof(_PyJITContext),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)_PyJITContext_Free,
    .tp_flags = Py_TPFLAGS_DEFAULT,
};
#pragma GCC diagnostic pop

int _PyJITContext_Init() {
  g_compiled_funcs = PyDict_New();
  if (g_compiled_funcs == NULL) {
    return -1;
  }

  g_compiled_types = PyDict_New();
  if (g_compiled_types == NULL) {
    return -1;
  }

  if (PyType_Ready(&DeoptInfo_Type) < 0) {
    return -1;
  }

  return PyType_Ready(&_PyJITContext_Type);
}

void _PyJITContext_Finalize() {
  JIT_DLOG("Finalizing JIT");
  /* De-optimize any remaining compiled functions */
  PyObject* funcs = PyDict_Keys(g_compiled_funcs);
  if (funcs != NULL) {
    Py_ssize_t num_funcs = PyList_Size(funcs);
    JIT_DLOG("De-optimizing %ld remaining functions", num_funcs);
    for (Py_ssize_t i = 0; i < num_funcs; i++) {
      PyObject* ref = PyList_GetItem(funcs, i);
      deopt_func(ref);
    }
  }
  Py_CLEAR(g_compiled_funcs);

  /* De-optimize any remaining compiled functions */
  PyObject* types = PyDict_Keys(g_compiled_types);
  if (types != NULL) {
    Py_ssize_t num_types = PyList_Size(types);
    JIT_DLOG("De-optimizing %ld remaining types", num_types);
    for (Py_ssize_t i = 0; i < num_types; i++) {
      PyObject* ref = PyList_GetItem(types, i);
      deopt_type(ref);
    }
  }
  Py_CLEAR(g_compiled_types);

  jit::codegen::NativeGenerator::runtime()->releaseReferences();
}

_PyJITContext* _PyJITContext_New(jit::Compiler* compiler) {
  _PyJITContext* ctx = PyObject_New(_PyJITContext, &_PyJITContext_Type);
  if (ctx == NULL) {
    return NULL;
  }

  ctx->code_gen = CodeGen_New();
  if (ctx->code_gen == NULL) {
    Py_DECREF(ctx);
    return NULL;
  }

  ctx->jit_compiler = compiler;
  ctx->jit_functions = PyDict_New();
  if (ctx->jit_functions == NULL) {
    Py_DECREF(ctx);
    return NULL;
  }

  ctx->deopt_info = PyDict_New();
  if (ctx->deopt_info == NULL) {
    Py_DECREF(ctx);
    return NULL;
  }

  return ctx;
}

static void deopt_type_on_type_change(
    PyObject* /* handle */,
    PyObject* /* arg */,
    PyObject* watched) {
  deopt_type(watched);
}

static void deopt_type(PyObject* type_ref) {
  PyObject* type = PyWeakref_GetObject(type_ref);
  const char* name = "<gone>";
  if (type != Py_None) {
    JIT_DCHECK(PyType_Check(type), "can only deoptimize types");
    name = ((PyTypeObject*)type)->tp_name;
  }

  JIT_DLOG("Deoptimizing type name=%s type=%p ref=%p", name, type, type_ref);

  _PyJITContext* ctx =
      (_PyJITContext*)PyDict_GetItem(g_compiled_types, type_ref);
  if (ctx == NULL) {
    return;
  }

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
  if (PyDict_DelItem(ctx->deopt_info, type_ref) < 0) {
    JIT_LOG("failed unsubscribing type %p (%s)", type, name);
  }

  /* Record the that the type is no longer jit compiled */
  if (PyDict_DelItem(g_compiled_types, type_ref) < 0) {
    JIT_LOG("failed unregistering type %p (%s)", type, name);
  }
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
    _PyJITContext* ctx,
    PyTypeObject* type,
    _PyJIT_TypeSlots* slots) {
  PyObject* type_ref = PyWeakref_NewRef((PyObject*)type, NULL);
  if (type_ref == NULL) {
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Set up dependency tracking for deoptimization */
  PyObject* deopt_info = DeoptInfo_NewForType(type);
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
    Py_DECREF(deopt_info);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_str */
  _Py_IDENTIFIER(__str__);
  res = specialize_tp_reprfunc(
      ctx, type, &type->tp_str, &PyId___str__, slots->tp_str);
  if (check_result(&ok_count, res) < 0) {
    Py_DECREF(deopt_info);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_call */
  res = specialize_generic_tp_call(ctx, type, slots->tp_call);
  if (check_result(&ok_count, res) < 0) {
    Py_DECREF(deopt_info);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_descr_get */
  res = specialize_tp_descr_get(ctx, type, slots->tp_descr_get);
  if (check_result(&ok_count, res) < 0) {
    Py_DECREF(deopt_info);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Specialize tp_getattro */
  res = specialize_tp_getattr(ctx, type, slots->tp_getattro);
  if (check_result(&ok_count, res) < 0) {
    Py_DECREF(deopt_info);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  if (ok_count > 0) {
    /* Subscribe to changes to the type */
    if (DeoptInfo_AddTypeSubscr(
            deopt_info, (PyObject*)type, deopt_type_on_type_change, NULL) < 0) {
      Py_DECREF(deopt_info);
      return PYJIT_RESULT_UNKNOWN_ERROR;
    }

    /* Mark the type as jit compiled */
    if (PyDict_SetItem(ctx->deopt_info, type_ref, deopt_info) < 0) {
      Py_DECREF(deopt_info);
      return PYJIT_RESULT_UNKNOWN_ERROR;
    }
    if (PyDict_SetItem(g_compiled_types, type_ref, (PyObject*)ctx) < 0) {
      PyDict_DelItem(ctx->deopt_info, type_ref);
      Py_DECREF(deopt_info);
      return PYJIT_RESULT_UNKNOWN_ERROR;
    }
  }

  Py_DECREF(type_ref);
  Py_DECREF(deopt_info);
  return ok_count > 0 ? PYJIT_RESULT_OK : PYJIT_RESULT_CANNOT_SPECIALIZE;
}

static void deopt_func_on_func_change(
    PyObject* /* handle */,
    PyObject* /* arg */,
    PyObject* watched) {
  deopt_func(watched);
}

static void deopt_func_on_type_change(
    PyObject* /* handle */,
    PyObject* arg,
    PyObject* /* watched */) {
  deopt_func(arg);
}

int _PyJITContext_AddTypeDependency(
    _PyJITContext* ctx,
    PyFunctionObject* func,
    PyObject* type) {
  PyObject* func_ref = PyWeakref_NewRef((PyObject*)func, NULL);
  if (func_ref == NULL) {
    return -1;
  }

  PyObject* deopt_info = PyDict_GetItem(ctx->deopt_info, func_ref);
  if (deopt_info == NULL) {
    Py_DECREF(func_ref);
    return -1;
  }
  Py_INCREF(deopt_info);

  int result = 0;
  if (DeoptInfo_AddTypeSubscr(
          deopt_info, type, deopt_func_on_type_change, func_ref) < 0) {
    result = -1;
  }

  Py_DECREF(deopt_info);
  Py_DECREF(func_ref);

  return result;
}

static void deopt_func(PyObject* func_ref) {
  /* Reset the entry point back to the interpreter if the function still
   * exists*/
  PyObject* func = PyWeakref_GetObject(func_ref);
  if (func != Py_None) {
    JIT_DCHECK(PyFunction_Check(func), "can only deoptimize functions");
    PyEntry_init((PyFunctionObject*)func);
  }

  /*
   * NB: Any failures below are OK. We will leak resources but the correct
   * version of the function will execute.
   *
   */
  _PyJITContext* ctx =
      (_PyJITContext*)PyDict_GetItem(g_compiled_funcs, func_ref);
  if (ctx == NULL) {
    return;
  }

  /* Unsubscribe from changes to dependencies */
  if (PyDict_DelItem(ctx->deopt_info, func_ref) < 0) {
    JIT_LOG("failed unsubscribing function %p", func);
  }

  /* Record the that the function is no longer jit compiled */
  if (PyDict_DelItem(g_compiled_funcs, func_ref) < 0) {
    JIT_LOG("failed unregistering function %p", func);
  }
}

_PyJIT_Result _PyJITContext_CompileFunction(
    _PyJITContext* ctx,
    PyFunctionObject* func) {
  if (ctx->jit_compiler == NULL) {
    return PYJIT_RESULT_OK;
  }
  auto code = (PyCodeObject*)func->func_code;
  auto required_flags = CO_OPTIMIZED | CO_NEWLOCALS;
  auto prohibited_flags = CO_SUPPRESS_JIT;
  if (!_PyJIT_CompileNested()) {
    prohibited_flags |= CO_NESTED;
  }
  // don't care flags: CO_NOFREE, CO_FUTURE_* (the only still-relevant future is
  // "annotations" which doesn't impact bytecode execution.)
  if (code == NULL || ((code->co_flags & required_flags) != required_flags) ||
      (code->co_flags & prohibited_flags)
      /* TODO(TT53254552) - Consider compiling nested functions (e.g. lambdas)
       */
  ) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  } else if (!_PyJIT_OnJitList(func)) {
    return PYJIT_RESULT_CANNOT_SPECIALIZE;
  }

  /*
   * This is the funnel for compiling Python functions.
   *
   * The basic flow is:
   *
   *   1. Pattern match on bytecode in the code object contained by func
   *      to see if we have a specialized routine capable of compiling it.
   *   2. If not, invoke the general purpose compiler.
   *
   */
  jit::CompiledFunction* compiled =
      ctx->jit_compiler->Compile(reinterpret_cast<PyObject*>(func)).release();
  if (compiled == nullptr) {
    JIT_DLOG("Failed compiling %p", func);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  PyObject* capsule = makeCapsule(compiled);
  if (capsule == nullptr) {
    delete compiled;
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  PyObject* func_ref = PyWeakref_NewRef((PyObject*)func, nullptr);
  if (func_ref == nullptr) {
    Py_DECREF(capsule);
    return PYJIT_RESULT_UNKNOWN_ERROR;
  }

  /* Subscribe to changes to the function so we can de-optimize */
  PyObject* deopt_info = DeoptInfo_New();
  if (deopt_info == nullptr) {
    goto error;
  }
  if (DeoptInfo_AddFuncSubscr(
          deopt_info, (PyObject*)func, deopt_func_on_func_change, nullptr) <
      0) {
    goto error;
  }
  if (PyDict_SetItem(ctx->deopt_info, func_ref, deopt_info) < 0) {
    goto error;
  }

  /* Store a reference to the compiled code */
  if (PyDict_SetItem(ctx->jit_functions, func_ref, capsule) < 0) {
    goto error;
  }

  /* Record that the function is jit compiled */
  if (PyDict_SetItem(g_compiled_funcs, func_ref, (PyObject*)ctx) < 0) {
    goto error;
  }

  func->vectorcall = compiled->entry_point();

  register_pyfunction_debug_symbol(func, compiled);

  Py_DECREF(deopt_info);
  Py_DECREF(func_ref);
  Py_DECREF(capsule);

  return PYJIT_RESULT_OK;

error:
  Py_XDECREF(deopt_info);
  PyDict_DelItem(g_compiled_funcs, func_ref);
  PyDict_DelItem(ctx->jit_functions, func_ref);
  Py_DECREF(func_ref);
  Py_DECREF(capsule);
  return PYJIT_RESULT_UNKNOWN_ERROR;
}

int _PyJITContext_DidCompile(_PyJITContext* ctx, PyObject* func) {
  PyObject* func_ref = PyWeakref_NewRef(func, NULL);
  if (func_ref == NULL) {
    return -1;
  }
  PyObject* result = PyDict_GetItemWithError(ctx->jit_functions, func_ref);
  int st = 1;
  if (result == NULL) {
    st = PyErr_Occurred() ? -1 : 0;
  }
  Py_DECREF(func_ref);
  return st;
}

static jit::CompiledFunction* _PyJitContext_GetJITFunction(
    _PyJITContext* ctx,
    PyObject* func) {
  PyObject* func_ref = PyWeakref_NewRef(func, NULL);
  if (func_ref == NULL) {
    return nullptr;
  }

  PyObject* jitfunc_capsule = PyDict_GetItem(ctx->jit_functions, func_ref);
  if (jitfunc_capsule == NULL) {
    Py_DECREF(func_ref);
    return nullptr;
  }

  auto jitfunc = static_cast<jit::CompiledFunction*>(
      PyCapsule_GetPointer(jitfunc_capsule, NULL));

  Py_DECREF(func_ref);
  return jitfunc;
}

int _PyJITContext_GetCodeSize(_PyJITContext* ctx, PyObject* func) {
  auto jitfunc = _PyJitContext_GetJITFunction(ctx, func);
  if (jitfunc == nullptr) {
    return -1;
  }

  int size = jitfunc->GetCodeSize();
  return size;
}

int _PyJITContext_GetStackSize(_PyJITContext* ctx, PyObject* func) {
  auto jitfunc = _PyJitContext_GetJITFunction(ctx, func);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetStackSize();
}

int _PyJITContext_GetSpillStackSize(_PyJITContext* ctx, PyObject* func) {
  auto jitfunc = _PyJitContext_GetJITFunction(ctx, func);
  if (jitfunc == nullptr) {
    return -1;
  }

  return jitfunc->GetSpillStackSize();
}

PyObject* _PyJITContext_GetCompiledFunctions() {
  PyObject* func_refs = PyDict_Keys(g_compiled_funcs);
  if (func_refs == NULL) {
    return NULL;
  }

  PyObject* funcs = PyList_New(0);
  if (funcs == NULL) {
    Py_DECREF(func_refs);
    return NULL;
  }

  Py_ssize_t num_func_refs = PyList_Size(func_refs);
  for (Py_ssize_t i = 0; i < num_func_refs; i++) {
    PyObject* func = PyWeakref_GetObject(PyList_GetItem(func_refs, i));
    if (func != Py_None) {
      if (PyList_Append(funcs, func) < 0) {
        Py_DECREF(funcs);
        Py_DECREF(func_refs);
        return NULL;
      }
    }
  }
  Py_DECREF(func_refs);
  return funcs;
}

int _PyJITContext_PrintHIR(_PyJITContext* ctx, PyObject* func) {
  PyObject* func_ref = PyWeakref_NewRef(func, NULL);
  if (func_ref == NULL) {
    return -1;
  }
  PyObject* result = PyDict_GetItemWithError(ctx->jit_functions, func_ref);
  Py_DECREF(func_ref);
  int st = 0;
  if (result == NULL) {
    if (PyErr_Occurred()) {
      st = -1;
    }
  } else {
    auto jit_func =
        static_cast<jit::CompiledFunction*>(PyCapsule_GetPointer(result, NULL));
    if (jit_func == nullptr) {
      return -1;
    }
    jit_func->PrintHIR();
  }

  return st;
}

int _PyJITContext_Disassemble(_PyJITContext* ctx, PyObject* func) {
  PyObject* func_ref = PyWeakref_NewRef(func, NULL);
  if (func_ref == NULL) {
    return -1;
  }
  PyObject* result = PyDict_GetItemWithError(ctx->jit_functions, func_ref);
  Py_DECREF(func_ref);
  int st = 0;
  if (result == NULL) {
    if (PyErr_Occurred()) {
      st = -1;
    }
  } else {
    auto jit_func =
        static_cast<jit::CompiledFunction*>(PyCapsule_GetPointer(result, NULL));
    if (jit_func == nullptr) {
      return -1;
    }
    jit_func->Disassemble();
  }

  return st;
}

void _PyJITContext_Free(_PyJITContext* ctx) {
  Py_ssize_t remaining = PyDict_Size(ctx->deopt_info);
  JIT_CHECK(
      remaining == 0,
      "trying to free jit context with %ld live compiled objects",
      remaining);

  Py_CLEAR(ctx->deopt_info);
  Py_CLEAR(ctx->jit_functions);

  if (ctx->code_gen != NULL) {
    CodeGen_Free(ctx->code_gen);
    ctx->code_gen = NULL;
  }

  if (ctx->code_gen != NULL) {
    CodeGen_Free(ctx->code_gen);
    ctx->code_gen = NULL;
  }

  delete ctx->jit_compiler;
  ctx->jit_compiler = nullptr;

  PyObject_Del((PyObject*)ctx);
}
