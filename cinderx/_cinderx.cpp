// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"
#include "cinder/exports.h"
#include "cinder/hooks.h"

#include "cinderx/CachedProperties/cached_properties.h"
#include "cinderx/Common/watchers.h"
#include "cinderx/Interpreter/interpreter.h"
#include "cinderx/Jit/frame.h"
#include "cinderx/Jit/pyjit.h"
#include "cinderx/Jit/pyjit_result.h"
#include "cinderx/Jit/pyjit_typeslots.h"
#include "cinderx/ParallelGC/parallel_gc.h"
#include "cinderx/Shadowcode/shadowcode.h"
#include "cinderx/StaticPython/classloader.h"
#include "cinderx/StaticPython/descrobject_vectorcall.h"
#include "cinderx/StaticPython/methodobject_vectorcall.h"
#include "cinderx/StaticPython/strictmoduleobject.h"
#include "internal/pycore_shadow_frame.h"
#include <dlfcn.h>

/*
 * Misc. Python-facing utility functions.
 */

static PyObject *clear_caches(PyObject *, PyObject *) {
  _PyJIT_ClearDictCaches();
  Py_RETURN_NONE;
}

static PyObject *clear_all_shadow_caches(PyObject *, PyObject *) {
  _PyShadow_FreeAll();
  Py_RETURN_NONE;
}

PyDoc_STRVAR(strict_module_patch_doc, "strict_module_patch(mod, name, value)\n\
Patch a field in a strict module\n\
Requires patching to be enabled");
static PyObject *strict_module_patch(PyObject *, PyObject *args) {
  PyObject *mod;
  PyObject *name;
  PyObject *value;
  if (!PyArg_ParseTuple(args, "OUO", &mod, &name, &value)) {
    return NULL;
  }
  if (Ci_do_strictmodule_patch(mod, name, value) < 0) {
    return NULL;
  }
  Py_RETURN_NONE;
}

PyDoc_STRVAR(strict_module_patch_delete_doc,
             "strict_module_patch_delete(mod, name)\n\
Delete a field in a strict module\n\
Requires patching to be enabled");
static PyObject *strict_module_patch_delete(PyObject *, PyObject *args) {
  PyObject *mod;
  PyObject *name;
  if (!PyArg_ParseTuple(args, "OU", &mod, &name)) {
    return NULL;
  }
  if (Ci_do_strictmodule_patch(mod, name, NULL) < 0) {
    return NULL;
  }
  Py_RETURN_NONE;
}

PyDoc_STRVAR(strict_module_patch_enabled_doc,
             "strict_module_patch_enabled(mod)\n\
Gets whether patching is enabled on the strict module");
static PyObject *strict_module_patch_enabled(PyObject *, PyObject *mod) {
  if (!Ci_StrictModule_Check(mod)) {
    PyErr_SetString(PyExc_TypeError, "expected strict module object");
    return NULL;
  }
  if (Ci_StrictModule_GetDictSetter(mod) != NULL) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyObject *clear_classloader_caches(PyObject *, PyObject *) {
  _PyClassLoader_ClearVtables();
  _PyClassLoader_ClearCache();
  _PyClassLoader_ClearGenericTypes();
  Py_RETURN_NONE;
}

static PyObject *set_profile_interp(PyObject *, PyObject *arg) {
  int is_true = PyObject_IsTrue(arg);
  if (is_true < 0) {
    return NULL;
  }

  PyThreadState *tstate = PyThreadState_Get();
  int old_flag = tstate->profile_interp;
  Ci_ThreadState_SetProfileInterp(tstate, is_true);

  if (old_flag) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyObject *set_profile_interp_all(PyObject *, PyObject *arg) {
  int is_true = PyObject_IsTrue(arg);
  if (is_true < 0) {
    return NULL;
  }
  _PyJIT_SetProfileNewInterpThreads(is_true);
  Ci_ThreadState_SetProfileInterpAll(is_true);

  Py_RETURN_NONE;
}

static PyObject *set_profile_interp_period(PyObject *, PyObject *arg) {
  if (!PyLong_Check(arg)) {
    PyErr_Format(PyExc_TypeError, "Expected int object, got %.200s",
                 Py_TYPE(arg)->tp_name);
    return NULL;
  }
  long val = PyLong_AsLong(arg);
  if (val == -1 && PyErr_Occurred()) {
    return NULL;
  }

  Ci_RuntimeState_SetProfileInterpPeriod(val);
  Py_RETURN_NONE;
}

static PyObject *get_and_clear_type_profiles(PyObject *, PyObject *) {
  PyObject *full_data = _PyJIT_GetAndClearTypeProfiles();
  if (full_data == NULL) {
    return NULL;
  }
  PyObject *profiles = PyDict_GetItemString(full_data, "profile");
  Py_XINCREF(profiles);
  Py_DECREF(full_data);
  return profiles;
}

static PyObject *get_and_clear_type_profiles_with_metadata(PyObject *,
                                                           PyObject *) {
  return _PyJIT_GetAndClearTypeProfiles();
}

static PyObject *clear_type_profiles(PyObject *, PyObject *) {
  _PyJIT_ClearTypeProfiles();
  Py_RETURN_NONE;
}

static PyObject *watch_sys_modules(PyObject *, PyObject *) {
  PyObject *sys = PyImport_ImportModule("sys");
  if (sys == NULL) {
    Py_RETURN_NONE;
  }

  PyObject *modules = PyObject_GetAttrString(sys, "modules");
  Py_DECREF(sys);
  if (modules == NULL) {
    Py_RETURN_NONE;
  }
  Ci_Watchers_WatchDict(modules);
  Py_DECREF(modules);
  Py_RETURN_NONE;
}

PyDoc_STRVAR(cinder_enable_parallel_gc_doc,
             "enable_parallel_gc(min_generation=2, num_threads=0)\n\
\n\
Enable parallel garbage collection for generations >= `min_generation`.\n\
\n\
Use `num_threads` threads to perform collection in parallel. When this value is\n\
0 the number of threads is half the number of processors.\n\
\n\
Calling this more than once has no effect. Call `cinder.disable_parallel_gc()`\n\
and then call this function to change the configuration.\n\
\n\
A ValueError is raised if the generation or number of threads is invalid.");
static PyObject *cinder_enable_parallel_gc(PyObject *, PyObject *args,
                                           PyObject *kwargs) {
  static char *argnames[] = {const_cast<char *>("min_generation"),
                             const_cast<char *>("num_threads"), NULL};

  int min_gen = 2;
  int num_threads = 0;

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ii", argnames, &min_gen,
                                   &num_threads)) {
    return NULL;
  }

  if (min_gen < 0) {
    PyErr_SetString(PyExc_ValueError, "invalid generation");
    return NULL;
  }

  if (num_threads < 0) {
    PyErr_SetString(PyExc_ValueError, "invalid num_threads");
    return NULL;
  }

  if (Cinder_EnableParallelGC(min_gen, num_threads) < 0) {
    return NULL;
  }
  Py_RETURN_NONE;
}

PyDoc_STRVAR(cinder_disable_parallel_gc_doc, "disable_parallel_gc()\n\
\n\
Disable parallel garbage collection.\n\
\n\
This only affects the next collection; calling this from a finalizer does not\n\
affect the current collection.");
static PyObject *cinder_disable_parallel_gc(PyObject *,
                                            PyObject *) {
  Cinder_DisableParallelGC();
  Py_RETURN_NONE;
}

PyDoc_STRVAR(cinder_get_parallel_gc_settings_doc, "get_parallel_gc_settings()\n\
\n\
Return the settings used by the parallel garbage collector or\n\
None if the parallel collector is not enabled.\n\
\n\
Returns a dictionary with the following keys when the parallel\n\
collector is enabled:\n\
\n\
    num_threads: Number of threads used.\n\
    min_generation: The minimum generation for which parallel gc is enabled.");
static PyObject *cinder_get_parallel_gc_settings(PyObject *,
                                                 PyObject *) {
  return Cinder_GetParallelGCSettings();
}

static PyObject*
compile_perf_trampoline_pre_fork(PyObject *, PyObject *) {
    _PyPerfTrampoline_CompilePerfTrampolinePreFork();
    Py_RETURN_NONE;
}

static PyObject*
is_compile_perf_trampoline_pre_fork_enabled(PyObject *, PyObject *) {
    if(_PyPerfTrampoline_IsPreforkCompilationEnabled()) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

typedef struct {
  PyObject *list;
  int hasError;
  int collectFrame;
} StackWalkState;

static CiStackWalkDirective frame_data_collector(void *data, PyObject *fqname,
                                                 PyCodeObject *code, int lineno,
                                                 PyObject *pyframe) {
  PyObject *lineNoObj;
  int failed;

  StackWalkState *state = (StackWalkState *)data;
  if (fqname == NULL) {
    fqname = ((PyCodeObject *)code)->co_qualname;
    if (!fqname || !PyUnicode_Check(fqname)) {
      fqname = ((PyCodeObject *)code)->co_name;
    }
  }
  PyObject *t = PyTuple_New(2 + state->collectFrame);
  if (t == NULL) {
    goto fail;
  }
  lineNoObj = PyLong_FromLong(lineno);
  if (lineNoObj == NULL) {
    Py_DECREF(t);
    goto fail;
  }
  PyTuple_SET_ITEM(t, 0, fqname);
  Py_INCREF(fqname);

  // steals ref
  PyTuple_SET_ITEM(t, 1, lineNoObj);

  if (state->collectFrame) {
    PyObject *o = pyframe;
    if (!o) {
      o = Py_None;
    }
    PyTuple_SET_ITEM(t, 2, o);
    Py_INCREF(o);
  }
  failed = PyList_Append(state->list, t);
  Py_DECREF(t);
  if (!failed) {
    return CI_SWD_CONTINUE_STACK_WALK;
  }
fail:
  state->hasError = 1;
  return CI_SWD_STOP_STACK_WALK;
}

static PyObject *collect_stack(int collectFrame) {
  PyObject *stack = PyList_New(0);
  if (stack == NULL) {
    return NULL;
  }
  StackWalkState state = {
      .list = stack, .hasError = 0, .collectFrame = collectFrame};
  Ci_WalkAsyncStack(PyThreadState_GET(), frame_data_collector, &state);
  if (state.hasError || (PyList_Reverse(stack) != 0)) {
    Py_CLEAR(stack);
  }
  return stack;
}

static PyObject *
get_entire_call_stack_as_qualnames_with_lineno(PyObject *,
                                               PyObject *) {
  return collect_stack(0);
}

static PyObject *get_entire_call_stack_as_qualnames_with_lineno_and_frame(
    PyObject *, PyObject *) {
  return collect_stack(1);
}


/*
 * (De)initialization functions
 */

static void init_already_existing_funcs() {
  PyUnstable_GC_VisitObjects([](PyObject* obj, void*){
    if (PyFunction_Check(obj)) {
      PyEntry_init((PyFunctionObject*)obj);
    }
    return 1;
  }, nullptr);
}

static int override_tp_getset(PyTypeObject *type, PyGetSetDef *tp_getset) {
  type->tp_getset = tp_getset;
  PyGetSetDef *gsp = type->tp_getset;
  PyObject *dict = type->tp_dict;
  for (; gsp->name != NULL; gsp++) {
      PyObject *descr = PyDescr_NewGetSet(type, gsp);
      if (descr == NULL) {
          return -1;
      }

      if (PyDict_SetDefault(dict, PyDescr_NAME(descr), descr) == NULL) {
          Py_DECREF(descr);
          return -1;
      }
      Py_DECREF(descr);
  }

  PyType_Modified(type);
  return 0;
}

static PyGetSetDef Ci_method_getset[] = {
  {.name = "__doc__", .get = (getter)Cix_method_get_doc, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__qualname__", .get = (getter)Cix_descr_get_qualname, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__text_signature__", .get = (getter)Cix_method_get_text_signature, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__typed_signature__", .get = (getter)Ci_method_get_typed_signature, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {},
};

static PyGetSetDef Ci_meth_getset[] = {
  {.name = "__doc__", .get = (getter)Cix_meth_get__doc__, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__name__", .get = (getter)Cix_meth_get__name__, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__qualname__", .get = (getter)Cix_meth_get__qualname__, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__text_signature__", .get = (getter)Cix_meth_get__text_signature__, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {.name = "__typed_signature__", .get = (getter)Ci_meth_get__typed_signature__, .set = nullptr, .doc = nullptr, .closure = nullptr},
  {},
};

static int init_already_existing_types() {
  PyUnstable_GC_VisitObjects([](PyObject* obj, void*) {
    if (PyType_Check(obj) && PyType_HasFeature((PyTypeObject*)obj, Py_TPFLAGS_READY)) {
      _PyJIT_TypeCreated((PyTypeObject*)obj);
    }
    return 1;
  }, nullptr);

  if (override_tp_getset(&PyMethodDescr_Type, Ci_method_getset) < 0) {
    return -1;
  }
  if (override_tp_getset(&PyClassMethodDescr_Type, Ci_method_getset) < 0) {
    return -1;
  }
  if (override_tp_getset(&PyCFunction_Type, Ci_meth_getset) < 0) {
    return -1;
  }

  return 0;
}

static void shadowcode_code_sizeof(struct _PyShadowCode *shadow, Py_ssize_t *res) {
    *res += sizeof(_PyShadowCode);
    *res += sizeof(PyObject *) * shadow->l1_cache.size;
    *res += sizeof(PyObject *) * shadow->cast_cache.size;
    *res += sizeof(PyObject **) * shadow->globals_size;
    *res += sizeof(_PyShadow_InstanceAttrEntry **) *
            shadow->polymorphic_caches_size;
    *res += sizeof(_FieldCache) * shadow->field_cache_size;
    *res += sizeof(_Py_CODEUNIT) * shadow->len;
}

static int get_current_code_flags(PyThreadState* tstate) {
    PyCodeObject *cur_code = NULL;
    Ci_WalkStack(tstate, [](void *ptr, PyCodeObject *code, int) {
      PyCodeObject **topmost_code = (PyCodeObject **) ptr;
      *topmost_code = code;
      return CI_SWD_STOP_STACK_WALK;
    }, &cur_code);
    if (!cur_code) {
        return -1;
    }
    return cur_code->co_flags;
}

static int _Ci_StrictModule_Check(PyObject* obj) {
  return Ci_StrictModule_Check(obj);
}

static int cinder_init() {
  Ci_hook_type_created = _PyJIT_TypeCreated;
  Ci_hook_type_destroyed = _PyJIT_TypeDestroyed;
  Ci_hook_type_name_modified = _PyJIT_TypeNameModified;
  Ci_hook_type_dealloc = _PyClassLoader_TypeDealloc;
  Ci_hook_type_traverse = _PyClassLoader_TypeTraverse;
  Ci_hook_type_clear = _PyClassLoader_TypeClear;
  Ci_hook_add_subclass = _PyClassLoader_AddSubclass;
  Ci_hook_type_pre_setattr = _PyClassLoader_InitTypeForPatching;
  Ci_hook_type_setattr = _PyClassLoader_UpdateSlot;
  Ci_hook_JIT_GetProfileNewInterpThread = _PyJIT_GetProfileNewInterpThreads;
  Ci_hook_JIT_GetFrame = _PyJIT_GetFrame;
  Ci_hook_PyCMethod_New = Ci_PyCMethod_New_METH_TYPED;
  Ci_hook_PyDescr_NewMethod = Ci_PyDescr_NewMethod_METH_TYPED;
  Ci_hook_WalkStack = Ci_WalkStack;
  Ci_hook_code_sizeof_shadowcode = shadowcode_code_sizeof;
  Ci_hook_PyJIT_GenVisitRefs = _PyJIT_GenVisitRefs;
  Ci_hook_PyJIT_GenDealloc = _PyJIT_GenDealloc;
  Ci_hook_PyJIT_GenSend = _PyJIT_GenSend;
  Ci_hook_PyJIT_GenYieldFromValue = _PyJIT_GenYieldFromValue;
  Ci_hook_PyJIT_GenMaterializeFrame = _PyJIT_GenMaterializeFrame;
  Ci_hook__PyShadow_FreeAll = _PyShadow_FreeAll;
  Ci_hook_MaybeStrictModule_Dict = Ci_MaybeStrictModule_Dict;
  Ci_hook_EvalFrame = Ci_EvalFrame;
  Ci_hook_PyJIT_GetFrame = _PyJIT_GetFrame;
  Ci_hook_PyJIT_GetBuiltins = _PyJIT_GetBuiltins;
  Ci_hook_PyJIT_GetGlobals = _PyJIT_GetGlobals;
  Ci_hook_PyJIT_GetCurrentCodeFlags = get_current_code_flags;
  Ci_hook_ShadowFrame_GetCode_JIT = Ci_ShadowFrame_GetCode_JIT;
  Ci_hook_ShadowFrame_HasGen_JIT = Ci_ShadowFrame_HasGen_JIT;
  Ci_hook_ShadowFrame_GetModuleName_JIT = Ci_ShadowFrame_GetModuleName_JIT;
  Ci_hook_ShadowFrame_WalkAndPopulate = Ci_ShadowFrame_WalkAndPopulate;

  if (init_already_existing_types() < 0) {
    return -1;
  }

  // Prevent the linker from omitting the object file containing the parallel
  // GC implementation. This is the only reference from another translation
  // unit to symbols defined in the file. Without it the linker will omit the
  // object file when linking the libpython archive into the main python
  // binary.
  //
  // TODO(T168696266): Remove this once we migrate to cinderx.
  PyObject *res = Cinder_GetParallelGCSettings();
  if (res == nullptr) {
    return -1;
  }
  Py_DECREF(res);

  if (Ci_Watchers_Init()) {
    return -1;
  }

  int jit_init_ret = _PyJIT_Initialize();
  if (jit_init_ret) {
    // Exit here rather than in _PyJIT_Initialize so the tests for printing
    // argument help works.
    if (jit_init_ret == -2) {
      exit(1);
    }
    return -1;
  }
  init_already_existing_funcs();

  Ci_cinderx_initialized = 1;

  return 0;
}

// Attempts to shutdown CinderX. This is very much a best-effort with the
// primary goals being to ensure Python shuts down without crashing, and
// tests which do some kind of re-initialization continue to work. A secondary
// goal is to one day make it possible to arbitrarily load/relaod CinderX at
// runtime. However, for now the only thing we truly support is loading
// CinderX once ASAP on start-up, and then never unloading it until complete
// process shutdown.
static int cinder_fini() {
  _PyClassLoader_ClearCache();

  if (PyThreadState_Get()->shadow_frame) {
    // If any Python code is running we can't tell if JIT code is in use. Even
    // if every frame in the callstack is interpreter-owned, some of them could
    // be the result of deopt and JIT code may still be on the native stack.
    JIT_DABORT("Python code still running on CinderX unload");
    JIT_LOG("Python code is executing, cannot cleanly shutdown CinderX.");
    return -1;
  }

  if (_PyJIT_Finalize()) {
    return -1;
  }

  if (Ci_cinderx_initialized && Ci_hook__PyShadow_FreeAll()) {
    return -1;
  }

  Ci_hook_type_created = nullptr;
  Ci_hook_type_destroyed = nullptr;
  Ci_hook_type_name_modified = nullptr;
  Ci_hook_type_pre_setattr = nullptr;
  Ci_hook_type_setattr = nullptr;
  Ci_hook_JIT_GetProfileNewInterpThread = nullptr;
  Ci_hook_JIT_GetFrame = nullptr;
  Ci_hook_PyDescr_NewMethod = nullptr;
  Ci_hook_WalkStack = nullptr;
  Ci_hook_code_sizeof_shadowcode = nullptr;
  Ci_hook_PyJIT_GenVisitRefs = nullptr;
  Ci_hook_PyJIT_GenDealloc = nullptr;
  Ci_hook_PyJIT_GenSend = nullptr;
  Ci_hook_PyJIT_GenYieldFromValue = nullptr;
  Ci_hook_PyJIT_GenMaterializeFrame = nullptr;
  Ci_hook__PyShadow_FreeAll = nullptr;
  Ci_hook_add_subclass = nullptr;
  Ci_hook_MaybeStrictModule_Dict = nullptr;
  Ci_hook_ShadowFrame_GetCode_JIT = nullptr;
  Ci_hook_ShadowFrame_HasGen_JIT = nullptr;
  Ci_hook_ShadowFrame_GetModuleName_JIT = nullptr;
  Ci_hook_ShadowFrame_WalkAndPopulate = nullptr;

  /* These hooks are not safe to unset, since there may be SP generic types that
   * outlive finalization of the cinder module, and if we don't have the hooks in
   * place for their cleanup, we will have leaks. But these hooks also have no
   * effect for any type other than an SP generic type, so they are generally
   * harmless to leave in place, even if the runtime is shutdown and
   * reinitialized. */

  // Ci_hook_type_dealloc = nullptr;
  // Ci_hook_type_traverse = nullptr;
  // Ci_hook_type_clear = nullptr;

  Ci_hook_EvalFrame = nullptr;
  Ci_hook_PyJIT_GetFrame = nullptr;
  Ci_hook_PyJIT_GetBuiltins = nullptr;
  Ci_hook_PyJIT_GetGlobals = nullptr;
  Ci_hook_PyJIT_GetCurrentCodeFlags = nullptr;

  Ci_cinderx_initialized = 0;

  return 0;
}

static bool g_was_initialized = false;

static PyObject* init(PyObject * /*self*/, PyObject * /*obj*/) {
  if (g_was_initialized) {
    Py_RETURN_FALSE;
  }
  if (cinder_init()) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to initialize CinderX");
    return NULL;
  }
  g_was_initialized = true;
  Py_RETURN_TRUE;
}

static void module_free(void *) {
  if (g_was_initialized) {
    g_was_initialized = false;
    JIT_CHECK(cinder_fini() == 0, "Failed to finalize CinderX");
  }
}

static PyMethodDef _cinderx_methods[] = {
    {"init", init, METH_NOARGS,
     "This must be called early. Preferably before any user code is run."},
    {"clear_caches", clear_caches, METH_NOARGS,
     "Clears caches associated with the JIT.  This may have a negative effect "
     "on performance of existing JIT compiled code."},
    {"clear_all_shadow_caches", clear_all_shadow_caches, METH_NOARGS, ""},
    {"strict_module_patch", strict_module_patch, METH_VARARGS,
     strict_module_patch_doc},
    {"strict_module_patch_delete", strict_module_patch_delete, METH_VARARGS,
     strict_module_patch_delete_doc},
    {"strict_module_patch_enabled", strict_module_patch_enabled, METH_O,
     strict_module_patch_enabled_doc},
    {"clear_classloader_caches", clear_classloader_caches, METH_NOARGS,
     "Clears classloader caches and vtables on all accessible types. "
     "Will hurt perf; for test isolation where modules and types with "
     "identical names are dynamically created and destroyed."},
    {"set_profile_interp", set_profile_interp, METH_O,
     "Enable or disable interpreter profiling for this thread. Returns whether "
     "or not profiling was enabled before the call."},
    {"set_profile_interp_all", set_profile_interp_all, METH_O,
     "Enable or disable interpreter profiling for all threads, including "
     "threads created after this function returns."},
    {"set_profile_interp_period", set_profile_interp_period, METH_O,
     "Set the period, in bytecode instructions, for interpreter profiling."},
    {"get_and_clear_type_profiles", get_and_clear_type_profiles, METH_NOARGS,
     "Get and clear accumulated interpreter type profiles."},
    {"get_and_clear_type_profiles_with_metadata",
     get_and_clear_type_profiles_with_metadata, METH_NOARGS,
     "Get and clear accumulated interpreter type profiles, including "
     "type-specific metadata."},
    {"clear_type_profiles", clear_type_profiles, METH_NOARGS,
     "Clear accumulated interpreter type profiles."},
    {"watch_sys_modules", watch_sys_modules, METH_NOARGS,
     "Watch the sys.modules dict to allow invalidating Static Python's "
     "internal caches."},
    {"enable_parallel_gc", (PyCFunction)cinder_enable_parallel_gc,
     METH_VARARGS | METH_KEYWORDS, cinder_enable_parallel_gc_doc},
    {"disable_parallel_gc", cinder_disable_parallel_gc, METH_NOARGS,
     cinder_disable_parallel_gc_doc},
    {"get_parallel_gc_settings", cinder_get_parallel_gc_settings, METH_NOARGS,
     cinder_get_parallel_gc_settings_doc},
    {"_compile_perf_trampoline_pre_fork", compile_perf_trampoline_pre_fork,
     METH_NOARGS, "Compile perf-trampoline entries before forking"},
    {"_is_compile_perf_trampoline_pre_fork_enabled",
     is_compile_perf_trampoline_pre_fork_enabled, METH_NOARGS,
     "Return whether compile perf-trampoline entries before fork is enabled or "
     "not"},
    {"_get_entire_call_stack_as_qualnames_with_lineno",
     get_entire_call_stack_as_qualnames_with_lineno, METH_NOARGS,
     "Return the current stack as a list of tuples (qualname, lineno)."},
    {"_get_entire_call_stack_as_qualnames_with_lineno_and_frame",
     get_entire_call_stack_as_qualnames_with_lineno_and_frame, METH_NOARGS,
     "Return the current stack as a list of tuples (qualname, lineno, PyFrame "
     "| None)."},
    {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef _cinderx_module = {
    PyModuleDef_HEAD_INIT,  "_cinderx", "The internal CinderX extension module",
    /*m_size=*/-1, // Doesn't support sub-interpreters
    _cinderx_methods,
    /*m_slots=*/nullptr,
    /*m_traverse=*/nullptr,
    /*m_clear=*/nullptr,
    /*m_free=*/module_free,
};

PyMODINIT_FUNC PyInit__cinderx(void) {
  if ((_PyInterpreterState_GET()->dlopenflags & RTLD_GLOBAL) == 0) {
    PyErr_SetString(PyExc_ImportError, "Do not import _cinderx directly. Use cinderx instead.");
    return NULL;
  }

  // Deliberate single-phase initialization.
  PyObject *m = PyModule_Create(&_cinderx_module);
  if (m == NULL) {
    return NULL;
  }

  if (PyType_Ready(&PyCachedProperty_Type) < 0) {
    return NULL;
  }
  if (PyType_Ready(&PyCachedPropertyWithDescr_Type) < 0) {
    return NULL;
  }
  if (PyType_Ready(&Ci_StrictModule_Type) < 0) {
    return NULL;
  }
  if (PyType_Ready(&PyAsyncCachedProperty_Type) < 0) {
    return NULL;
  }
  if (PyType_Ready(&PyAsyncCachedPropertyWithDescr_Type) < 0) {
    return NULL;
  }
  if (PyType_Ready(&PyAsyncCachedClassProperty_Type) < 0) {
    return NULL;
  }

  PyObject *cached_classproperty =
      PyType_FromSpec(&_PyCachedClassProperty_TypeSpec);
  if (cached_classproperty == NULL) {
    return NULL;
  }
  if (PyObject_SetAttrString(m, "cached_classproperty", cached_classproperty) <
      0) {
    Py_DECREF(cached_classproperty);
    return NULL;
  }
  Py_DECREF(cached_classproperty);

#define ADDITEM(NAME, OBJECT)                                                  \
  if (PyObject_SetAttrString(m, NAME, (PyObject *)OBJECT) < 0) {               \
    return NULL;                                                               \
  }

  ADDITEM("StrictModule", &Ci_StrictModule_Type);
  ADDITEM("cached_property", &PyCachedProperty_Type);
  ADDITEM("async_cached_property", &PyAsyncCachedProperty_Type);
  ADDITEM("async_cached_classproperty", &PyAsyncCachedClassProperty_Type);

#undef ADDITEM

  return m;
}
