#pragma once

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cinder/ci_api.h"

/*
 * This file contains signatures for functions that live in various
 * CPython-internal files (e.g., Objects/funcobject.c, Python/ceval.c) so they
 * can access private functions or data, and are not intended to be candidates
 * for upstreaming. They should all go away one way or another as part of our
 * CinderX work.
 */

CiAPI_FUNC(PyObject *) Ci_dict_subscript(PyObject *mp, PyObject *key);
CiAPI_FUNC(PyObject *) Ci_list_subscript(PyObject *list, PyObject *item);
CiAPI_FUNC(PyObject *) Ci_tuple_subscript(PyObject *self, PyObject *item);
CiAPI_FUNC(PyObject *) Ci_module_lookupattro(PyObject *self, PyObject *name);

CiAPI_FUNC(Py_hash_t) Ci_TupleHashItems(PyObject *const *items, Py_ssize_t len);

/* Force the dictionary to use a combined layout.
 * Returns 0 on success or -1 on error.
 */
CiAPI_FUNC(int) Ci_PyDict_ForceCombined(PyObject *);

CiAPI_FUNC(PyObject **) Ci_PyObject_GetDictPtrAtOffset(PyObject *obj, Py_ssize_t dictoffset);

/* Enable or disable interpreter type profiling for all threads or for a
   specific thread. */
CiAPI_FUNC(void) Ci_ThreadState_SetProfileInterpAll(int);
CiAPI_FUNC(void) Ci_ThreadState_SetProfileInterp(PyThreadState *, int);

/* Set the profile period for interpreter type profiling, in bytecode
   instructions. */
CiAPI_FUNC(void) Ci_RuntimeState_SetProfileInterpPeriod(long);

CiAPI_FUNC(int) Ci_set_attribute_error_context(PyObject *v, PyObject *name);

CiAPI_DATA(PyTypeObject) Ci_StrictModuleLoader_Type;
CiAPI_DATA(PyTypeObject) Ci_StrictModuleAnalysisResult_Type;

// Originally in Include/object.h
#define Ci_Py_TPFLAG_CPYTHON_ALLOCATED (1UL << 2)
#define Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED (1UL << 3)

#define Ci_Py_TPFLAGS_FROZEN (1UL << 21)

typedef struct {
    PyObject *init_func;
} Ci_PyType_CinderExtra;

#define Ci_PyHeapType_CINDER_EXTRA(etype) \
    ((Ci_PyType_CinderExtra *)(((char *)etype) +  \
      Py_TYPE(etype)->tp_basicsize + \
      Py_SIZE(etype) * sizeof(PyMemberDef)))

// Implementation in Python/bltinmodule.c
CiAPI_FUNC(PyObject *) builtin_next(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

CiAPI_FUNC(PyObject *) Ci_Builtin_Next_Core(PyObject *it, PyObject *def);

typedef enum {
  CI_SWD_STOP_STACK_WALK = 0,
  CI_SWD_CONTINUE_STACK_WALK = 1,
} CiStackWalkDirective;

/*
 * A callback that will be invoked by Ci_WalkStack for each entry on the Python
 * call stack.
 */
typedef CiStackWalkDirective (*CiWalkStackCallback)(void *data,
                                                    PyCodeObject *code,
                                                    int lineno);

typedef CiStackWalkDirective (*CiWalkAsyncStackCallback)(void *data,
                                                    PyObject *fqname,
                                                    PyCodeObject *code,
                                                    int lineno,
                                                    PyObject* pyFrame);

/*
 * Walk the stack, invoking cb for each entry with the supplied data parameter
 * as its first argument.
 *
 * The return value of cb controls whether or not stack walking continues.
 */
CiAPI_FUNC(void) Ci_WalkStack(PyThreadState *tstate, CiWalkStackCallback cb, void *data);
CiAPI_FUNC(void) Ci_WalkAsyncStack(PyThreadState *tstate, CiWalkAsyncStackCallback cb, void *data);

CiAPI_FUNC(PyObject *) CiCoro_New_NoFrame(PyThreadState *tstate, PyCodeObject *code);
CiAPI_FUNC(PyObject *) CiAsyncGen_New_NoFrame(PyCodeObject *code);
CiAPI_FUNC(PyObject *) CiGen_New_NoFrame(PyCodeObject *code);
CiAPI_FUNC(int) CiGen_close_yf(PyObject *yf);
CiAPI_FUNC(int) CiGen_restore_error(PyObject *et, PyObject *ev, PyObject *tb);

struct Ci_PyGCImpl;

/*
 * Collect cyclic garbage.
 *
 * impl           - Pointer to the collection implementation.
 * tstate         - Indirectly specifies (via tstate->interp) the interpreter
                    for which collection should be performed.
 * generation     - Collect generations <= this value
 * n_collected    - Out param for number of objects collected
 * n_unollectable - Out param for number of uncollectable garbage objects
 * nofail         - When true, swallow exceptions that occur during collection
 */
typedef Py_ssize_t (*Ci_gc_collect_t)(struct Ci_PyGCImpl *impl, PyThreadState* tstate, int generation,
                                      Py_ssize_t *n_collected, Py_ssize_t *n_uncollectable,
                                      int nofail);

// Free a collector
typedef void (*Ci_gc_finalize_t)(struct Ci_PyGCImpl *impl);

// An implementation of cyclic garbage collection
typedef struct Ci_PyGCImpl {
    Ci_gc_collect_t collect;
    Ci_gc_finalize_t finalize;
} Ci_PyGCImpl;

struct _gc_runtime_state;

/*
 * Set the collection implementation.
 *
 * The callee takes ownership of impl.
 *
 * Returns a pointer to the previous impl, which the caller is responsible for freeing
 * using the returned impl's finalize().
 */
CiAPI_FUNC(Ci_PyGCImpl *) Ci_PyGC_SetImpl(struct _gc_runtime_state *gc_state, Ci_PyGCImpl *impl);

/*
 * Returns a pointer to the current GC implementation but does not transfer
 * ownership to the caller.
 */
CiAPI_FUNC(Ci_PyGCImpl *) Ci_PyGC_GetImpl(struct _gc_runtime_state *gc_state);

/*
 * Clear free lists (e.g. frames, tuples, etc.) for the given interpreter.
 *
 * This should be called by GC implementations after collecting the highest
 * generation.
 */
CiAPI_FUNC(void) Ci_PyGC_ClearFreeLists(PyInterpreterState *interp);

typedef struct {
  PyCodeObject *code;        // The code object for the bounds. May be NULL.
  PyCodeAddressRange bounds; // Only valid if code != NULL.
  CFrame cframe;
} PyTraceInfo;

CiAPI_DATA(int) lltrace;

CiAPI_FUNC(int) Cix_eval_frame_handle_pending(PyThreadState *tstate);
CiAPI_FUNC(PyObject *)
    Cix_special_lookup(PyThreadState *tstate, PyObject *o, _Py_Identifier *id);
CiAPI_FUNC(void) Cix_format_kwargs_error(PyThreadState *tstate, PyObject *func,
                                         PyObject *kwargs);
CiAPI_FUNC(void)
    Cix_format_awaitable_error(PyThreadState *tstate, PyTypeObject *type,
                               int prevprevopcode, int prevopcode);
CiAPI_FUNC(PyFrameObject *)
    Cix_PyEval_MakeFrameVector(PyThreadState *tstate, PyFrameConstructor *con,
                               PyObject *locals, PyObject *const *args,
                               Py_ssize_t argcount, PyObject *kwnames);
CiAPI_FUNC(PyObject *)
    Cix_SuperLookupMethodOrAttr(PyThreadState *tstate, PyObject *global_super,
                                PyTypeObject *type, PyObject *self,
                                PyObject *name, int call_no_args,
                                int *meth_found);
CiAPI_FUNC(int)
    Cix_do_raise(PyThreadState *tstate, PyObject *exc, PyObject *cause);
CiAPI_FUNC(void) Cix_format_exc_check_arg(PyThreadState *, PyObject *,
                                          const char *, PyObject *);
CiAPI_FUNC(PyObject *)
    Cix_match_class(PyThreadState *tstate, PyObject *subject, PyObject *type,
                    Py_ssize_t nargs, PyObject *kwargs);
CiAPI_FUNC(PyObject *)
    Cix_match_keys(PyThreadState *tstate, PyObject *map, PyObject *keys);

CiAPI_FUNC(PyObject *)
    Ci_Super_Lookup(PyTypeObject *type, PyObject *obj, PyObject *name,
                    PyObject *super_instance, int *meth_found);

CiAPI_FUNC(int)
    _PyCode_InitAddressRange(PyCodeObject *co, PyCodeAddressRange *bounds);

#ifdef __cplusplus
}
#endif
