/* Portions copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */

/* Execute compiled code */

/* XXX TO DO:
   XXX speed up searching for keywords by using a dictionary
   XXX document it!
   */

/* enable more aggressive intra-module optimizations, where available */
/* affects both release and debug builds - see bpo-43271 */
#define PY_LOCAL_AGGRESSIVE

#include "Python.h"
#include "pycore_abstract.h"      // _PyIndex_Check()
#include "pycore_call.h"          // _PyObject_FastCallDictTstate()
#include "pycore_ceval.h"         // _PyEval_SignalAsyncExc()
#include "pycore_code.h"          // _PyCode_InitOpcache()
#include "pycore_initconfig.h"    // _PyStatus_OK()
#include "pycore_import.h"        // _PyImport_ImportName()
#include "pycore_lazyimport.h"    // PyLazyImport_CheckExact()
#include "pycore_object.h"        // _PyObject_GC_TRACK()
#include "pycore_pyerrors.h"      // _PyErr_Fetch()
#include "pycore_pylifecycle.h"   // _PyErr_Print()
#include "pycore_pymem.h"         // _PyMem_IsPtrFreed()
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_shadow_frame.h"  // _PyShadowFrame_{PushInterp,Pop}
#include "pycore_shadowcode.h"
#include "pycore_sysmodule.h"     // _PySys_Audit()
#include "pycore_tuple.h"         // _PyTuple_ITEMS()

#include "Jit/pyjit.h"
#include "code.h"
#include "dictobject.h"
#include "frameobject.h"
#include "opcode.h"
#include "pydtrace.h"
#include "setobject.h"
#include "structmember.h" // struct PyMemberDef, T_OFFSET_EX
#include "cinder/exports.h"
#include "classloader.h"

#include <ctype.h>

typedef struct {
    PyCodeObject *code; // The code object for the bounds. May be NULL.
    PyCodeAddressRange bounds; // Only valid if code != NULL.
    CFrame cframe;
} PyTraceInfo;


#ifdef Py_DEBUG
/* For debugging the interpreter: */
#define LLTRACE  1      /* Low-level trace feature */
#define CHECKEXC 1      /* Double-check exception checking */
#endif

#if !defined(Py_BUILD_CORE)
#  error "ceval.c must be build with Py_BUILD_CORE define for best performance"
#endif

_Py_IDENTIFIER(__name__);

/* Forward declarations */
Py_LOCAL_INLINE(PyObject *) call_function(
    PyThreadState *tstate, PyTraceInfo *, PyObject ***pp_stack,
    Py_ssize_t oparg, PyObject *kwnames, size_t flags);
static PyObject * do_call_core(PyThreadState *tstate,
             PyTraceInfo *trace_info,
             PyObject *func,
             PyObject *callargs,
             PyObject *kwdict,
             int awaited);

#ifdef LLTRACE
static int lltrace;
static int prtrace(PyThreadState *, PyObject *, const char *);
#endif
static int call_trace(Py_tracefunc, PyObject *,
                      PyThreadState *, PyFrameObject *,
                      PyTraceInfo *,
                      int, PyObject *);
static int call_trace_protected(Py_tracefunc, PyObject *,
                                PyThreadState *, PyFrameObject *,
                                PyTraceInfo *,
                                int, PyObject *);
static void call_exc_trace(Py_tracefunc, PyObject *,
                           PyThreadState *, PyFrameObject *,
                           PyTraceInfo *trace_info);
static int maybe_call_line_trace(Py_tracefunc, PyObject *,
                                 PyThreadState *, PyFrameObject *,
                                 PyTraceInfo *, int);
static void maybe_dtrace_line(PyFrameObject *, PyTraceInfo *, int);
static void dtrace_function_entry(PyFrameObject *);
static void dtrace_function_return(PyFrameObject *);

static int import_all_from(PyThreadState *, PyObject *, PyObject *);
static void format_exc_unbound(PyThreadState *tstate, PyCodeObject *co, int oparg);
static PyObject * unicode_concatenate(PyThreadState *, PyObject *, PyObject *,
                                      PyFrameObject *, const _Py_CODEUNIT *);
#ifdef ENABLE_CINDERX
static void try_profile_next_instr(PyFrameObject* f, PyObject** stack_pointer,
                                   const _Py_CODEUNIT* next_instr);
#endif

#define NAME_ERROR_MSG \
    "name '%.200s' is not defined"
#define UNBOUNDLOCAL_ERROR_MSG \
    "local variable '%.200s' referenced before assignment"
#define UNBOUNDFREE_ERROR_MSG \
    "free variable '%.200s' referenced before assignment" \
    " in enclosing scope"

/* Dynamic execution profile */
#ifdef DYNAMIC_EXECUTION_PROFILE
#ifdef DXPAIRS
static long dxpairs[257][256];
#define dxp dxpairs[256]
#else
static long dxp[256];
#endif
#endif

/* per opcode cache */
static int opcache_min_runs = 1024;  /* create opcache when code executed this many times */
#define OPCODE_CACHE_MAX_TRIES 20
#define OPCACHE_STATS 0  /* Enable stats */

// This function allows to deactivate the opcode cache. As different cache mechanisms may hold
// references, this can mess with the reference leak detector functionality so the cache needs
// to be deactivated in such scenarios to avoid false positives. See bpo-3714 for more information.
void
_PyEval_DeactivateOpCache(void)
{
    opcache_min_runs = 0;
}

#if OPCACHE_STATS
static size_t opcache_code_objects = 0;
static size_t opcache_code_objects_extra_mem = 0;

static size_t opcache_global_opts = 0;
static size_t opcache_global_hits = 0;
static size_t opcache_global_misses = 0;

static size_t opcache_attr_opts = 0;
static size_t opcache_attr_hits = 0;
static size_t opcache_attr_misses = 0;
static size_t opcache_attr_deopts = 0;
static size_t opcache_attr_total = 0;
#endif


#ifndef NDEBUG
/* Ensure that tstate is valid: sanity check for PyEval_AcquireThread() and
   PyEval_RestoreThread(). Detect if tstate memory was freed. It can happen
   when a thread continues to run after Python finalization, especially
   daemon threads. */
static int
is_tstate_valid(PyThreadState *tstate)
{
    assert(!_PyMem_IsPtrFreed(tstate));
    assert(!_PyMem_IsPtrFreed(tstate->interp));
    return 1;
}
#endif


/* This can set eval_breaker to 0 even though gil_drop_request became
   1.  We believe this is all right because the eval loop will release
   the GIL eventually anyway. */
static inline void
COMPUTE_EVAL_BREAKER(PyInterpreterState *interp,
                     struct _ceval_runtime_state *ceval,
                     struct _ceval_state *ceval2)
{
    _Py_atomic_store_relaxed(&ceval2->eval_breaker,
        _Py_atomic_load_relaxed(&ceval2->gil_drop_request)
        | (_Py_atomic_load_relaxed(&ceval->signals_pending)
           && _Py_ThreadCanHandleSignals(interp))
        | (_Py_atomic_load_relaxed(&ceval2->pending.calls_to_do)
           && _Py_ThreadCanHandlePendingCalls())
        | ceval2->pending.async_exc);
}


static inline void
SET_GIL_DROP_REQUEST(PyInterpreterState *interp)
{
    struct _ceval_state *ceval2 = &interp->ceval;
    _Py_atomic_store_relaxed(&ceval2->gil_drop_request, 1);
    _Py_atomic_store_relaxed(&ceval2->eval_breaker, 1);
}


static inline void
RESET_GIL_DROP_REQUEST(PyInterpreterState *interp)
{
    struct _ceval_runtime_state *ceval = &interp->runtime->ceval;
    struct _ceval_state *ceval2 = &interp->ceval;
    _Py_atomic_store_relaxed(&ceval2->gil_drop_request, 0);
    COMPUTE_EVAL_BREAKER(interp, ceval, ceval2);
}


static inline void
SIGNAL_PENDING_CALLS(PyInterpreterState *interp)
{
    struct _ceval_runtime_state *ceval = &interp->runtime->ceval;
    struct _ceval_state *ceval2 = &interp->ceval;
    _Py_atomic_store_relaxed(&ceval2->pending.calls_to_do, 1);
    COMPUTE_EVAL_BREAKER(interp, ceval, ceval2);
}


static inline void
UNSIGNAL_PENDING_CALLS(PyInterpreterState *interp)
{
    struct _ceval_runtime_state *ceval = &interp->runtime->ceval;
    struct _ceval_state *ceval2 = &interp->ceval;
    _Py_atomic_store_relaxed(&ceval2->pending.calls_to_do, 0);
    COMPUTE_EVAL_BREAKER(interp, ceval, ceval2);
}


static inline void
SIGNAL_PENDING_SIGNALS(PyInterpreterState *interp, int force)
{
    struct _ceval_runtime_state *ceval = &interp->runtime->ceval;
    struct _ceval_state *ceval2 = &interp->ceval;
    _Py_atomic_store_relaxed(&ceval->signals_pending, 1);
    if (force) {
        _Py_atomic_store_relaxed(&ceval2->eval_breaker, 1);
    }
    else {
        /* eval_breaker is not set to 1 if thread_can_handle_signals() is false */
        COMPUTE_EVAL_BREAKER(interp, ceval, ceval2);
    }
}


static inline void
UNSIGNAL_PENDING_SIGNALS(PyInterpreterState *interp)
{
    struct _ceval_runtime_state *ceval = &interp->runtime->ceval;
    struct _ceval_state *ceval2 = &interp->ceval;
    _Py_atomic_store_relaxed(&ceval->signals_pending, 0);
    COMPUTE_EVAL_BREAKER(interp, ceval, ceval2);
}


static inline void
SIGNAL_ASYNC_EXC(PyInterpreterState *interp)
{
    struct _ceval_state *ceval2 = &interp->ceval;
    ceval2->pending.async_exc = 1;
    _Py_atomic_store_relaxed(&ceval2->eval_breaker, 1);
}


static inline void
UNSIGNAL_ASYNC_EXC(PyInterpreterState *interp)
{
    struct _ceval_runtime_state *ceval = &interp->runtime->ceval;
    struct _ceval_state *ceval2 = &interp->ceval;
    ceval2->pending.async_exc = 0;
    COMPUTE_EVAL_BREAKER(interp, ceval, ceval2);
}

PyObject *Ci_GetAIter(PyThreadState *tstate, PyObject *obj) {
    unaryfunc getter = NULL;
    PyObject *iter = NULL;
    PyTypeObject *type = Py_TYPE(obj);

    if (type->tp_as_async != NULL) {
        getter = type->tp_as_async->am_aiter;
    }

    if (getter != NULL) {
        iter = (*getter)(obj);
        if (iter == NULL) {
            return NULL;
        }
    }
    else {
        _PyErr_Format(tstate, PyExc_TypeError,
                        "'async for' requires an object with "
                        "__aiter__ method, got %.100s",
                        type->tp_name);
        return NULL;
    }

    if (Py_TYPE(iter)->tp_as_async == NULL ||
            Py_TYPE(iter)->tp_as_async->am_anext == NULL) {

        _PyErr_Format(tstate, PyExc_TypeError,
                        "'async for' received an object from __aiter__ "
                        "that does not implement __anext__: %.100s",
                        Py_TYPE(iter)->tp_name);
        Py_DECREF(iter);
        return NULL;
    }
    return iter;
}

PyObject *Ci_GetANext(PyThreadState *tstate, PyObject *aiter) {
    unaryfunc getter = NULL;
    PyObject *next_iter = NULL;
    PyObject *awaitable = NULL;
    PyTypeObject *type = Py_TYPE(aiter);

    if (PyAsyncGen_CheckExact(aiter)) {
        awaitable = type->tp_as_async->am_anext(aiter);
        if (awaitable == NULL) {
            return NULL;
        }
    } else {
        if (type->tp_as_async != NULL){
            getter = type->tp_as_async->am_anext;
        }

        if (getter != NULL) {
            next_iter = (*getter)(aiter);
            if (next_iter == NULL) {
                return NULL;
            }
        }
        else {
            _PyErr_Format(tstate, PyExc_TypeError,
                            "'async for' requires an iterator with "
                            "__anext__ method, got %.100s",
                            type->tp_name);
            return NULL;
        }

        awaitable = _PyCoro_GetAwaitableIter(next_iter);
        if (awaitable == NULL) {
            _PyErr_FormatFromCause(
                PyExc_TypeError,
                "'async for' received an invalid object "
                "from __anext__: %.100s",
                Py_TYPE(next_iter)->tp_name);

            Py_DECREF(next_iter);
            return NULL;
        } else {
            Py_DECREF(next_iter);
        }
    }
    return awaitable;
}

// These are used to truncate primitives/check signed bits when converting between them
#ifdef ENABLE_CINDERX
static uint64_t trunc_masks[] = {0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
static uint64_t signed_bits[] = {0x80, 0x8000, 0x80000000, 0x8000000000000000};
static uint64_t signex_masks[] = {0xFFFFFFFFFFFFFF00, 0xFFFFFFFFFFFF0000,
                                  0xFFFFFFFF00000000, 0x0};
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "ceval_gil.h"

#ifdef ENABLE_CINDERX
int _PyEval_ShadowByteCodeEnabled = 1;
#else
int _PyEval_ShadowByteCodeEnabled = 0;
#endif

PyAPI_DATA(int) Py_LazyImportsFlag;

void _Py_NO_RETURN
_Py_FatalError_TstateNULL(const char *func)
{
    _Py_FatalErrorFunc(func,
                       "the function must be called with the GIL held, "
                       "but the GIL is released "
                       "(the current Python thread state is NULL)");
}

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
int
_PyEval_ThreadsInitialized(PyInterpreterState *interp)
{
    return gil_created(&interp->ceval.gil);
}

int
PyEval_ThreadsInitialized(void)
{
    // Fatal error if there is no current interpreter
    PyInterpreterState *interp = PyInterpreterState_Get();
    return _PyEval_ThreadsInitialized(interp);
}
#else
int
_PyEval_ThreadsInitialized(_PyRuntimeState *runtime)
{
    return gil_created(&runtime->ceval.gil);
}

int
PyEval_ThreadsInitialized(void)
{
    _PyRuntimeState *runtime = &_PyRuntime;
    return _PyEval_ThreadsInitialized(runtime);
}
#endif

#define IS_AWAITED() (_Py_OPCODE(*next_instr) == GET_AWAITABLE)
#define DISPATCH_EAGER_CORO_RESULT(r, X)                                    \
        assert(Ci_PyWaitHandle_CheckExact(r));                                \
        PyObject *coro_or_result = ((Ci_PyWaitHandleObject*)r)->wh_coro_or_result; \
        X(coro_or_result);                                                  \
        assert(_Py_OPCODE(*next_instr) == GET_AWAITABLE);                   \
        assert(_Py_OPCODE(*(next_instr + 1)) == LOAD_CONST);                \
        if (((Ci_PyWaitHandleObject*)r)->wh_waiter) {                          \
            f->f_state = FRAME_SUSPENDED;                                   \
            if (f->f_gen != NULL && (co->co_flags & CO_COROUTINE)) {        \
                _PyAwaitable_SetAwaiter(coro_or_result, f->f_gen);          \
            }                                                               \
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);         \
            retval = ((Ci_PyWaitHandleObject*)r)->wh_waiter;                   \
            Ci_PyWaitHandle_Release(r);                                       \
            assert(f->f_lasti > 0);                                            \
            f->f_lasti = INSTR_OFFSET() + 1;                                                    \
            goto exiting;                                             \
        }                                                                   \
        else {                                                              \
            Ci_PyWaitHandle_Release(r);                                       \
            f->f_state = FRAME_EXECUTING;                                     \
            assert(_Py_OPCODE(*(next_instr + 2)) == YIELD_FROM);            \
            next_instr += 3;                                                \
            DISPATCH();                                                     \
        }

PyStatus
_PyEval_InitGIL(PyThreadState *tstate)
{
#ifndef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    if (!_Py_IsMainInterpreter(tstate->interp)) {
        /* Currently, the GIL is shared by all interpreters,
           and only the main interpreter is responsible to create
           and destroy it. */
        return _PyStatus_OK();
    }
#endif

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    struct _gil_runtime_state *gil = &tstate->interp->ceval.gil;
#else
    struct _gil_runtime_state *gil = &tstate->interp->runtime->ceval.gil;
#endif
    assert(!gil_created(gil));

    PyThread_init_thread();
    create_gil(gil);

    take_gil(tstate);

    assert(gil_created(gil));
    return _PyStatus_OK();
}

void
_PyEval_FiniGIL(PyInterpreterState *interp)
{
#ifndef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    if (!_Py_IsMainInterpreter(interp)) {
        /* Currently, the GIL is shared by all interpreters,
           and only the main interpreter is responsible to create
           and destroy it. */
        return;
    }
#endif

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    struct _gil_runtime_state *gil = &interp->ceval.gil;
#else
    struct _gil_runtime_state *gil = &interp->runtime->ceval.gil;
#endif
    if (!gil_created(gil)) {
        /* First Py_InitializeFromConfig() call: the GIL doesn't exist
           yet: do nothing. */
        return;
    }

    destroy_gil(gil);
    assert(!gil_created(gil));
}

void
PyEval_InitThreads(void)
{
    /* Do nothing: kept for backward compatibility */
}

void
_PyEval_Fini(void)
{
#if OPCACHE_STATS
    fprintf(stderr, "-- Opcode cache number of objects  = %zd\n",
            opcache_code_objects);

    fprintf(stderr, "-- Opcode cache total extra mem    = %zd\n",
            opcache_code_objects_extra_mem);

    fprintf(stderr, "\n");

    fprintf(stderr, "-- Opcode cache LOAD_GLOBAL hits   = %zd (%d%%)\n",
            opcache_global_hits,
            (int) (100.0 * opcache_global_hits /
                (opcache_global_hits + opcache_global_misses)));

    fprintf(stderr, "-- Opcode cache LOAD_GLOBAL misses = %zd (%d%%)\n",
            opcache_global_misses,
            (int) (100.0 * opcache_global_misses /
                (opcache_global_hits + opcache_global_misses)));

    fprintf(stderr, "-- Opcode cache LOAD_GLOBAL opts   = %zd\n",
            opcache_global_opts);

    fprintf(stderr, "\n");

    fprintf(stderr, "-- Opcode cache LOAD_ATTR hits     = %zd (%d%%)\n",
            opcache_attr_hits,
            (int) (100.0 * opcache_attr_hits /
                opcache_attr_total));

    fprintf(stderr, "-- Opcode cache LOAD_ATTR misses   = %zd (%d%%)\n",
            opcache_attr_misses,
            (int) (100.0 * opcache_attr_misses /
                opcache_attr_total));

    fprintf(stderr, "-- Opcode cache LOAD_ATTR opts     = %zd\n",
            opcache_attr_opts);

    fprintf(stderr, "-- Opcode cache LOAD_ATTR deopts   = %zd\n",
            opcache_attr_deopts);

    fprintf(stderr, "-- Opcode cache LOAD_ATTR total    = %zd\n",
            opcache_attr_total);
#endif
}

void
PyEval_AcquireLock(void)
{
    _PyRuntimeState *runtime = &_PyRuntime;
    PyThreadState *tstate = _PyRuntimeState_GetThreadState(runtime);
    _Py_EnsureTstateNotNULL(tstate);

    take_gil(tstate);
}

void
PyEval_ReleaseLock(void)
{
    _PyRuntimeState *runtime = &_PyRuntime;
    PyThreadState *tstate = _PyRuntimeState_GetThreadState(runtime);
    /* This function must succeed when the current thread state is NULL.
       We therefore avoid PyThreadState_Get() which dumps a fatal error
       in debug mode. */
    struct _ceval_runtime_state *ceval = &runtime->ceval;
    struct _ceval_state *ceval2 = &tstate->interp->ceval;
    drop_gil(ceval, ceval2, tstate);
}

void
_PyEval_ReleaseLock(PyThreadState *tstate)
{
    struct _ceval_runtime_state *ceval = &tstate->interp->runtime->ceval;
    struct _ceval_state *ceval2 = &tstate->interp->ceval;
    drop_gil(ceval, ceval2, tstate);
}

void
PyEval_AcquireThread(PyThreadState *tstate)
{
    _Py_EnsureTstateNotNULL(tstate);

    take_gil(tstate);

    struct _gilstate_runtime_state *gilstate = &tstate->interp->runtime->gilstate;
#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    (void)_PyThreadState_Swap(gilstate, tstate);
#else
    if (_PyThreadState_Swap(gilstate, tstate) != NULL) {
        Py_FatalError("non-NULL old thread state");
    }
#endif
}

void
PyEval_ReleaseThread(PyThreadState *tstate)
{
    assert(is_tstate_valid(tstate));

    _PyRuntimeState *runtime = tstate->interp->runtime;
    PyThreadState *new_tstate = _PyThreadState_Swap(&runtime->gilstate, NULL);
    if (new_tstate != tstate) {
        Py_FatalError("wrong thread state");
    }
    struct _ceval_runtime_state *ceval = &runtime->ceval;
    struct _ceval_state *ceval2 = &tstate->interp->ceval;
    drop_gil(ceval, ceval2, tstate);
}

#ifdef HAVE_FORK
/* This function is called from PyOS_AfterFork_Child to destroy all threads
   which are not running in the child process, and clear internal locks
   which might be held by those threads. */
PyStatus
_PyEval_ReInitThreads(PyThreadState *tstate)
{
    _PyRuntimeState *runtime = tstate->interp->runtime;

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    struct _gil_runtime_state *gil = &tstate->interp->ceval.gil;
#else
    struct _gil_runtime_state *gil = &runtime->ceval.gil;
#endif
    if (!gil_created(gil)) {
        return _PyStatus_OK();
    }
    recreate_gil(gil);

    take_gil(tstate);

    struct _pending_calls *pending = &tstate->interp->ceval.pending;
    if (_PyThread_at_fork_reinit(&pending->lock) < 0) {
        return _PyStatus_ERR("Can't reinitialize pending calls lock");
    }

    /* Destroy all threads except the current one */
    _PyThreadState_DeleteExcept(runtime, tstate);
    return _PyStatus_OK();
}
#endif

/* This function is used to signal that async exceptions are waiting to be
   raised. */

void
_PyEval_SignalAsyncExc(PyInterpreterState *interp)
{
    SIGNAL_ASYNC_EXC(interp);
}

PyThreadState *
PyEval_SaveThread(void)
{
    _PyRuntimeState *runtime = &_PyRuntime;
#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    PyThreadState *old_tstate = _PyThreadState_GET();
    PyThreadState *tstate = _PyThreadState_Swap(&runtime->gilstate, old_tstate);
#else
    PyThreadState *tstate = _PyThreadState_Swap(&runtime->gilstate, NULL);
#endif
    _Py_EnsureTstateNotNULL(tstate);

    struct _ceval_runtime_state *ceval = &runtime->ceval;
    struct _ceval_state *ceval2 = &tstate->interp->ceval;
#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    assert(gil_created(&ceval2->gil));
#else
    assert(gil_created(&ceval->gil));
#endif
    drop_gil(ceval, ceval2, tstate);
    return tstate;
}

void
PyEval_RestoreThread(PyThreadState *tstate)
{
    _Py_EnsureTstateNotNULL(tstate);

    take_gil(tstate);

    struct _gilstate_runtime_state *gilstate = &tstate->interp->runtime->gilstate;
    _PyThreadState_Swap(gilstate, tstate);
}


/* Mechanism whereby asynchronously executing callbacks (e.g. UNIX
   signal handlers or Mac I/O completion routines) can schedule calls
   to a function to be called synchronously.
   The synchronous function is called with one void* argument.
   It should return 0 for success or -1 for failure -- failure should
   be accompanied by an exception.

   If registry succeeds, the registry function returns 0; if it fails
   (e.g. due to too many pending calls) it returns -1 (without setting
   an exception condition).

   Note that because registry may occur from within signal handlers,
   or other asynchronous events, calling malloc() is unsafe!

   Any thread can schedule pending calls, but only the main thread
   will execute them.
   There is no facility to schedule calls to a particular thread, but
   that should be easy to change, should that ever be required.  In
   that case, the static variables here should go into the python
   threadstate.
*/

void
_PyEval_SignalReceived(PyInterpreterState *interp)
{
#ifdef MS_WINDOWS
    // bpo-42296: On Windows, _PyEval_SignalReceived() is called from a signal
    // handler which can run in a thread different than the Python thread, in
    // which case _Py_ThreadCanHandleSignals() is wrong. Ignore
    // _Py_ThreadCanHandleSignals() and always set eval_breaker to 1.
    //
    // The next eval_frame_handle_pending() call will call
    // _Py_ThreadCanHandleSignals() to recompute eval_breaker.
    int force = 1;
#else
    int force = 0;
#endif
    /* bpo-30703: Function called when the C signal handler of Python gets a
       signal. We cannot queue a callback using _PyEval_AddPendingCall() since
       that function is not async-signal-safe. */
    SIGNAL_PENDING_SIGNALS(interp, force);
}

/* Push one item onto the queue while holding the lock. */
static int
_push_pending_call(struct _pending_calls *pending,
                   int (*func)(void *), void *arg)
{
    int i = pending->last;
    int j = (i + 1) % NPENDINGCALLS;
    if (j == pending->first) {
        return -1; /* Queue full */
    }
    pending->calls[i].func = func;
    pending->calls[i].arg = arg;
    pending->last = j;
    return 0;
}

/* Pop one item off the queue while holding the lock. */
static void
_pop_pending_call(struct _pending_calls *pending,
                  int (**func)(void *), void **arg)
{
    int i = pending->first;
    if (i == pending->last) {
        return; /* Queue empty */
    }

    *func = pending->calls[i].func;
    *arg = pending->calls[i].arg;
    pending->first = (i + 1) % NPENDINGCALLS;
}

/* This implementation is thread-safe.  It allows
   scheduling to be made from any thread, and even from an executing
   callback.
 */

int
_PyEval_AddPendingCall(PyInterpreterState *interp,
                       int (*func)(void *), void *arg)
{
    struct _pending_calls *pending = &interp->ceval.pending;

    /* Ensure that _PyEval_InitPendingCalls() was called
       and that _PyEval_FiniPendingCalls() is not called yet. */
    assert(pending->lock != NULL);

    PyThread_acquire_lock(pending->lock, WAIT_LOCK);
    int result = _push_pending_call(pending, func, arg);
    PyThread_release_lock(pending->lock);

    /* signal main loop */
    SIGNAL_PENDING_CALLS(interp);
    return result;
}

int
Py_AddPendingCall(int (*func)(void *), void *arg)
{
    /* Best-effort to support subinterpreters and calls with the GIL released.

       First attempt _PyThreadState_GET() since it supports subinterpreters.

       If the GIL is released, _PyThreadState_GET() returns NULL . In this
       case, use PyGILState_GetThisThreadState() which works even if the GIL
       is released.

       Sadly, PyGILState_GetThisThreadState() doesn't support subinterpreters:
       see bpo-10915 and bpo-15751.

       Py_AddPendingCall() doesn't require the caller to hold the GIL. */
    PyThreadState *tstate = _PyThreadState_GET();
    if (tstate == NULL) {
        tstate = PyGILState_GetThisThreadState();
    }

    PyInterpreterState *interp;
    if (tstate != NULL) {
        interp = tstate->interp;
    }
    else {
        /* Last resort: use the main interpreter */
        interp = _PyRuntime.interpreters.main;
    }
    return _PyEval_AddPendingCall(interp, func, arg);
}

static int
handle_signals(PyThreadState *tstate)
{
    assert(is_tstate_valid(tstate));
    if (!_Py_ThreadCanHandleSignals(tstate->interp)) {
        return 0;
    }

    UNSIGNAL_PENDING_SIGNALS(tstate->interp);
    if (_PyErr_CheckSignalsTstate(tstate) < 0) {
        /* On failure, re-schedule a call to handle_signals(). */
        SIGNAL_PENDING_SIGNALS(tstate->interp, 0);
        return -1;
    }
    return 0;
}

static int
make_pending_calls(PyInterpreterState *interp)
{
    /* only execute pending calls on main thread */
    if (!_Py_ThreadCanHandlePendingCalls()) {
        return 0;
    }

    /* don't perform recursive pending calls */
    static int busy = 0;
    if (busy) {
        return 0;
    }
    busy = 1;

    /* unsignal before starting to call callbacks, so that any callback
       added in-between re-signals */
    UNSIGNAL_PENDING_CALLS(interp);
    int res = 0;

    /* perform a bounded number of calls, in case of recursion */
    struct _pending_calls *pending = &interp->ceval.pending;
    for (int i=0; i<NPENDINGCALLS; i++) {
        int (*func)(void *) = NULL;
        void *arg = NULL;

        /* pop one item off the queue while holding the lock */
        PyThread_acquire_lock(pending->lock, WAIT_LOCK);
        _pop_pending_call(pending, &func, &arg);
        PyThread_release_lock(pending->lock);

        /* having released the lock, perform the callback */
        if (func == NULL) {
            break;
        }
        res = func(arg);
        if (res) {
            goto error;
        }
    }

    busy = 0;
    return res;

error:
    busy = 0;
    SIGNAL_PENDING_CALLS(interp);
    return res;
}

void
_Py_FinishPendingCalls(PyThreadState *tstate)
{
    assert(PyGILState_Check());
    assert(is_tstate_valid(tstate));

    struct _pending_calls *pending = &tstate->interp->ceval.pending;

    if (!_Py_atomic_load_relaxed(&(pending->calls_to_do))) {
        return;
    }

    if (make_pending_calls(tstate->interp) < 0) {
        PyObject *exc, *val, *tb;
        _PyErr_Fetch(tstate, &exc, &val, &tb);
        PyErr_BadInternalCall();
        _PyErr_ChainExceptions(exc, val, tb);
        _PyErr_Print(tstate);
    }
}

/* Py_MakePendingCalls() is a simple wrapper for the sake
   of backward-compatibility. */
int
Py_MakePendingCalls(void)
{
    assert(PyGILState_Check());

    PyThreadState *tstate = _PyThreadState_GET();
    assert(is_tstate_valid(tstate));

    /* Python signal handler doesn't really queue a callback: it only signals
       that a signal was received, see _PyEval_SignalReceived(). */
    int res = handle_signals(tstate);
    if (res != 0) {
        return res;
    }

    res = make_pending_calls(tstate->interp);
    if (res != 0) {
        return res;
    }

    return 0;
}

/* The interpreter's recursion limit */

#ifndef Py_DEFAULT_RECURSION_LIMIT
#  define Py_DEFAULT_RECURSION_LIMIT 1000
#endif

void
_PyEval_InitRuntimeState(struct _ceval_runtime_state *ceval)
{
#ifndef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    _gil_initialize(&ceval->gil);
#endif
}

int
_PyEval_InitState(struct _ceval_state *ceval)
{
    ceval->recursion_limit = Py_DEFAULT_RECURSION_LIMIT;

    struct _pending_calls *pending = &ceval->pending;
    assert(pending->lock == NULL);

    pending->lock = PyThread_allocate_lock();
    if (pending->lock == NULL) {
        return -1;
    }

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
    _gil_initialize(&ceval->gil);
#endif

    ceval->profile_instr_counter = 0;
    ceval->profile_instr_period = 1;

    return 0;
}

void
_PyEval_FiniState(struct _ceval_state *ceval)
{
    struct _pending_calls *pending = &ceval->pending;
    if (pending->lock != NULL) {
        PyThread_free_lock(pending->lock);
        pending->lock = NULL;
    }
}

int
Py_GetRecursionLimit(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return interp->ceval.recursion_limit;
}

void
Py_SetRecursionLimit(int new_limit)
{
    PyThreadState *tstate = _PyThreadState_GET();
    tstate->interp->ceval.recursion_limit = new_limit;
}

/* The function _Py_EnterRecursiveCall() only calls _Py_CheckRecursiveCall()
   if the recursion_depth reaches recursion_limit.
   If USE_STACKCHECK, the macro decrements recursion_limit
   to guarantee that _Py_CheckRecursiveCall() is regularly called.
   Without USE_STACKCHECK, there is no need for this. */
int
_Py_CheckRecursiveCall(PyThreadState *tstate, const char *where)
{
    int recursion_limit = tstate->interp->ceval.recursion_limit;

#ifdef USE_STACKCHECK
    tstate->stackcheck_counter = 0;
    if (PyOS_CheckStack()) {
        --tstate->recursion_depth;
        _PyErr_SetString(tstate, PyExc_MemoryError, "Stack overflow");
        return -1;
    }
#endif
    if (tstate->recursion_headroom) {
        if (tstate->recursion_depth > recursion_limit + 50) {
            /* Overflowing while handling an overflow. Give up. */
            Py_FatalError("Cannot recover from stack overflow.");
        }
    }
    else {
        if (tstate->recursion_depth > recursion_limit) {
            tstate->recursion_headroom++;
            _PyErr_Format(tstate, PyExc_RecursionError,
                        "maximum recursion depth exceeded%s",
                        where);
            tstate->recursion_headroom--;
            --tstate->recursion_depth;
            return -1;
        }
    }
    return 0;
}


// PEP 634: Structural Pattern Matching


// Return a tuple of values corresponding to keys, with error checks for
// duplicate/missing keys.
PyObject*
Ci_match_keys(PyThreadState *tstate, PyObject *map, PyObject *keys)
{
    assert(PyTuple_CheckExact(keys));
    Py_ssize_t nkeys = PyTuple_GET_SIZE(keys);
    if (!nkeys) {
        // No keys means no items.
        return PyTuple_New(0);
    }
    PyObject *seen = NULL;
    PyObject *dummy = NULL;
    PyObject *values = NULL;
    // We use the two argument form of map.get(key, default) for two reasons:
    // - Atomically check for a key and get its value without error handling.
    // - Don't cause key creation or resizing in dict subclasses like
    //   collections.defaultdict that define __missing__ (or similar).
    _Py_IDENTIFIER(get);
    PyObject *get = _PyObject_GetAttrId(map, &PyId_get);
    if (get == NULL) {
        goto fail;
    }
    seen = PySet_New(NULL);
    if (seen == NULL) {
        goto fail;
    }
    // dummy = object()
    dummy = _PyObject_CallNoArg((PyObject *)&PyBaseObject_Type);
    if (dummy == NULL) {
        goto fail;
    }
    values = PyList_New(0);
    if (values == NULL) {
        goto fail;
    }
    for (Py_ssize_t i = 0; i < nkeys; i++) {
        PyObject *key = PyTuple_GET_ITEM(keys, i);
        if (PySet_Contains(seen, key) || PySet_Add(seen, key)) {
            if (!_PyErr_Occurred(tstate)) {
                // Seen it before!
                _PyErr_Format(tstate, PyExc_ValueError,
                              "mapping pattern checks duplicate key (%R)", key);
            }
            goto fail;
        }
        PyObject *value = PyObject_CallFunctionObjArgs(get, key, dummy, NULL);
        if (value == NULL) {
            goto fail;
        }
        if (value == dummy) {
            // key not in map!
            Py_DECREF(value);
            Py_DECREF(values);
            // Return None:
            Py_INCREF(Py_None);
            values = Py_None;
            goto done;
        }
        PyList_Append(values, value);
        Py_DECREF(value);
    }
    Py_SETREF(values, PyList_AsTuple(values));
    // Success:
done:
    Py_DECREF(get);
    Py_DECREF(seen);
    Py_DECREF(dummy);
    return values;
fail:
    Py_XDECREF(get);
    Py_XDECREF(seen);
    Py_XDECREF(dummy);
    Py_XDECREF(values);
    return NULL;
}

// Extract a named attribute from the subject, with additional bookkeeping to
// raise TypeErrors for repeated lookups. On failure, return NULL (with no
// error set). Use _PyErr_Occurred(tstate) to disambiguate.
static PyObject*
match_class_attr(PyThreadState *tstate, PyObject *subject, PyObject *type,
                 PyObject *name, PyObject *seen)
{
    assert(PyUnicode_CheckExact(name));
    assert(PySet_CheckExact(seen));
    if (PySet_Contains(seen, name) || PySet_Add(seen, name)) {
        if (!_PyErr_Occurred(tstate)) {
            // Seen it before!
            _PyErr_Format(tstate, PyExc_TypeError,
                          "%s() got multiple sub-patterns for attribute %R",
                          ((PyTypeObject*)type)->tp_name, name);
        }
        return NULL;
    }
    PyObject *attr = PyObject_GetAttr(subject, name);
    if (attr == NULL && _PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
        _PyErr_Clear(tstate);
    }
    return attr;
}

// On success (match), return a tuple of extracted attributes. On failure (no
// match), return NULL. Use _PyErr_Occurred(tstate) to disambiguate.
PyObject*
Ci_match_class(PyThreadState *tstate, PyObject *subject, PyObject *type,
            Py_ssize_t nargs, PyObject *kwargs)
{
    if (!PyType_Check(type)) {
        const char *e = "called match pattern must be a type";
        _PyErr_Format(tstate, PyExc_TypeError, e);
        return NULL;
    }
    assert(PyTuple_CheckExact(kwargs));
    // First, an isinstance check:
    if (PyObject_IsInstance(subject, type) <= 0) {
        return NULL;
    }
    // So far so good:
    PyObject *seen = PySet_New(NULL);
    if (seen == NULL) {
        return NULL;
    }
    PyObject *attrs = PyList_New(0);
    if (attrs == NULL) {
        Py_DECREF(seen);
        return NULL;
    }
    // NOTE: From this point on, goto fail on failure:
    PyObject *match_args = NULL;
    // First, the positional subpatterns:
    if (nargs) {
        int match_self = 0;
        match_args = PyObject_GetAttrString(type, "__match_args__");
        if (match_args) {
            if (!PyTuple_CheckExact(match_args)) {
                const char *e = "%s.__match_args__ must be a tuple (got %s)";
                _PyErr_Format(tstate, PyExc_TypeError, e,
                              ((PyTypeObject *)type)->tp_name,
                              Py_TYPE(match_args)->tp_name);
                goto fail;
            }
        }
        else if (_PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
            _PyErr_Clear(tstate);
            // _Py_TPFLAGS_MATCH_SELF is only acknowledged if the type does not
            // define __match_args__. This is natural behavior for subclasses:
            // it's as if __match_args__ is some "magic" value that is lost as
            // soon as they redefine it.
            match_args = PyTuple_New(0);
            match_self = PyType_HasFeature((PyTypeObject*)type,
                                            _Py_TPFLAGS_MATCH_SELF);
        }
        else {
            goto fail;
        }
        assert(PyTuple_CheckExact(match_args));
        Py_ssize_t allowed = match_self ? 1 : PyTuple_GET_SIZE(match_args);
        if (allowed < nargs) {
            const char *plural = (allowed == 1) ? "" : "s";
            _PyErr_Format(tstate, PyExc_TypeError,
                          "%s() accepts %d positional sub-pattern%s (%d given)",
                          ((PyTypeObject*)type)->tp_name,
                          allowed, plural, nargs);
            goto fail;
        }
        if (match_self) {
            // Easy. Copy the subject itself, and move on to kwargs.
            PyList_Append(attrs, subject);
        }
        else {
            for (Py_ssize_t i = 0; i < nargs; i++) {
                PyObject *name = PyTuple_GET_ITEM(match_args, i);
                if (!PyUnicode_CheckExact(name)) {
                    _PyErr_Format(tstate, PyExc_TypeError,
                                  "__match_args__ elements must be strings "
                                  "(got %s)", Py_TYPE(name)->tp_name);
                    goto fail;
                }
                PyObject *attr = match_class_attr(tstate, subject, type, name,
                                                  seen);
                if (attr == NULL) {
                    goto fail;
                }
                PyList_Append(attrs, attr);
                Py_DECREF(attr);
            }
        }
        Py_CLEAR(match_args);
    }
    // Finally, the keyword subpatterns:
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(kwargs); i++) {
        PyObject *name = PyTuple_GET_ITEM(kwargs, i);
        PyObject *attr = match_class_attr(tstate, subject, type, name, seen);
        if (attr == NULL) {
            goto fail;
        }
        PyList_Append(attrs, attr);
        Py_DECREF(attr);
    }
    Py_SETREF(attrs, PyList_AsTuple(attrs));
    Py_DECREF(seen);
    return attrs;
fail:
    // We really don't care whether an error was raised or not... that's our
    // caller's problem. All we know is that the match failed.
    Py_XDECREF(match_args);
    Py_DECREF(seen);
    Py_DECREF(attrs);
    return NULL;
}


static int unpack_iterable(PyThreadState *, PyObject *, int, int, PyObject **);

#ifdef ENABLE_CINDERX
static inline void store_field(int field_type, void *addr, PyObject *value);
static inline PyObject *load_field(int field_type, void *addr);
static inline PyObject *
box_primitive(int field_type, Py_ssize_t value);
#endif

PyObject *
PyEval_EvalCode(PyObject *co, PyObject *globals, PyObject *locals)
{
    PyThreadState *tstate = PyThreadState_GET();
    if (locals == NULL) {
        locals = globals;
    }
    PyObject *builtins = _PyEval_BuiltinsFromGlobals(tstate, globals); // borrowed ref
    if (builtins == NULL) {
        return NULL;
    }
    PyFrameConstructor desc = {
        .fc_globals = globals,
        .fc_builtins = builtins,
        .fc_name = ((PyCodeObject *)co)->co_name,
        .fc_qualname = ((PyCodeObject *)co)->co_name,
        .fc_code = co,
        .fc_defaults = NULL,
        .fc_kwdefaults = NULL,
        .fc_closure = NULL
    };
    return _PyEval_Vector(tstate, &desc, locals, NULL, 0, NULL);
}


/* Interpreter main loop */

PyObject *
PyEval_EvalFrame(PyFrameObject *f)
{
    /* Function kept for backward compatibility */
    PyThreadState *tstate = _PyThreadState_GET();
    return _PyEval_EvalFrame(tstate, f, 0);
}

PyObject *
PyEval_EvalFrameEx(PyFrameObject *f, int throwflag)
{
    PyThreadState *tstate = _PyThreadState_GET();
    return _PyEval_EvalFrame(tstate, f, throwflag);
}

// steals the reference to frame
PyObject *
_PyEval_EvalEagerCoro(PyThreadState *tstate, struct _frame *f, PyObject *name, PyObject *qualname)
{
#define RELEASE_EXC_INFO(exc_info)                                            \
    Py_XDECREF((exc_info).exc_type);                                          \
    Py_XDECREF((exc_info).exc_value);                                         \
    Py_XDECREF((exc_info).exc_traceback);

#define Ci_RELEASE_FRAME(tstate, f)                                              \
    if (Py_REFCNT(f) > 1) {                                                   \
        Py_DECREF(f);                                                         \
        _PyObject_GC_TRACK(f);                                                \
    } else {                                                                  \
        ++tstate->recursion_depth;                                            \
        Py_DECREF(f);                                                         \
        --tstate->recursion_depth;                                            \
    }

    _PyErr_StackItem *previous_exc_info = tstate->exc_info;
    _PyErr_StackItem exc_info = {.exc_type = NULL,
                                 .exc_value = NULL,
                                 .exc_traceback = NULL,
                                 .previous_item = previous_exc_info};
    tstate->exc_info = &exc_info;
    f->f_valuestack[(f->f_stackdepth)++] = Py_None;
    Py_INCREF(Py_None);
    f->f_state = FRAME_EXECUTING;
    PyObject *retval = PyEval_EvalFrameEx(f, 0);
    tstate->exc_info = previous_exc_info;
    if (!retval) {
        f->f_state = FRAME_SUSPENDED;
        RELEASE_EXC_INFO(exc_info);
        Ci_RELEASE_FRAME(tstate, f);
        return NULL;
    }
    if (f->f_stackdepth != 0) {
        PyCoroObject *coro =
            (PyCoroObject*)_PyCoro_ForFrame(tstate, f, name, qualname);
        if (coro == NULL) {
            RELEASE_EXC_INFO(exc_info);
            Ci_RELEASE_FRAME(tstate, f);
            return NULL;
        }
        coro->cr_exc_state = exc_info;
        PyObject *yf = _PyGen_yf((PyGenObject *)coro);
        f->f_state = FRAME_SUSPENDED;
        if (yf) {
            _PyAwaitable_SetAwaiter(yf, (PyObject *)coro);
            Py_DECREF(yf);
        }
        return Ci_PyWaitHandle_New((PyObject *)coro, retval);
    }
    RELEASE_EXC_INFO(exc_info);
    Ci_RELEASE_FRAME(tstate, f);

#undef RELEASE_EXC_INFO
#undef Ci_RELEASE_FRAME

    return Ci_PyWaitHandle_New(retval, NULL);
}

#ifdef ENABLE_CINDERX
static inline Py_ssize_t
unbox_primitive_int_and_decref(PyObject *x)
{
    assert(PyLong_Check(x));
    Py_ssize_t res = (Py_ssize_t)PyLong_AsVoidPtr(x);
    Py_DECREF(x);
    return res;
}
#endif

/* Handle signals, pending calls, GIL drop request
   and asynchronous exception */
int
eval_frame_handle_pending(PyThreadState *tstate)
{
    _PyRuntimeState * const runtime = &_PyRuntime;
    struct _ceval_runtime_state *ceval = &runtime->ceval;

    /* Pending signals */
    if (_Py_atomic_load_relaxed(&ceval->signals_pending)) {
        if (handle_signals(tstate) != 0) {
            return -1;
        }
    }

    /* Pending calls */
    struct _ceval_state *ceval2 = &tstate->interp->ceval;
    if (_Py_atomic_load_relaxed(&ceval2->pending.calls_to_do)) {
        if (make_pending_calls(tstate->interp) != 0) {
            return -1;
        }
    }

    /* GIL drop request */
    if (_Py_atomic_load_relaxed(&ceval2->gil_drop_request)) {
        /* Give another thread a chance */
        if (_PyThreadState_Swap(&runtime->gilstate, NULL) != tstate) {
            Py_FatalError("tstate mix-up");
        }
        drop_gil(ceval, ceval2, tstate);

        /* Other threads may run now */

        take_gil(tstate);

#ifdef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
        (void)_PyThreadState_Swap(&runtime->gilstate, tstate);
#else
        if (_PyThreadState_Swap(&runtime->gilstate, tstate) != NULL) {
            Py_FatalError("orphan tstate");
        }
#endif
    }

    /* Check for asynchronous exception. */
    if (tstate->async_exc != NULL) {
        PyObject *exc = tstate->async_exc;
        tstate->async_exc = NULL;
        UNSIGNAL_ASYNC_EXC(tstate->interp);
        _PyErr_SetNone(tstate, exc);
        Py_DECREF(exc);
        return -1;
    }

#ifdef MS_WINDOWS
    // bpo-42296: On Windows, _PyEval_SignalReceived() can be called in a
    // different thread than the Python thread, in which case
    // _Py_ThreadCanHandleSignals() is wrong. Recompute eval_breaker in the
    // current Python thread with the correct _Py_ThreadCanHandleSignals()
    // value. It prevents to interrupt the eval loop at every instruction if
    // the current Python thread cannot handle signals (if
    // _Py_ThreadCanHandleSignals() is false).
    COMPUTE_EVAL_BREAKER(tstate->interp, ceval, ceval2);
#endif

    return 0;
}

#ifdef ENABLE_CINDERX
static PyObject *
invoke_static_function(PyObject *func, PyObject **args, Py_ssize_t nargs, int awaited) {
    return _PyObject_Vectorcall(
        func,
        args,
        (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) | nargs,
        NULL);
}
#endif

/* Computed GOTOs, or
       the-optimization-commonly-but-improperly-known-as-"threaded code"
   using gcc's labels-as-values extension
   (http://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html).

   The traditional bytecode evaluation loop uses a "switch" statement, which
   decent compilers will optimize as a single indirect branch instruction
   combined with a lookup table of jump addresses. However, since the
   indirect jump instruction is shared by all opcodes, the CPU will have a
   hard time making the right prediction for where to jump next (actually,
   it will be always wrong except in the uncommon case of a sequence of
   several identical opcodes).

   "Threaded code" in contrast, uses an explicit jump table and an explicit
   indirect jump instruction at the end of each opcode. Since the jump
   instruction is at a different address for each opcode, the CPU will make a
   separate prediction for each of these instructions, which is equivalent to
   predicting the second opcode of each opcode pair. These predictions have
   a much better chance to turn out valid, especially in small bytecode loops.

   A mispredicted branch on a modern CPU flushes the whole pipeline and
   can cost several CPU cycles (depending on the pipeline depth),
   and potentially many more instructions (depending on the pipeline width).
   A correctly predicted branch, however, is nearly free.

   At the time of this writing, the "threaded code" version is up to 15-20%
   faster than the normal "switch" version, depending on the compiler and the
   CPU architecture.

   We disable the optimization if DYNAMIC_EXECUTION_PROFILE is defined,
   because it would render the measurements invalid.


   NOTE: care must be taken that the compiler doesn't try to "optimize" the
   indirect jumps by sharing them between all opcodes. Such optimizations
   can be disabled on gcc by using the -fno-gcse flag (or possibly
   -fno-crossjumping).
*/

/* Use macros rather than inline functions, to make it as clear as possible
 * to the C compiler that the tracing check is a simple test then branch.
 * We want to be sure that the compiler knows this before it generates
 * the CFG.
 */
#ifdef LLTRACE
#define OR_LLTRACE || lltrace
#else
#define OR_LLTRACE
#endif

#ifdef WITH_DTRACE
#define OR_DTRACE_LINE || PyDTrace_LINE_ENABLED()
#else
#define OR_DTRACE_LINE
#endif

#ifdef DYNAMIC_EXECUTION_PROFILE
#undef USE_COMPUTED_GOTOS
#define USE_COMPUTED_GOTOS 0
#endif

#ifdef HAVE_COMPUTED_GOTOS
    #ifndef USE_COMPUTED_GOTOS
    #define USE_COMPUTED_GOTOS 1
    #endif
#else
    #if defined(USE_COMPUTED_GOTOS) && USE_COMPUTED_GOTOS
    #error "Computed gotos are not supported on this compiler."
    #endif
    #undef USE_COMPUTED_GOTOS
    #define USE_COMPUTED_GOTOS 0
#endif

#if USE_COMPUTED_GOTOS
#define TARGET(op) op: TARGET_##op
#define DISPATCH() \
    { \
        if (trace_info.cframe.use_tracing OR_DTRACE_LINE OR_LLTRACE) { \
            goto tracing_dispatch; \
        } \
        f->f_lasti = INSTR_OFFSET(); \
        NEXTOPARG(); \
        goto *opcode_targets[opcode]; \
    }
#else
#define TARGET(op) op
#define DISPATCH() goto predispatch;
#endif


#define CHECK_EVAL_BREAKER() \
    if (_Py_atomic_load_relaxed(eval_breaker)) { \
        continue; \
    }


/* Tuple access macros */

#ifndef Py_DEBUG
#define GETITEM(v, i) PyTuple_GET_ITEM((PyTupleObject *)(v), (i))
#else
#define GETITEM(v, i) PyTuple_GetItem((v), (i))
#endif

/* Code access macros */

/* The integer overflow is checked by an assertion below. */
#define INSTR_OFFSET() ((int)(next_instr - first_instr))
#define NEXTOPARG()  do { \
        _Py_CODEUNIT word = *next_instr; \
        opcode = _Py_OPCODE(word); \
        oparg = _Py_OPARG(word); \
        next_instr++; \
    } while (0)
#define JUMPTO(x)       (next_instr = first_instr + (x))
#define JUMPBY(x)       (next_instr += (x))

/* OpCode prediction macros
    Some opcodes tend to come in pairs thus making it possible to
    predict the second code when the first is run.  For example,
    COMPARE_OP is often followed by POP_JUMP_IF_FALSE or POP_JUMP_IF_TRUE.

    Verifying the prediction costs a single high-speed test of a register
    variable against a constant.  If the pairing was good, then the
    processor's own internal branch predication has a high likelihood of
    success, resulting in a nearly zero-overhead transition to the
    next opcode.  A successful prediction saves a trip through the eval-loop
    including its unpredictable switch-case branch.  Combined with the
    processor's internal branch prediction, a successful PREDICT has the
    effect of making the two opcodes run as if they were a single new opcode
    with the bodies combined.

    If collecting opcode statistics, your choices are to either keep the
    predictions turned-on and interpret the results as if some opcodes
    had been combined or turn-off predictions so that the opcode frequency
    counter updates for both opcodes.

    Opcode prediction is disabled with threaded code, since the latter allows
    the CPU to record separate branch prediction information for each
    opcode.

*/

#define PREDICT_ID(op)          PRED_##op

#if defined(DYNAMIC_EXECUTION_PROFILE) || USE_COMPUTED_GOTOS
#define PREDICT(op)             if (0) goto PREDICT_ID(op)
#else
#define PREDICT(op) \
    do { \
        _Py_CODEUNIT word = *next_instr; \
        opcode = _Py_OPCODE(word); \
        if (opcode == op) { \
            oparg = _Py_OPARG(word); \
            next_instr++; \
            goto PREDICT_ID(op); \
        } \
    } while(0)
#endif
#define PREDICTED(op)           PREDICT_ID(op):


/* Stack manipulation macros */

/* The stack can grow at most MAXINT deep, as co_nlocals and
   co_stacksize are ints. */
#define STACK_LEVEL()     ((int)(stack_pointer - f->f_valuestack))
#define EMPTY()           (STACK_LEVEL() == 0)
#define TOP()             (stack_pointer[-1])
#define SECOND()          (stack_pointer[-2])
#define THIRD()           (stack_pointer[-3])
#define FOURTH()          (stack_pointer[-4])
#define PEEK(n)           (stack_pointer[-(n)])
#define SET_TOP(v)        (stack_pointer[-1] = (v))
#define SET_SECOND(v)     (stack_pointer[-2] = (v))
#define SET_THIRD(v)      (stack_pointer[-3] = (v))
#define SET_FOURTH(v)     (stack_pointer[-4] = (v))
#define BASIC_STACKADJ(n) (stack_pointer += n)
#define BASIC_PUSH(v)     (*stack_pointer++ = (v))
#define BASIC_POP()       (*--stack_pointer)

#ifdef LLTRACE
#define PUSH(v)         { (void)(BASIC_PUSH(v), \
                          lltrace && prtrace(tstate, TOP(), "push")); \
                          assert(STACK_LEVEL() <= co->co_stacksize); }
#define POP()           ((void)(lltrace && prtrace(tstate, TOP(), "pop")), \
                         BASIC_POP())
#define STACK_GROW(n)   do { \
                          assert(n >= 0); \
                          (void)(BASIC_STACKADJ(n), \
                          lltrace && prtrace(tstate, TOP(), "stackadj")); \
                          assert(STACK_LEVEL() <= co->co_stacksize); \
                        } while (0)
#define STACK_SHRINK(n) do { \
                            assert(n >= 0); \
                            (void)(lltrace && prtrace(tstate, TOP(), "stackadj")); \
                            (void)(BASIC_STACKADJ(-n)); \
                            assert(STACK_LEVEL() <= co->co_stacksize); \
                        } while (0)
#define EXT_POP(STACK_POINTER) ((void)(lltrace && \
                                prtrace(tstate, (STACK_POINTER)[-1], "ext_pop")), \
                                *--(STACK_POINTER))
#else
#define PUSH(v)                BASIC_PUSH(v)
#define POP()                  BASIC_POP()
#define STACK_GROW(n)          BASIC_STACKADJ(n)
#define STACK_SHRINK(n)        BASIC_STACKADJ(-n)
#define EXT_POP(STACK_POINTER) (*--(STACK_POINTER))
#endif

/* Local variable macros */

#define GETLOCAL(i)     (fastlocals[i])

/* The SETLOCAL() macro must not DECREF the local variable in-place and
   then store the new value; it must copy the old value to a temporary
   value, then store the new value, and then DECREF the temporary value.
   This is because it is possible that during the DECREF the frame is
   accessed by other code (e.g. a __del__ method or gc.collect()) and the
   variable would be pointing to already-freed memory. */
#define SETLOCAL(i, value)      do { PyObject *tmp = GETLOCAL(i); \
                                     GETLOCAL(i) = value; \
                                     Py_XDECREF(tmp); } while (0)


#define UNWIND_BLOCK(b) \
    while (STACK_LEVEL() > (b)->b_level) { \
        PyObject *v = POP(); \
        Py_XDECREF(v); \
    }

#define UNWIND_EXCEPT_HANDLER(b) \
    do { \
        PyObject *type, *value, *traceback; \
        _PyErr_StackItem *exc_info; \
        assert(STACK_LEVEL() >= (b)->b_level + 3); \
        while (STACK_LEVEL() > (b)->b_level + 3) { \
            value = POP(); \
            Py_XDECREF(value); \
        } \
        exc_info = tstate->exc_info; \
        type = exc_info->exc_type; \
        value = exc_info->exc_value; \
        traceback = exc_info->exc_traceback; \
        exc_info->exc_type = POP(); \
        exc_info->exc_value = POP(); \
        exc_info->exc_traceback = POP(); \
        Py_XDECREF(type); \
        Py_XDECREF(value); \
        Py_XDECREF(traceback); \
    } while(0)


extern PyObject * Ci_Super_Lookup(PyTypeObject *type,
                                  PyObject *obj,
                                  PyObject *name,
                                  PyObject *super_instance,
                                  int *meth_found);
inline PyObject *
Ci_SuperLookupMethodOrAttr(PyThreadState *tstate,
                           PyObject *global_super,
                           PyTypeObject *type,
                           PyObject *self,
                           PyObject *name,
                           int call_no_args,
                           int *meth_found)
{
    if (global_super != (PyObject *)&PySuper_Type) {
        PyObject *super_instance;
        if (call_no_args) {
            super_instance = _PyObject_VectorcallTstate(tstate, global_super, NULL, 0, NULL);
        }
        else {
            PyObject *args[] = { (PyObject *)type, self };
            super_instance = _PyObject_VectorcallTstate(tstate, global_super, args, 2, NULL);
        }
        if (super_instance == NULL) {
            return NULL;
        }
        PyObject *result = PyObject_GetAttr(super_instance, name);
        Py_DECREF(super_instance);
        if (result == NULL) {
            return NULL;
        }
        if (meth_found) {
            *meth_found = 0;
        }
        return result;
    }
    if (type->tp_getattro != PyObject_GenericGetAttr) {
        meth_found = NULL;
    }
    return Ci_Super_Lookup(type, self, name, NULL, meth_found);
}

#define PYSHADOW_INIT_THRESHOLD 50

PyObject* _Py_HOT_FUNCTION
_PyEval_EvalFrameDefault(PyThreadState *tstate, PyFrameObject *f, int throwflag)
{
    _Py_EnsureTstateNotNULL(tstate);

#if USE_COMPUTED_GOTOS
/* Import the static jump table */
#include "opcode_targets.h"
#endif

#ifdef DXPAIRS
    int lastopcode = 0;
#endif
    PyObject **stack_pointer;  /* Next free slot in value stack */
    const _Py_CODEUNIT *next_instr;
    int opcode;        /* Current opcode */
    int oparg;         /* Current opcode argument, if any */
    PyObject **fastlocals, **freevars;
    PyObject *retval = NULL;            /* Return value */
    _Py_atomic_int * const eval_breaker = &tstate->interp->ceval.eval_breaker;
    PyCodeObject *co;
    _PyShadowFrame shadow_frame;
#ifdef ENABLE_CINDERX
    Py_ssize_t profiled_instrs = 0;
#endif

    const _Py_CODEUNIT *first_instr;
    PyObject *names;
    PyObject *consts;
    _PyShadow_EvalState shadow = {}; /* facebook T39538061 */

#ifdef LLTRACE
    _Py_IDENTIFIER(__ltrace__);
#endif

    if (_Py_EnterRecursiveCall(tstate, "")) {
        return NULL;
    }

    PyTraceInfo trace_info;
    /* Mark trace_info as uninitialized */
    trace_info.code = NULL;

    /* WARNING: Because the CFrame lives on the C stack,
     * but can be accessed from a heap allocated object (tstate)
     * strict stack discipline must be maintained.
     */
    CFrame *prev_cframe = tstate->cframe;
    trace_info.cframe.use_tracing = prev_cframe->use_tracing;
    trace_info.cframe.previous = prev_cframe;
    tstate->cframe = &trace_info.cframe;

#ifdef ENABLE_CINDERX
    /*
     * When shadow-frame mode is active, `tstate->frame` may have changed
     * between when `f` was allocated and now. Reset `f->f_back` to point to
     * the top-most frame if so.
     */
    if (f->f_back != tstate->frame) {
      Py_XINCREF(tstate->frame);
      Py_XSETREF(f->f_back, tstate->frame);
    }
#endif

    /* push frame */
    tstate->frame = f;
    co = f->f_code;
    co->co_mutable->curcalls++;

    // Generator shadow frames are managed by the send implementation.
    if (f->f_gen == NULL) {
        _PyShadowFrame_PushInterp(tstate, &shadow_frame, f);
    }

    if (trace_info.cframe.use_tracing) {
        if (tstate->c_tracefunc != NULL) {
            /* tstate->c_tracefunc, if defined, is a
               function that will be called on *every* entry
               to a code block.  Its return value, if not
               None, is a function that will be called at
               the start of each executed line of code.
               (Actually, the function must return itself
               in order to continue tracing.)  The trace
               functions are called with three arguments:
               a pointer to the current frame, a string
               indicating why the function is called, and
               an argument which depends on the situation.
               The global trace function is also called
               whenever an exception is detected. */
            if (call_trace_protected(tstate->c_tracefunc,
                                     tstate->c_traceobj,
                                     tstate, f, &trace_info,
                                     PyTrace_CALL, Py_None)) {
                /* Trace function raised an error */
                goto exit_eval_frame;
            }
        }
        if (tstate->c_profilefunc != NULL) {
            /* Similar for c_profilefunc, except it needn't
               return itself and isn't called for "line" events */
            if (call_trace_protected(tstate->c_profilefunc,
                                     tstate->c_profileobj,
                                     tstate, f, &trace_info,
                                     PyTrace_CALL, Py_None)) {
                /* Profile function raised an error */
                goto exit_eval_frame;
            }
        }
    }

    if (PyDTrace_FUNCTION_ENTRY_ENABLED())
        dtrace_function_entry(f);

    /* facebook begin t39538061 */
    /* Initialize the inline cache after the code object is "hot enough" */
    if (!tstate->profile_interp && co->co_mutable->shadow == NULL &&
        _PyEval_ShadowByteCodeEnabled) {
        if (++(co->co_mutable->ncalls) > PYSHADOW_INIT_THRESHOLD) {
            if (_PyShadow_InitCache(co) == -1) {
                goto error;
            }
            INLINE_CACHE_CREATED(co->co_mutable);
        }
    }
    /* facebook end t39538061 */

    names = co->co_names;
    consts = co->co_consts;
    fastlocals = f->f_localsplus;
    freevars = f->f_localsplus + co->co_nlocals;
    assert(PyBytes_Check(co->co_code));
    assert(PyBytes_GET_SIZE(co->co_code) <= INT_MAX);
    assert(PyBytes_GET_SIZE(co->co_code) % sizeof(_Py_CODEUNIT) == 0);
    assert(_Py_IS_ALIGNED(PyBytes_AS_STRING(co->co_code), sizeof(_Py_CODEUNIT)));

    /* facebook begin t39538061 */
    shadow.code = co;
    shadow.first_instr = &first_instr;
    assert(PyDict_CheckExact(f->f_builtins));
    PyObject ***global_cache = NULL;
    if (co->co_mutable->shadow != NULL && PyDict_CheckExact(f->f_globals)) {
        shadow.shadow = co->co_mutable->shadow;
        global_cache = shadow.shadow->globals;
        first_instr = &shadow.shadow->code[0];
    } else {
        first_instr = (_Py_CODEUNIT *)PyBytes_AS_STRING(co->co_code);
    }
    /* facebook end t39538061 */

    /*
       f->f_lasti refers to the index of the last instruction,
       unless it's -1 in which case next_instr should be first_instr.

       YIELD_FROM sets f_lasti to itself, in order to repeatedly yield
       multiple values.

       When the PREDICT() macros are enabled, some opcode pairs follow in
       direct succession without updating f->f_lasti.  A successful
       prediction effectively links the two codes together as if they
       were a single new opcode; accordingly,f->f_lasti will point to
       the first code in the pair (for instance, GET_ITER followed by
       FOR_ITER is effectively a single opcode and f->f_lasti will point
       to the beginning of the combined pair.)
    */
    assert(f->f_lasti >= -1);
    next_instr = first_instr + f->f_lasti + 1;
    stack_pointer = f->f_valuestack + f->f_stackdepth;
    /* Set f->f_stackdepth to -1.
     * Update when returning or calling trace function.
       Having f_stackdepth <= 0 ensures that invalid
       values are not visible to the cycle GC.
       We choose -1 rather than 0 to assist debugging.
     */
    f->f_stackdepth = -1;
    f->f_state = FRAME_EXECUTING;

#ifdef LLTRACE
    {
        int r = _PyDict_ContainsId(f->f_globals, &PyId___ltrace__);
        if (r < 0) {
            goto exit_eval_frame;
        }
        lltrace = r;
    }
#endif

    if (throwflag) { /* support for generator.throw() */
        goto error;
    }

#ifdef Py_DEBUG
    /* _PyEval_EvalFrameDefault() must not be called with an exception set,
       because it can clear it (directly or indirectly) and so the
       caller loses its exception */
    assert(!_PyErr_Occurred(tstate));
#endif

f->lazy_imports = -1;
f->lazy_imports_cache = 0;
f->lazy_imports_cache_seq = -1;

main_loop:
    for (;;) {
        assert(stack_pointer >= f->f_valuestack); /* else underflow */
        assert(STACK_LEVEL() <= co->co_stacksize);  /* else overflow */
        assert(!_PyErr_Occurred(tstate));

        /* Do periodic things.  Doing this every time through
           the loop would add too much overhead, so we do it
           only every Nth instruction.  We also do it if
           ``pending.calls_to_do'' is set, i.e. when an asynchronous
           event needs attention (e.g. a signal handler or
           async I/O handler); see Py_AddPendingCall() and
           Py_MakePendingCalls() above. */

        if (_Py_atomic_load_relaxed(eval_breaker)) {
            opcode = _Py_OPCODE(*next_instr);
            if (opcode != SETUP_FINALLY &&
                opcode != SETUP_WITH &&
                opcode != BEFORE_ASYNC_WITH &&
                opcode != YIELD_FROM) {
                /* Few cases where we skip running signal handlers and other
                   pending calls:
                   - If we're about to enter the 'with:'. It will prevent
                     emitting a resource warning in the common idiom
                     'with open(path) as file:'.
                   - If we're about to enter the 'async with:'.
                   - If we're about to enter the 'try:' of a try/finally (not
                     *very* useful, but might help in some cases and it's
                     traditional)
                   - If we're resuming a chain of nested 'yield from' or
                     'await' calls, then each frame is parked with YIELD_FROM
                     as its next opcode. If the user hit control-C we want to
                     wait until we've reached the innermost frame before
                     running the signal handler and raising KeyboardInterrupt
                     (see bpo-30039).
                */
                if (eval_frame_handle_pending(tstate) != 0) {
                    goto error;
                }
             }
        }

    tracing_dispatch:
    {
        int instr_prev = f->f_lasti;
        f->f_lasti = INSTR_OFFSET();
        NEXTOPARG();

#ifdef ENABLE_CINDERX
        struct _ceval_state *ceval = &tstate->interp->ceval;
        if (tstate->profile_interp &&
            ++ceval->profile_instr_counter == ceval->profile_instr_period) {
            ceval->profile_instr_counter = 0;
            profiled_instrs++;
            try_profile_next_instr(f, stack_pointer, next_instr - 1);
        }
#endif

        if (PyDTrace_LINE_ENABLED())
            maybe_dtrace_line(f, &trace_info, instr_prev);

        /* line-by-line tracing support */

        if (trace_info.cframe.use_tracing &&
            tstate->c_tracefunc != NULL && !tstate->tracing) {
            int err;
            /* see maybe_call_line_trace()
               for expository comments */
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);

            err = maybe_call_line_trace(tstate->c_tracefunc,
                                        tstate->c_traceobj,
                                        tstate, f,
                                        &trace_info, instr_prev);
            /* Reload possibly changed frame fields */
            JUMPTO(f->f_lasti);
            stack_pointer = f->f_valuestack+f->f_stackdepth;
            f->f_stackdepth = -1;
            if (err) {
                /* trace function raised an exception */
                goto error;
            }
            NEXTOPARG();
        }
    }

#ifdef LLTRACE
        /* Instruction tracing */

        if (lltrace) {
            if (HAS_ARG(opcode)) {
                printf("%d: %d, %d\n",
                       f->f_lasti, opcode, oparg);
            }
            else {
                printf("%d: %d\n",
                       f->f_lasti, opcode);
            }
        }
#endif
#if USE_COMPUTED_GOTOS == 0
    goto dispatch_opcode;

    predispatch:
        if (trace_info.cframe.use_tracing OR_DTRACE_LINE OR_LLTRACE) {
            goto tracing_dispatch;
        }
        f->f_lasti = INSTR_OFFSET();
        NEXTOPARG();
#endif
    dispatch_opcode:
#ifdef DYNAMIC_EXECUTION_PROFILE
#ifdef DXPAIRS
        dxpairs[lastopcode][opcode]++;
        lastopcode = opcode;
#endif
        dxp[opcode]++;
#endif

        switch (opcode) {

        /* BEWARE!
           It is essential that any operation that fails must goto error
           and that all operation that succeed call DISPATCH() ! */

        case TARGET(NOP): {
            DISPATCH();
        }

        case TARGET(LOAD_FAST): {
            PyObject *value = GETLOCAL(oparg);
            if (value == NULL) {
                format_exc_check_arg(tstate, PyExc_UnboundLocalError,
                                     UNBOUNDLOCAL_ERROR_MSG,
                                     PyTuple_GetItem(co->co_varnames, oparg));
                goto error;
            }
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(LOAD_CONST): {
            PREDICTED(LOAD_CONST);
            PyObject *value = GETITEM(consts, oparg);
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(STORE_FAST): {
            PREDICTED(STORE_FAST);
            PyObject *value = POP();
            SETLOCAL(oparg, value);
            DISPATCH();
        }

        case TARGET(POP_TOP): {
            PyObject *value = POP();
            Py_DECREF(value);
            DISPATCH();
        }

        case TARGET(ROT_TWO): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            SET_TOP(second);
            SET_SECOND(top);
            DISPATCH();
        }

        case TARGET(ROT_THREE): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            PyObject *third = THIRD();
            SET_TOP(second);
            SET_SECOND(third);
            SET_THIRD(top);
            DISPATCH();
        }

        case TARGET(ROT_FOUR): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            PyObject *third = THIRD();
            PyObject *fourth = FOURTH();
            SET_TOP(second);
            SET_SECOND(third);
            SET_THIRD(fourth);
            SET_FOURTH(top);
            DISPATCH();
        }

        case TARGET(DUP_TOP): {
            PyObject *top = TOP();
            Py_INCREF(top);
            PUSH(top);
            DISPATCH();
        }

        case TARGET(DUP_TOP_TWO): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            Py_INCREF(top);
            Py_INCREF(second);
            STACK_GROW(2);
            SET_TOP(top);
            SET_SECOND(second);
            DISPATCH();
        }

        case TARGET(UNARY_POSITIVE): {
            PyObject *value = TOP();
            PyObject *res = PyNumber_Positive(value);
            Py_DECREF(value);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(UNARY_NEGATIVE): {
            PyObject *value = TOP();
            PyObject *res = PyNumber_Negative(value);
            Py_DECREF(value);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(UNARY_NOT): {
            PyObject *value = TOP();
            int err = PyObject_IsTrue(value);
            Py_DECREF(value);
            if (err == 0) {
                Py_INCREF(Py_True);
                SET_TOP(Py_True);
                DISPATCH();
            }
            else if (err > 0) {
                Py_INCREF(Py_False);
                SET_TOP(Py_False);
                DISPATCH();
            }
            STACK_SHRINK(1);
            goto error;
        }

        case TARGET(UNARY_INVERT): {
            PyObject *value = TOP();
            PyObject *res = PyNumber_Invert(value);
            Py_DECREF(value);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_POWER): {
            PyObject *exp = POP();
            PyObject *base = TOP();
            PyObject *res = PyNumber_Power(base, exp, Py_None);
            Py_DECREF(base);
            Py_DECREF(exp);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Multiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_MATRIX_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_MatrixMultiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_TRUE_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_TrueDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_FLOOR_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_FloorDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_MODULO): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *res;
            if (PyUnicode_CheckExact(dividend) && (
                  !PyUnicode_Check(divisor) || PyUnicode_CheckExact(divisor))) {
              // fast path; string formatting, but not if the RHS is a str subclass
              // (see issue28598)
              res = PyUnicode_Format(dividend, divisor);
            } else {
              res = PyNumber_Remainder(dividend, divisor);
            }
            Py_DECREF(divisor);
            Py_DECREF(dividend);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_ADD): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *sum;
            /* NOTE(vstinner): Please don't try to micro-optimize int+int on
               CPython using bytecode, it is simply worthless.
               See http://bugs.python.org/issue21955 and
               http://bugs.python.org/issue10044 for the discussion. In short,
               no patch shown any impact on a realistic benchmark, only a minor
               speedup on microbenchmarks. */
            if (PyUnicode_CheckExact(left) &&
                     PyUnicode_CheckExact(right)) {
                sum = unicode_concatenate(tstate, left, right, f, next_instr);
                /* unicode_concatenate consumed the ref to left */
            }
            else {
                sum = PyNumber_Add(left, right);
                Py_DECREF(left);
            }
            Py_DECREF(right);
            SET_TOP(sum);
            if (sum == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBTRACT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *diff = PyNumber_Subtract(left, right);
            Py_DECREF(right);
            Py_DECREF(left);
            SET_TOP(diff);
            if (diff == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR): {
            PyObject *res;
            PyObject *sub = POP();
            PyObject *container = TOP();
#ifdef INLINE_CACHE_PROFILE
            char type_names[81];
            snprintf(type_names,
                     sizeof(type_names),
                     "%s[%s]",
                     Py_TYPE(container)->tp_name,
                     Py_TYPE(sub)->tp_name);
            INLINE_CACHE_INCR("binary_subscr_types", type_names);

#endif
            res = shadow.shadow == NULL
                      ? PyObject_GetItem(container, sub)
                      : _PyShadow_BinarySubscrWithCache(
                            &shadow, next_instr, container, sub, oparg);
            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_LSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Lshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_RSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Rshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_AND): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_And(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_XOR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Xor(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_OR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Or(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(LIST_APPEND): {
            PyObject *v = POP();
            PyObject *list = PEEK(oparg);
            int err;
            err = Ci_List_APPEND((PyListObject *) list, v);
            Py_DECREF(v);
            if (err != 0)
                goto error;
            PREDICT(JUMP_ABSOLUTE);
            DISPATCH();
        }

        case TARGET(SET_ADD): {
            PyObject *v = POP();
            PyObject *set = PEEK(oparg);
            int err;
            err = PySet_Add(set, v);
            Py_DECREF(v);
            if (err != 0)
                goto error;
            PREDICT(JUMP_ABSOLUTE);
            DISPATCH();
        }

        case TARGET(INPLACE_POWER): {
            PyObject *exp = POP();
            PyObject *base = TOP();
            PyObject *res = PyNumber_InPlacePower(base, exp, Py_None);
            Py_DECREF(base);
            Py_DECREF(exp);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceMultiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_MATRIX_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceMatrixMultiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_TRUE_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_InPlaceTrueDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_FLOOR_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_InPlaceFloorDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_MODULO): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *mod = PyNumber_InPlaceRemainder(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(mod);
            if (mod == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_ADD): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *sum;
            if (PyUnicode_CheckExact(left) && PyUnicode_CheckExact(right)) {
                sum = unicode_concatenate(tstate, left, right, f, next_instr);
                /* unicode_concatenate consumed the ref to left */
            }
            else {
                sum = PyNumber_InPlaceAdd(left, right);
                Py_DECREF(left);
            }
            Py_DECREF(right);
            SET_TOP(sum);
            if (sum == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_SUBTRACT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *diff = PyNumber_InPlaceSubtract(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(diff);
            if (diff == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_LSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceLshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_RSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceRshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_AND): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceAnd(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_XOR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceXor(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_OR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceOr(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(STORE_SUBSCR): {
            PyObject *sub = TOP();
            PyObject *container = SECOND();
            PyObject *v = THIRD();
            int err;
            STACK_SHRINK(3);
            /* container[sub] = v */
            err = PyObject_SetItem(container, sub, v);
            Py_DECREF(v);
            Py_DECREF(container);
            Py_DECREF(sub);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_SUBSCR): {
            PyObject *sub = TOP();
            PyObject *container = SECOND();
            int err;
            STACK_SHRINK(2);
            /* del container[sub] */
            err = PyObject_DelItem(container, sub);
            Py_DECREF(container);
            Py_DECREF(sub);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(PRINT_EXPR): {
            _Py_IDENTIFIER(displayhook);
            PyObject *value = POP();
            PyObject *hook = _PySys_GetObjectId(&PyId_displayhook);
            PyObject *res;
            if (hook == NULL) {
                _PyErr_SetString(tstate, PyExc_RuntimeError,
                                 "lost sys.displayhook");
                Py_DECREF(value);
                goto error;
            }
            res = PyObject_CallOneArg(hook, value);
            Py_DECREF(value);
            if (res == NULL)
                goto error;
            Py_DECREF(res);
            DISPATCH();
        }

        case TARGET(RAISE_VARARGS): {
            PyObject *cause = NULL, *exc = NULL;
            switch (oparg) {
            case 2:
                cause = POP(); /* cause */
                /* fall through */
            case 1:
                exc = POP(); /* exc */
                /* fall through */
            case 0:
                if (do_raise(tstate, exc, cause)) {
                    goto exception_unwind;
                }
                break;
            default:
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "bad RAISE_VARARGS oparg");
                break;
            }
            goto error;
        }

        case TARGET(RETURN_VALUE): {
            retval = POP();
            assert(f->f_iblock == 0);
            assert(EMPTY());
            f->f_state = FRAME_RETURNED;
            f->f_stackdepth = 0;
            goto exiting;
        }

        case TARGET(GET_AITER): {
            PyObject *obj = TOP();
            PyObject *iter = Ci_GetAIter(tstate, obj);
            Py_DECREF(obj);
            SET_TOP(iter);
            if (iter == NULL) {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(GET_ANEXT): {
            PyObject *awaitable = Ci_GetANext(tstate, TOP());
            if (awaitable == NULL) {
                goto error;
            }
            PUSH(awaitable);
            PREDICT(LOAD_CONST);
            DISPATCH();
        }

        case TARGET(GET_AWAITABLE): {
            PREDICTED(GET_AWAITABLE);
            PyObject *iterable = TOP();
            PyObject *iter = _PyCoro_GetAwaitableIter(iterable);

            if (iter == NULL) {
                int opcode_at_minus_3 = 0;
                if ((next_instr - first_instr) > 2) {
                    opcode_at_minus_3 = _Py_OPCODE(next_instr[-3]);
                }
                format_awaitable_error(tstate, Py_TYPE(iterable),
                                       opcode_at_minus_3,
                                       _Py_OPCODE(next_instr[-2]));
            }

            Py_DECREF(iterable);

            if (iter != NULL && PyCoro_CheckExact(iter)) {
                PyObject *yf = _PyGen_yf((PyGenObject*)iter);
                if (yf != NULL) {
                    /* `iter` is a coroutine object that is being
                       awaited, `yf` is a pointer to the current awaitable
                       being awaited on. */
                    Py_DECREF(yf);
                    Py_CLEAR(iter);
                    _PyErr_SetString(tstate, PyExc_RuntimeError,
                                     "coroutine is being awaited already");
                    /* The code below jumps to `error` if `iter` is NULL. */
                }
            }

            SET_TOP(iter); /* Even if it's NULL */

            if (iter == NULL) {
                goto error;
            }

            PREDICT(LOAD_CONST);
            DISPATCH();
        }

        case TARGET(YIELD_FROM): {
            PyObject *v = POP();
            PyObject *receiver = TOP();
            PySendResult gen_status;
            if (f->f_gen && (co->co_flags & CO_COROUTINE)) {
                _PyAwaitable_SetAwaiter(receiver, f->f_gen);
            }
            if (tstate->c_tracefunc == NULL) {
                gen_status = PyIter_Send(receiver, v, &retval);
            } else {
                _Py_IDENTIFIER(send);
                if (Py_IsNone(v) && PyIter_Check(receiver)) {
                    retval = Py_TYPE(receiver)->tp_iternext(receiver);
                }
                else {
                    retval = _PyObject_CallMethodIdOneArg(receiver, &PyId_send, v);
                }
                if (retval == NULL) {
                    if (tstate->c_tracefunc != NULL
                            && _PyErr_ExceptionMatches(tstate, PyExc_StopIteration))
                        call_exc_trace(tstate->c_tracefunc, tstate->c_traceobj, tstate, f, &trace_info);
                    if (_PyGen_FetchStopIterationValue(&retval) == 0) {
                        gen_status = PYGEN_RETURN;
                    }
                    else {
                        gen_status = PYGEN_ERROR;
                    }
                }
                else {
                    gen_status = PYGEN_NEXT;
                }
            }
            Py_DECREF(v);
            if (gen_status == PYGEN_ERROR) {
                assert (retval == NULL);
                goto error;
            }
            if (gen_status == PYGEN_RETURN) {
                assert (retval != NULL);

                Py_DECREF(receiver);
                SET_TOP(retval);
                retval = NULL;
                DISPATCH();
            }
            assert (gen_status == PYGEN_NEXT);
            /* receiver remains on stack, retval is value to be yielded */
            /* and repeat... */
            assert(f->f_lasti > 0);
            f->f_lasti -= 1;
            f->f_state = FRAME_SUSPENDED;
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);
            goto exiting;
        }

        case TARGET(YIELD_VALUE): {
            retval = POP();

            if (co->co_flags & CO_ASYNC_GENERATOR) {
                PyObject *w = _PyAsyncGenValueWrapperNew(retval);
                Py_DECREF(retval);
                if (w == NULL) {
                    retval = NULL;
                    goto error;
                }
                retval = w;
            }
            f->f_state = FRAME_SUSPENDED;
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);
            goto exiting;
        }

        case TARGET(GEN_START): {
            PyObject *none = POP();
            assert(none == Py_None);
            assert(oparg < 3);
            Py_DECREF(none);
            DISPATCH();
        }

        case TARGET(POP_EXCEPT): {
            PyObject *type, *value, *traceback;
            _PyErr_StackItem *exc_info;
            PyTryBlock *b = PyFrame_BlockPop(f);
            if (b->b_type != EXCEPT_HANDLER) {
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "popped block is not an except handler");
                goto error;
            }
            assert(STACK_LEVEL() >= (b)->b_level + 3 &&
                   STACK_LEVEL() <= (b)->b_level + 4);
            exc_info = tstate->exc_info;
            type = exc_info->exc_type;
            value = exc_info->exc_value;
            traceback = exc_info->exc_traceback;
            exc_info->exc_type = POP();
            exc_info->exc_value = POP();
            exc_info->exc_traceback = POP();
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
            DISPATCH();
        }

        case TARGET(POP_BLOCK): {
            PyFrame_BlockPop(f);
            DISPATCH();
        }

        case TARGET(RERAISE): {
            assert(f->f_iblock > 0);
            if (oparg) {
                f->f_lasti = f->f_blockstack[f->f_iblock-1].b_handler;
            }
            PyObject *exc = POP();
            PyObject *val = POP();
            PyObject *tb = POP();
            assert(PyExceptionClass_Check(exc));
            _PyErr_Restore(tstate, exc, val, tb);
            goto exception_unwind;
        }

        case TARGET(END_ASYNC_FOR): {
            PyObject *exc = POP();
            assert(PyExceptionClass_Check(exc));
            if (PyErr_GivenExceptionMatches(exc, PyExc_StopAsyncIteration)) {
                PyTryBlock *b = PyFrame_BlockPop(f);
                assert(b->b_type == EXCEPT_HANDLER);
                Py_DECREF(exc);
                UNWIND_EXCEPT_HANDLER(b);
                Py_DECREF(POP());
                JUMPBY(oparg);
                DISPATCH();
            }
            else {
                PyObject *val = POP();
                PyObject *tb = POP();
                _PyErr_Restore(tstate, exc, val, tb);
                goto exception_unwind;
            }
        }

        case TARGET(LOAD_ASSERTION_ERROR): {
            PyObject *value = PyExc_AssertionError;
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(LOAD_BUILD_CLASS): {
            _Py_IDENTIFIER(__build_class__);

            PyObject *bc;
            if (PyDict_CheckExact(f->f_builtins)) {
                bc = _PyDict_GetItemIdWithError(f->f_builtins, &PyId___build_class__);
                if (bc == NULL) {
                    if (!_PyErr_Occurred(tstate)) {
                        _PyErr_SetString(tstate, PyExc_NameError,
                                         "__build_class__ not found");
                    }
                    goto error;
                }
                Py_INCREF(bc);
            }
            else {
                PyObject *build_class_str = _PyUnicode_FromId(&PyId___build_class__);
                if (build_class_str == NULL)
                    goto error;
                bc = PyObject_GetItem(f->f_builtins, build_class_str);
                if (bc == NULL) {
                    if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError))
                        _PyErr_SetString(tstate, PyExc_NameError,
                                         "__build_class__ not found");
                    goto error;
                }
            }
            PUSH(bc);
            DISPATCH();
        }

        case TARGET(STORE_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *v = POP();
            PyObject *ns = f->f_locals;
            int err;
            if (ns == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals found when storing %R", name);
                Py_DECREF(v);
                goto error;
            }
            if (PyDict_CheckExact(ns)) {
                err = PyDict_SetItem(ns, name, v);
            } else {
                err = PyObject_SetItem(ns, name, v);
            }
            Py_DECREF(v);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *ns = f->f_locals;
            int err;
            if (ns == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals when deleting %R", name);
                goto error;
            }
            err = PyObject_DelItem(ns, name);
            if (err != 0) {
                format_exc_check_arg(tstate, PyExc_NameError,
                                     NAME_ERROR_MSG,
                                     name);
                goto error;
            }
            DISPATCH();
        }

        case TARGET(UNPACK_SEQUENCE): {
            PREDICTED(UNPACK_SEQUENCE);
            PyObject *seq = POP(), *item, **items;
            if (PyTuple_CheckExact(seq) &&
                PyTuple_GET_SIZE(seq) == oparg) {
                items = ((PyTupleObject *)seq)->ob_item;
                while (oparg--) {
                    item = items[oparg];
                    Py_INCREF(item);
                    PUSH(item);
                }
            } else if (PyList_CheckExact(seq) &&
                       PyList_GET_SIZE(seq) == oparg) {
                items = ((PyListObject *)seq)->ob_item;
                while (oparg--) {
                    item = items[oparg];
                    Py_INCREF(item);
                    PUSH(item);
                }
            } else if (unpack_iterable(tstate, seq, oparg, -1,
                                       stack_pointer + oparg)) {
                STACK_GROW(oparg);
            } else {
                /* unpack_iterable() raised an exception */
                Py_DECREF(seq);
                goto error;
            }
            Py_DECREF(seq);
            DISPATCH();
        }

        case TARGET(UNPACK_EX): {
            int totalargs = 1 + (oparg & 0xFF) + (oparg >> 8);
            PyObject *seq = POP();

            if (unpack_iterable(tstate, seq, oparg & 0xFF, oparg >> 8,
                                stack_pointer + totalargs)) {
                stack_pointer += totalargs;
            } else {
                Py_DECREF(seq);
                goto error;
            }
            Py_DECREF(seq);
            DISPATCH();
        }

        case TARGET(STORE_ATTR): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            PyObject *v = SECOND();
#ifdef INLINE_CACHE_PROFILE
            _PyShadow_LogLocation(&shadow, next_instr, "STORE_ATTR");
            char type_name[81];
            snprintf(type_name,
                     sizeof(type_name),
                     "STORE_ATTR_TYPE[%s]",
                     Py_TYPE(owner)->tp_name);
            _PyShadow_LogLocation(&shadow, next_instr, type_name);
#endif
            int err;
            STACK_SHRINK(2);
            err = shadow.shadow == NULL
                      ? PyObject_SetAttr(owner, name, v)
                      : _PyShadow_StoreAttrWithCache(
                            &shadow, next_instr, owner, name, v);
            Py_DECREF(v);
            Py_DECREF(owner);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_ATTR): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = POP();
            int err;
            err = PyObject_SetAttr(owner, name, (PyObject *)NULL);
            Py_DECREF(owner);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(STORE_GLOBAL): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *v = POP();
            int err;
            err = PyDict_SetItem(f->f_globals, name, v);
            Py_DECREF(v);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_GLOBAL): {
            PyObject *name = GETITEM(names, oparg);
            int err;
            err = PyDict_DelItem(f->f_globals, name);
            if (err != 0) {
                if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                    format_exc_check_arg(tstate, PyExc_NameError,
                                         NAME_ERROR_MSG, name);
                }
                goto error;
            }
            DISPATCH();
        }

        case TARGET(LOAD_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *locals = f->f_locals;
            PyObject *v;
            if (locals == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals when loading %R", name);
                goto error;
            }
            if (PyDict_CheckExact(locals)) {
                v = PyDict_GetItemWithError(locals, name);
                if (v != NULL) {
                    Py_INCREF(v);
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto error;
                }
            }
            else {
                v = PyObject_GetItem(locals, name);
                if (v == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError))
                        goto error;
                    _PyErr_Clear(tstate);
                }
            }
            if (v == NULL) {
                v = PyDict_GetItemWithError(f->f_globals, name);
                if (v != NULL) {
                    Py_INCREF(v);
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto error;
                }
                else {
                    if (PyDict_CheckExact(f->f_builtins)) {
                        v = PyDict_GetItemWithError(f->f_builtins, name);
                        if (v == NULL) {
                            if (!_PyErr_Occurred(tstate)) {
                                format_exc_check_arg(
                                        tstate, PyExc_NameError,
                                        NAME_ERROR_MSG, name);
                            }
                            goto error;
                        }
                        Py_INCREF(v);
                    }
                    else {
                        v = PyObject_GetItem(f->f_builtins, name);
                        if (v == NULL) {
                            if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                                format_exc_check_arg(
                                            tstate, PyExc_NameError,
                                            NAME_ERROR_MSG, name);
                            }
                            goto error;
                        }
                    }
                }
            }
            PUSH(v);
            DISPATCH();
        }

        case TARGET(LOAD_GLOBAL): {
            PyObject *name;
            PyObject *v;
            if (PyDict_CheckExact(f->f_globals)) {
                assert(PyDict_CheckExact(f->f_builtins));
                name = GETITEM(names, oparg);
                v = _PyDict_LoadGlobal((PyDictObject *)f->f_globals,
                                       (PyDictObject *)f->f_builtins,
                                       name);
                if (v == NULL) {
                    if (!_PyErr_Occurred(tstate)) {
                        /* _PyDict_LoadGlobal() returns NULL without raising
                         * an exception if the key doesn't exist */
                        format_exc_check_arg(tstate, PyExc_NameError,
                                             NAME_ERROR_MSG, name);
                    }
                    goto error;
                }

                if (shadow.shadow != NULL) {
                    _PyShadow_InitGlobal(&shadow,
                                         next_instr,
                                         f->f_globals,
                                         f->f_builtins,
                                         name);
                }

                Py_INCREF(v);
                /* facebook end */
            }
            else {
                /* Slow-path if globals or builtins is not a dict */

                /* namespace 1: globals */
                name = GETITEM(names, oparg);
                v = PyObject_GetItem(f->f_globals, name);
                if (v == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                        goto error;
                    }
                    _PyErr_Clear(tstate);

                    /* namespace 2: builtins */
                    v = PyObject_GetItem(f->f_builtins, name);
                    if (v == NULL) {
                        if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                            format_exc_check_arg(
                                        tstate, PyExc_NameError,
                                        NAME_ERROR_MSG, name);
                        }
                        goto error;
                    }
                }
            }
            PUSH(v);
            DISPATCH();
        }

        case TARGET(DELETE_FAST): {
            PyObject *v = GETLOCAL(oparg);
            if (v != NULL) {
                SETLOCAL(oparg, NULL);
            }
            DISPATCH();
        }

        case TARGET(DELETE_DEREF): {
            PyObject *cell = freevars[oparg];
            PyObject *oldobj = PyCell_GET(cell);
            if (oldobj != NULL) {
                PyCell_SET(cell, NULL);
                Py_DECREF(oldobj);
                DISPATCH();
            }
            format_exc_unbound(tstate, co, oparg);
            goto error;
        }

        case TARGET(LOAD_CLOSURE): {
            PyObject *cell = freevars[oparg];
            Py_INCREF(cell);
            PUSH(cell);
            DISPATCH();
        }

        case TARGET(LOAD_CLASSDEREF): {
            PyObject *name, *value, *locals = f->f_locals;
            Py_ssize_t idx;
            assert(locals);
            assert(oparg >= PyTuple_GET_SIZE(co->co_cellvars));
            idx = oparg - PyTuple_GET_SIZE(co->co_cellvars);
            assert(idx >= 0 && idx < PyTuple_GET_SIZE(co->co_freevars));
            name = PyTuple_GET_ITEM(co->co_freevars, idx);
            if (PyDict_CheckExact(locals)) {
                value = PyDict_GetItemWithError(locals, name);
                if (value != NULL) {
                    Py_INCREF(value);
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto error;
                }
            }
            else {
                value = PyObject_GetItem(locals, name);
                if (value == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                        goto error;
                    }
                    _PyErr_Clear(tstate);
                }
            }
            if (!value) {
                PyObject *cell = freevars[oparg];
                value = PyCell_GET(cell);
                if (value == NULL) {
                    format_exc_unbound(tstate, co, oparg);
                    goto error;
                }
                Py_INCREF(value);
            }
            PUSH(value);
            DISPATCH();
        }

        case TARGET(LOAD_DEREF): {
            PyObject *cell = freevars[oparg];
            PyObject *value = PyCell_GET(cell);
            if (value == NULL) {
                format_exc_unbound(tstate, co, oparg);
                goto error;
            }
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(STORE_DEREF): {
            PyObject *v = POP();
            PyObject *cell = freevars[oparg];
            PyObject *oldobj = PyCell_GET(cell);
            PyCell_SET(cell, v);
            Py_XDECREF(oldobj);
            DISPATCH();
        }

        case TARGET(BUILD_STRING): {
            PyObject *str;
            PyObject *empty = PyUnicode_New(0, 0);
            if (empty == NULL) {
                goto error;
            }
            str = _PyUnicode_JoinArray(empty, stack_pointer - oparg, oparg);
            Py_DECREF(empty);
            if (str == NULL)
                goto error;
            while (--oparg >= 0) {
                PyObject *item = POP();
                Py_DECREF(item);
            }
            PUSH(str);
            DISPATCH();
        }

        case TARGET(BUILD_TUPLE): {
            PyObject *tup = PyTuple_New(oparg);
            if (tup == NULL)
                goto error;
            while (--oparg >= 0) {
                PyObject *item = POP();
                PyTuple_SET_ITEM(tup, oparg, item);
            }
            PUSH(tup);
            DISPATCH();
        }

        case TARGET(BUILD_LIST): {
            PyObject *list =  PyList_New(oparg);
            if (list == NULL)
                goto error;
            while (--oparg >= 0) {
                PyObject *item = POP();
                PyList_SET_ITEM(list, oparg, item);
            }
            PUSH(list);
            DISPATCH();
        }

        case TARGET(LIST_TO_TUPLE): {
            PyObject *list = POP();
            PyObject *tuple = PyList_AsTuple(list);
            Py_DECREF(list);
            if (tuple == NULL) {
                goto error;
            }
            PUSH(tuple);
            DISPATCH();
        }

        case TARGET(LIST_EXTEND): {
            PyObject *iterable = POP();
            PyObject *list = PEEK(oparg);
            PyObject *none_val = _PyList_Extend((PyListObject *)list, iterable);
            if (none_val == NULL) {
                if (_PyErr_ExceptionMatches(tstate, PyExc_TypeError) &&
                   (Py_TYPE(iterable)->tp_iter == NULL && !PySequence_Check(iterable)))
                {
                    _PyErr_Clear(tstate);
                    _PyErr_Format(tstate, PyExc_TypeError,
                          "Value after * must be an iterable, not %.200s",
                          Py_TYPE(iterable)->tp_name);
                }
                Py_DECREF(iterable);
                goto error;
            }
            Py_DECREF(none_val);
            Py_DECREF(iterable);
            DISPATCH();
        }

        case TARGET(SET_UPDATE): {
            PyObject *iterable = POP();
            PyObject *set = PEEK(oparg);
            int err = _PySet_Update(set, iterable);
            Py_DECREF(iterable);
            if (err < 0) {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(BUILD_SET): {
            PyObject *set = PySet_New(NULL);
            int err = 0;
            int i;
            if (set == NULL)
                goto error;
            for (i = oparg; i > 0; i--) {
                PyObject *item = PEEK(i);
                if (err == 0)
                    err = PySet_Add(set, item);
                Py_DECREF(item);
            }
            STACK_SHRINK(oparg);
            if (err != 0) {
                Py_DECREF(set);
                goto error;
            }
            PUSH(set);
            DISPATCH();
        }

#define Ci_BUILD_DICT(map_size)                                               \
                                                                              \
    for (Py_ssize_t i = map_size; i > 0; i--) {                               \
        int err;                                                              \
        PyObject *key = PEEK(2 * i);                                          \
        PyObject *value = PEEK(2 * i - 1);                                    \
        err = Ci_Dict_SetItemInternal(map, key, value);                       \
        if (err != 0) {                                                       \
            Py_DECREF(map);                                                   \
            goto error;                                                       \
        }                                                                     \
    }                                                                         \
                                                                              \
    while (map_size--) {                                                      \
        Py_DECREF(POP());                                                     \
        Py_DECREF(POP());                                                     \
    }                                                                         \
    PUSH(map);

        case TARGET(BUILD_MAP): {
            PyObject *map = _PyDict_NewPresized((Py_ssize_t)oparg);
            if (map == NULL)
                goto error;

            Ci_BUILD_DICT(oparg);

            DISPATCH();
        }

        case TARGET(SETUP_ANNOTATIONS): {
            _Py_IDENTIFIER(__annotations__);
            int err;
            PyObject *ann_dict;
            if (f->f_locals == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals found when setting up annotations");
                goto error;
            }
            /* check if __annotations__ in locals()... */
            if (PyDict_CheckExact(f->f_locals)) {
                ann_dict = _PyDict_GetItemIdWithError(f->f_locals,
                                             &PyId___annotations__);
                if (ann_dict == NULL) {
                    if (_PyErr_Occurred(tstate)) {
                        goto error;
                    }
                    /* ...if not, create a new one */
                    ann_dict = PyDict_New();
                    if (ann_dict == NULL) {
                        goto error;
                    }
                    err = _PyDict_SetItemId(f->f_locals,
                                            &PyId___annotations__, ann_dict);
                    Py_DECREF(ann_dict);
                    if (err != 0) {
                        goto error;
                    }
                }
            }
            else {
                /* do the same if locals() is not a dict */
                PyObject *ann_str = _PyUnicode_FromId(&PyId___annotations__);
                if (ann_str == NULL) {
                    goto error;
                }
                ann_dict = PyObject_GetItem(f->f_locals, ann_str);
                if (ann_dict == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                        goto error;
                    }
                    _PyErr_Clear(tstate);
                    ann_dict = PyDict_New();
                    if (ann_dict == NULL) {
                        goto error;
                    }
                    err = PyObject_SetItem(f->f_locals, ann_str, ann_dict);
                    Py_DECREF(ann_dict);
                    if (err != 0) {
                        goto error;
                    }
                }
                else {
                    Py_DECREF(ann_dict);
                }
            }
            DISPATCH();
        }

        case TARGET(BUILD_CONST_KEY_MAP): {
            Py_ssize_t i;
            PyObject *map;
            PyObject *keys = TOP();
            if (!PyTuple_CheckExact(keys) ||
                PyTuple_GET_SIZE(keys) != (Py_ssize_t)oparg) {
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "bad BUILD_CONST_KEY_MAP keys argument");
                goto error;
            }
            map = _PyDict_NewPresized((Py_ssize_t)oparg);
            if (map == NULL) {
                goto error;
            }
            for (i = oparg; i > 0; i--) {
                int err;
                PyObject *key = PyTuple_GET_ITEM(keys, oparg - i);
                PyObject *value = PEEK(i + 1);
                err = PyDict_SetItem(map, key, value);
                if (err != 0) {
                    Py_DECREF(map);
                    goto error;
                }
            }

            Py_DECREF(POP());
            while (oparg--) {
                Py_DECREF(POP());
            }
            PUSH(map);
            DISPATCH();
        }

        case TARGET(DICT_UPDATE): {
            PyObject *update = POP();
            PyObject *dict = PEEK(oparg);
            if (PyDict_Update(dict, update) < 0) {
                if (_PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
                    _PyErr_Format(tstate, PyExc_TypeError,
                                    "'%.200s' object is not a mapping",
                                    Py_TYPE(update)->tp_name);
                }
                Py_DECREF(update);
                goto error;
            }
            Py_DECREF(update);
            DISPATCH();
        }

        case TARGET(DICT_MERGE): {
            PyObject *update = POP();
            PyObject *dict = PEEK(oparg);

            if (_PyDict_MergeEx(dict, update, 2) < 0) {
                format_kwargs_error(tstate, PEEK(2 + oparg), update);
                Py_DECREF(update);
                goto error;
            }
            Py_DECREF(update);
            PREDICT(CALL_FUNCTION_EX);
            DISPATCH();
        }

        case TARGET(MAP_ADD): {
            PyObject *value = TOP();
            PyObject *key = SECOND();
            PyObject *map;
            int err;
            STACK_SHRINK(2);
            map = PEEK(oparg);                      /* dict */
            assert(PyDict_CheckExact(map)
#ifdef ENABLE_CINDERX
                 || Ci_CheckedDict_Check(map)
#endif
                 );
            err = Ci_Dict_SetItemInternal(map, key, value);  /* map[key] = value */
            Py_DECREF(value);
            Py_DECREF(key);
            if (err != 0)
                goto error;
            PREDICT(JUMP_ABSOLUTE);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            PyObject *res = shadow.shadow == NULL
                                ? PyObject_GetAttr(owner, name)
                                : _PyShadow_LoadAttrWithCache(
                                      &shadow, next_instr, owner, name);
            Py_DECREF(owner);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(COMPARE_OP): {
            assert(oparg <= Py_GE);
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyObject_RichCompare(left, right, oparg);
            SET_TOP(res);
            Py_DECREF(left);
            Py_DECREF(right);
            if (res == NULL)
                goto error;
            PREDICT(POP_JUMP_IF_FALSE);
            PREDICT(POP_JUMP_IF_TRUE);
            DISPATCH();
        }

        case TARGET(IS_OP): {
            PyObject *right = POP();
            PyObject *left = TOP();
            int res = Py_Is(left, right) ^ oparg;
            PyObject *b = res ? Py_True : Py_False;
            Py_INCREF(b);
            SET_TOP(b);
            Py_DECREF(left);
            Py_DECREF(right);
            PREDICT(POP_JUMP_IF_FALSE);
            PREDICT(POP_JUMP_IF_TRUE);
            DISPATCH();
        }

        case TARGET(CONTAINS_OP): {
            PyObject *right = POP();
            PyObject *left = POP();
            int res = PySequence_Contains(right, left);
            Py_DECREF(left);
            Py_DECREF(right);
            if (res < 0) {
                goto error;
            }
            PyObject *b = (res^oparg) ? Py_True : Py_False;
            Py_INCREF(b);
            PUSH(b);
            PREDICT(POP_JUMP_IF_FALSE);
            PREDICT(POP_JUMP_IF_TRUE);
            DISPATCH();
        }

#define CANNOT_CATCH_MSG "catching classes that do not inherit from "\
                         "BaseException is not allowed"

        case TARGET(JUMP_IF_NOT_EXC_MATCH): {
            PyObject *right = POP();
            PyObject *left = POP();
            if (PyTuple_Check(right)) {
                Py_ssize_t i, length;
                length = PyTuple_GET_SIZE(right);
                for (i = 0; i < length; i++) {
                    PyObject *exc = PyTuple_GET_ITEM(right, i);
                    if (!PyExceptionClass_Check(exc)) {
                        _PyErr_SetString(tstate, PyExc_TypeError,
                                        CANNOT_CATCH_MSG);
                        Py_DECREF(left);
                        Py_DECREF(right);
                        goto error;
                    }
                }
            }
            else {
                if (!PyExceptionClass_Check(right)) {
                    _PyErr_SetString(tstate, PyExc_TypeError,
                                    CANNOT_CATCH_MSG);
                    Py_DECREF(left);
                    Py_DECREF(right);
                    goto error;
                }
            }
            int res = PyErr_GivenExceptionMatches(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            if (res > 0) {
                /* Exception matches -- Do nothing */;
            }
            else if (res == 0) {
                JUMPTO(oparg);
            }
            else {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(IMPORT_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *fromlist = POP();
            PyObject *level = TOP();
            PyObject *res;

            if (_PyImport_IsLazyImportsEnabled(tstate)
                && f->f_globals == f->f_locals
                && f->f_iblock == 0) {
                res = _PyImport_LazyImportName(f->f_builtins,
                                               f->f_globals,
                                               f->f_locals == NULL ? Py_None : f->f_locals,
                                               name,
                                               fromlist,
                                               level);
            }
            else {
                res = _PyImport_ImportName(f->f_builtins,
                                           f->f_globals,
                                           f->f_locals == NULL ? Py_None : f->f_locals,
                                           name,
                                           fromlist,
                                           level);
            }

            Py_DECREF(level);
            Py_DECREF(fromlist);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(IMPORT_STAR): {
            PyObject *from = POP(), *locals;
            int err;
            if (PyLazyImport_CheckExact(from)) {
                PyObject *mod = _PyImport_LoadLazyImportTstate(tstate, from, 1);
                Py_DECREF(from);
                if (mod == NULL) {
                    if (!_PyErr_Occurred(tstate)) {
                        _PyErr_SetString(tstate, PyExc_SystemError,
                                         "Lazy Import cycle");
                    }
                    goto error;
                }
                from = mod;
            }

            if (PyFrame_FastToLocalsWithError(f) < 0) {
                Py_DECREF(from);
                goto error;
            }

            locals = f->f_locals;
            if (locals == NULL) {
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "no locals found during 'import *'");
                Py_DECREF(from);
                goto error;
            }
            err = import_all_from(tstate, locals, from);
            Py_DECREF(from);
            if (err != 0)
                goto error;
            PyFrame_LocalsToFast(f, 0);
            DISPATCH();
        }

        case TARGET(IMPORT_FROM): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *from = TOP();
            PyObject *res;
            if (PyLazyImport_CheckExact(from)) {
                res = _PyImport_LazyImportFrom(tstate, from, name);
            } else {
                res = _PyImport_ImportFrom(tstate, from, name);
            }
            PUSH(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_FORWARD): {
            JUMPBY(oparg);
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_FALSE): {
            PREDICTED(POP_JUMP_IF_FALSE);
            PyObject *cond = POP();
            int err;
            if (Py_IsTrue(cond)) {
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsFalse(cond)) {
                Py_DECREF(cond);
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            Py_DECREF(cond);
            if (err > 0)
                ;
            else if (err == 0) {
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
            }
            else
                goto error;
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_TRUE): {
            PREDICTED(POP_JUMP_IF_TRUE);
            PyObject *cond = POP();
            int err;
            if (Py_IsFalse(cond)) {
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsTrue(cond)) {
                Py_DECREF(cond);
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            Py_DECREF(cond);
            if (err > 0) {
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
            }
            else if (err == 0)
                ;
            else
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_IF_FALSE_OR_POP): {
            PyObject *cond = TOP();
            int err;
            if (Py_IsTrue(cond)) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsFalse(cond)) {
                JUMPTO(oparg);
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            if (err > 0) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            }
            else if (err == 0)
                JUMPTO(oparg);
            else
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_IF_TRUE_OR_POP): {
            PyObject *cond = TOP();
            int err;
            if (Py_IsFalse(cond)) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsTrue(cond)) {
                JUMPTO(oparg);
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            if (err > 0) {
                JUMPTO(oparg);
            }
            else if (err == 0) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            }
            else
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_ABSOLUTE): {
            PREDICTED(JUMP_ABSOLUTE);
            JUMPTO(oparg);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(GET_LEN): {
            // PUSH(len(TOS))
            Py_ssize_t len_i = PyObject_Length(TOP());
            if (len_i < 0) {
                goto error;
            }
            PyObject *len_o = PyLong_FromSsize_t(len_i);
            if (len_o == NULL) {
                goto error;
            }
            PUSH(len_o);
            DISPATCH();
        }

        case TARGET(MATCH_CLASS): {
            // Pop TOS. On success, set TOS to True and TOS1 to a tuple of
            // attributes. On failure, set TOS to False.
            PyObject *names = POP();
            PyObject *type = TOP();
            PyObject *subject = SECOND();
            assert(PyTuple_CheckExact(names));
            PyObject *attrs = Ci_match_class(tstate, subject, type, oparg, names);
            Py_DECREF(names);
            if (attrs) {
                // Success!
                assert(PyTuple_CheckExact(attrs));
                Py_DECREF(subject);
                SET_SECOND(attrs);
            }
            else if (_PyErr_Occurred(tstate)) {
                goto error;
            }
            Py_DECREF(type);
            SET_TOP(PyBool_FromLong(!!attrs));
            DISPATCH();
        }

        case TARGET(MATCH_MAPPING): {
            PyObject *subject = TOP();
            int match = Py_TYPE(subject)->tp_flags & Py_TPFLAGS_MAPPING;
            PyObject *res = match ? Py_True : Py_False;
            Py_INCREF(res);
            PUSH(res);
            DISPATCH();
        }

        case TARGET(MATCH_SEQUENCE): {
            PyObject *subject = TOP();
            int match = Py_TYPE(subject)->tp_flags & Py_TPFLAGS_SEQUENCE;
            PyObject *res = match ? Py_True : Py_False;
            Py_INCREF(res);
            PUSH(res);
            DISPATCH();
        }

        case TARGET(MATCH_KEYS): {
            // On successful match for all keys, PUSH(values) and PUSH(True).
            // Otherwise, PUSH(None) and PUSH(False).
            PyObject *keys = TOP();
            PyObject *subject = SECOND();
            PyObject *values_or_none = Ci_match_keys(tstate, subject, keys);
            if (values_or_none == NULL) {
                goto error;
            }
            PUSH(values_or_none);
            if (Py_IsNone(values_or_none)) {
                Py_INCREF(Py_False);
                PUSH(Py_False);
                DISPATCH();
            }
            assert(PyTuple_CheckExact(values_or_none));
            Py_INCREF(Py_True);
            PUSH(Py_True);
            DISPATCH();
        }

        case TARGET(COPY_DICT_WITHOUT_KEYS): {
            // rest = dict(TOS1)
            // for key in TOS:
            //     del rest[key]
            // SET_TOP(rest)
            PyObject *keys = TOP();
            PyObject *subject = SECOND();
            PyObject *rest = PyDict_New();
            if (rest == NULL || PyDict_Update(rest, subject)) {
                Py_XDECREF(rest);
                goto error;
            }
            // This may seem a bit inefficient, but keys is rarely big enough to
            // actually impact runtime.
            assert(PyTuple_CheckExact(keys));
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(keys); i++) {
                if (PyDict_DelItem(rest, PyTuple_GET_ITEM(keys, i))) {
                    Py_DECREF(rest);
                    goto error;
                }
            }
            Py_DECREF(keys);
            SET_TOP(rest);
            DISPATCH();
        }

        case TARGET(GET_ITER): {
            /* before: [obj]; after [getiter(obj)] */
            PyObject *iterable = TOP();
            PyObject *iter = PyObject_GetIter(iterable);
            Py_DECREF(iterable);
            SET_TOP(iter);
            if (iter == NULL)
                goto error;
            PREDICT(FOR_ITER);
            PREDICT(CALL_FUNCTION);
            DISPATCH();
        }

        case TARGET(GET_YIELD_FROM_ITER): {
            /* before: [obj]; after [getiter(obj)] */
            PyObject *iterable = TOP();
            PyObject *iter;
            if (PyCoro_CheckExact(iterable)) {
                /* `iterable` is a coroutine */
                if (!(co->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE))) {
                    /* and it is used in a 'yield from' expression of a
                       regular generator. */
                    Py_DECREF(iterable);
                    SET_TOP(NULL);
                    _PyErr_SetString(tstate, PyExc_TypeError,
                                     "cannot 'yield from' a coroutine object "
                                     "in a non-coroutine generator");
                    goto error;
                }
            }
            else if (!PyGen_CheckExact(iterable)) {
                /* `iterable` is not a generator. */
                iter = PyObject_GetIter(iterable);
                Py_DECREF(iterable);
                SET_TOP(iter);
                if (iter == NULL)
                    goto error;
            }
            PREDICT(LOAD_CONST);
            DISPATCH();
        }

        case TARGET(FOR_ITER): {
            PREDICTED(FOR_ITER);
            /* before: [iter]; after: [iter, iter()] *or* [] */
            PyObject *iter = TOP();
            PyObject *next = (*Py_TYPE(iter)->tp_iternext)(iter);
            if (next != NULL) {
                PUSH(next);
                PREDICT(STORE_FAST);
                PREDICT(UNPACK_SEQUENCE);
                DISPATCH();
            }
            if (_PyErr_Occurred(tstate)) {
                if (!_PyErr_ExceptionMatches(tstate, PyExc_StopIteration)) {
                    goto error;
                }
                else if (tstate->c_tracefunc != NULL) {
                    call_exc_trace(tstate->c_tracefunc, tstate->c_traceobj, tstate, f, &trace_info);
                }
                _PyErr_Clear(tstate);
            }
            /* iterator ended normally */
            STACK_SHRINK(1);
            Py_DECREF(iter);
            JUMPBY(oparg);
            DISPATCH();
        }

        case TARGET(SETUP_FINALLY): {
            PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + oparg,
                               STACK_LEVEL());
            DISPATCH();
        }

        case TARGET(BEFORE_ASYNC_WITH): {
            _Py_IDENTIFIER(__aenter__);
            _Py_IDENTIFIER(__aexit__);
            PyObject *mgr = TOP();
            PyObject *enter = special_lookup(tstate, mgr, &PyId___aenter__);
            PyObject *res;
            if (enter == NULL) {
                goto error;
            }
            PyObject *exit = special_lookup(tstate, mgr, &PyId___aexit__);
            if (exit == NULL) {
                Py_DECREF(enter);
                goto error;
            }
            SET_TOP(exit);
            Py_DECREF(mgr);
            res = _PyObject_CallNoArg(enter);
            Py_DECREF(enter);
            if (res == NULL)
                goto error;
            PUSH(res);
            PREDICT(GET_AWAITABLE);
            DISPATCH();
        }

        case TARGET(SETUP_ASYNC_WITH): {
            PyObject *res = POP();
            /* Setup the finally block before pushing the result
               of __aenter__ on the stack. */
            PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + oparg,
                               STACK_LEVEL());
            PUSH(res);
            DISPATCH();
        }

        case TARGET(SETUP_WITH): {
            _Py_IDENTIFIER(__enter__);
            _Py_IDENTIFIER(__exit__);
            PyObject *mgr = TOP();
            PyObject *enter = special_lookup(tstate, mgr, &PyId___enter__);
            PyObject *res;
            if (enter == NULL) {
                goto error;
            }
            PyObject *exit = special_lookup(tstate, mgr, &PyId___exit__);
            if (exit == NULL) {
                Py_DECREF(enter);
                goto error;
            }
            SET_TOP(exit);
            Py_DECREF(mgr);
            res = _PyObject_CallNoArg(enter);
            Py_DECREF(enter);
            if (res == NULL)
                goto error;
            /* Setup the finally block before pushing the result
               of __enter__ on the stack. */
            PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + oparg,
                               STACK_LEVEL());

            PUSH(res);
            DISPATCH();
        }

        case TARGET(WITH_EXCEPT_START): {
            /* At the top of the stack are 7 values:
               - (TOP, SECOND, THIRD) = exc_info()
               - (FOURTH, FIFTH, SIXTH) = previous exception for EXCEPT_HANDLER
               - SEVENTH: the context.__exit__ bound method
               We call SEVENTH(TOP, SECOND, THIRD).
               Then we push again the TOP exception and the __exit__
               return value.
            */
            PyObject *exit_func;
            PyObject *exc, *val, *tb, *res;

            exc = TOP();
            val = SECOND();
            tb = THIRD();
            assert(!Py_IsNone(exc));
            assert(!PyLong_Check(exc));
            exit_func = PEEK(7);
            PyObject *stack[4] = {NULL, exc, val, tb};
            res = PyObject_Vectorcall(exit_func, stack + 1,
                    3 | PY_VECTORCALL_ARGUMENTS_OFFSET, NULL);
            if (res == NULL)
                goto error;

            PUSH(res);
            DISPATCH();
        }

        case TARGET(LOAD_METHOD): {
            /* Designed to work in tandem with CALL_METHOD. */
            PyObject *name = GETITEM(names, oparg);
            PyObject *obj = TOP();
            PyObject *meth = NULL;

            int meth_found = shadow.shadow == NULL
                                 ? _PyObject_GetMethod(obj, name, &meth)
                                 : _PyShadow_LoadMethodWithCache(
                                       &shadow, next_instr, obj, name, &meth);

            if (meth == NULL) {
                /* Most likely attribute wasn't found. */
                goto error;
            }

            if (meth_found) {
                /* We can bypass temporary bound method object.
                   meth is unbound method and obj is self.

                   meth | self | arg1 | ... | argN
                 */
                SET_TOP(meth);
                PUSH(obj);  // self
            }
            else {
                /* meth is not an unbound method (but a regular attr, or
                   something was returned by a descriptor protocol).  Set
                   the second element of the stack to NULL, to signal
                   CALL_METHOD that it's not a method call.

                   NULL | meth | arg1 | ... | argN
                */
                SET_TOP(NULL);
                Py_DECREF(obj);
                PUSH(meth);
            }
            DISPATCH();
        }

        case TARGET(CALL_METHOD): {
            /* Designed to work in tamdem with LOAD_METHOD. */
            PyObject **sp, *res, *meth;

            sp = stack_pointer;
            int awaited = IS_AWAITED();

            meth = PEEK(oparg + 2);
            if (meth == NULL) {
                /* `meth` is NULL when LOAD_METHOD thinks that it's not
                   a method call.

                   Stack layout:

                       ... | NULL | callable | arg1 | ... | argN
                                                            ^- TOP()
                                               ^- (-oparg)
                                    ^- (-oparg-1)
                             ^- (-oparg-2)

                   `callable` will be POPed by call_function.
                   NULL will will be POPed manually later.
                */
                res = call_function(tstate,
                    &trace_info,
                    &sp,
                    oparg,
                    NULL,
                    awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
                stack_pointer = sp;
                (void)POP(); /* POP the NULL. */
            }
            else {
                /* This is a method call.  Stack layout:

                     ... | method | self | arg1 | ... | argN
                                                        ^- TOP()
                                           ^- (-oparg)
                                    ^- (-oparg-1)
                           ^- (-oparg-2)

                  `self` and `method` will be POPed by call_function.
                  We'll be passing `oparg + 1` to call_function, to
                  make it accept the `self` as a first argument.
                */
                res = call_function(tstate,
                                    &trace_info,
                                    &sp,
                                    oparg + 1,
                                    NULL,
                                    (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) |
                                        Ci_Py_VECTORCALL_INVOKED_METHOD);
                stack_pointer = sp;
            }
            if (res == NULL) {
                PUSH(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);
            }
            assert(!Ci_PyWaitHandle_CheckExact(res));
            PUSH(res);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(CALL_FUNCTION): {
            PREDICTED(CALL_FUNCTION);
            PyObject **sp, *res;
            sp = stack_pointer;
            int awaited = IS_AWAITED();
            res = call_function(tstate,
                                &trace_info,
                                &sp,
                                oparg,
                                NULL,
                                awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
            stack_pointer = sp;
            if (res == NULL) {
                PUSH(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);
            }
            assert(!Ci_PyWaitHandle_CheckExact(res));
            PUSH(res);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(CALL_FUNCTION_KW): {
            PyObject **sp, *res, *names;

            names = POP();

            assert(PyTuple_Check(names));
            assert(PyTuple_GET_SIZE(names) <= oparg);
            /* We assume without checking that names contains only strings */
            sp = stack_pointer;
            int awaited = IS_AWAITED();
            res = call_function(tstate,
                                &trace_info,
                                &sp,
                                oparg,
                                names,
                                awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
            stack_pointer = sp;
            Py_DECREF(names);

            if (res == NULL) {
                PUSH(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);
            }
            assert(!Ci_PyWaitHandle_CheckExact(res));

            PUSH(res);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(CALL_FUNCTION_EX): {
            PREDICTED(CALL_FUNCTION_EX);
            PyObject *func, *callargs, *kwargs = NULL, *result;
            if (oparg & 0x01) {
                kwargs = POP();
                if (!PyDict_CheckExact(kwargs)) {
                    PyObject *d = PyDict_New();
                    if (d == NULL)
                        goto error;
                    if (_PyDict_MergeEx(d, kwargs, 2) < 0) {
                        Py_DECREF(d);
                        format_kwargs_error(tstate, SECOND(), kwargs);
                        Py_DECREF(kwargs);
                        goto error;
                    }
                    Py_DECREF(kwargs);
                    kwargs = d;
                }
                assert(PyDict_CheckExact(kwargs));
            }
            callargs = POP();
            func = TOP();
            if (!PyTuple_CheckExact(callargs)) {
                if (check_args_iterable(tstate, func, callargs) < 0) {
                    Py_DECREF(callargs);
                    goto error;
                }
                Py_SETREF(callargs, PySequence_Tuple(callargs));
                if (callargs == NULL) {
                    goto error;
                }
            }
            assert(PyTuple_CheckExact(callargs));
            int awaited = IS_AWAITED();
            result = do_call_core(tstate, &trace_info, func, callargs, kwargs, awaited);
            Py_DECREF(func);
            Py_DECREF(callargs);
            Py_XDECREF(kwargs);

            if (result == NULL) {
                SET_TOP(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(result)) {
                DISPATCH_EAGER_CORO_RESULT(result, SET_TOP);
            }
            assert(!Ci_PyWaitHandle_CheckExact(result));
            SET_TOP(result);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(MAKE_FUNCTION): {
            PyObject *qualname = POP();
            PyObject *codeobj = POP();
            PyFunctionObject *func = (PyFunctionObject *)
                PyFunction_NewWithQualName(codeobj, f->f_globals, qualname);

            Py_DECREF(codeobj);
            Py_DECREF(qualname);
            if (func == NULL) {
                goto error;
            }

            if (oparg & 0x08) {
                assert(PyTuple_CheckExact(TOP()));
                func->func_closure = POP();
            }
            if (oparg & 0x04) {
                assert(PyTuple_CheckExact(TOP()));
                func->func_annotations = POP();
            }
            if (oparg & 0x02) {
                assert(PyDict_CheckExact(TOP()));
                func->func_kwdefaults = POP();
            }
            if (oparg & 0x01) {
                assert(PyTuple_CheckExact(TOP()));
                func->func_defaults = POP();
            }

#ifdef ENABLE_CINDERX
            PyEntry_init(func);
#endif

            PUSH((PyObject *)func);
            DISPATCH();
        }

        case TARGET(BUILD_SLICE): {
            PyObject *start, *stop, *step, *slice;
            if (oparg == 3)
                step = POP();
            else
                step = NULL;
            stop = POP();
            start = TOP();
            slice = PySlice_New(start, stop, step);
            Py_DECREF(start);
            Py_DECREF(stop);
            Py_XDECREF(step);
            SET_TOP(slice);
            if (slice == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(FORMAT_VALUE): {
            /* Handles f-string value formatting. */
            PyObject *result;
            PyObject *fmt_spec;
            PyObject *value;
            PyObject *(*conv_fn)(PyObject *);
            int which_conversion = oparg & FVC_MASK;
            int have_fmt_spec = (oparg & FVS_MASK) == FVS_HAVE_SPEC;

            fmt_spec = have_fmt_spec ? POP() : NULL;
            value = POP();

            /* See if any conversion is specified. */
            switch (which_conversion) {
            case FVC_NONE:  conv_fn = NULL;           break;
            case FVC_STR:   conv_fn = PyObject_Str;   break;
            case FVC_REPR:  conv_fn = PyObject_Repr;  break;
            case FVC_ASCII: conv_fn = PyObject_ASCII; break;
            default:
                _PyErr_Format(tstate, PyExc_SystemError,
                              "unexpected conversion flag %d",
                              which_conversion);
                goto error;
            }

            /* If there's a conversion function, call it and replace
               value with that result. Otherwise, just use value,
               without conversion. */
            if (conv_fn != NULL) {
                result = conv_fn(value);
                Py_DECREF(value);
                if (result == NULL) {
                    Py_XDECREF(fmt_spec);
                    goto error;
                }
                value = result;
            }

            /* If value is a unicode object, and there's no fmt_spec,
               then we know the result of format(value) is value
               itself. In that case, skip calling format(). I plan to
               move this optimization in to PyObject_Format()
               itself. */
            if (PyUnicode_CheckExact(value) && fmt_spec == NULL) {
                /* Do nothing, just transfer ownership to result. */
                result = value;
            } else {
                /* Actually call format(). */
                result = PyObject_Format(value, fmt_spec);
                Py_DECREF(value);
                Py_XDECREF(fmt_spec);
                if (result == NULL) {
                    goto error;
                }
            }

            PUSH(result);
            DISPATCH();
        }

        case TARGET(ROT_N): {
            PyObject *top = TOP();
            memmove(&PEEK(oparg - 1), &PEEK(oparg),
                    sizeof(PyObject*) * (oparg - 1));
            PEEK(oparg) = top;
            DISPATCH();
        }

#ifdef ENABLE_CINDERX
        case TARGET(SHADOW_NOP): {
            DISPATCH();
        }

        case TARGET(LOAD_GLOBAL_CACHED): {
            PyObject *name;
            PyObject *v = *global_cache[(unsigned int)oparg];

            if (v == NULL) {
                name = _PyShadow_GetOriginalName(&shadow, next_instr);
                v = _PyDict_LoadGlobal((PyDictObject *)f->f_globals,
                                       (PyDictObject *)f->f_builtins,
                                       name);
                if (v == NULL) {
                    if (!PyErr_Occurred()) {
                        /* _PyDict_LoadGlobal() returns NULL without raising
                         * an exception if the key doesn't exist */
                        format_exc_check_arg(
                            tstate, PyExc_NameError, NAME_ERROR_MSG, name);
                    }
                    goto error;
                }
            }
            Py_INCREF(v);
            PUSH(v);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_NO_DICT_DESCR): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res = _PyShadow_LoadAttrNoDictDescr(
                &shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_DICT_DESCR): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrDictDescr(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_DICT_NO_DESCR): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res = _PyShadow_LoadAttrDictNoDescr(
                &shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SLOT): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrSlot(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            SET_TOP(res);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SPLIT_DICT): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrSplitDict(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            SET_TOP(res);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SPLIT_DICT_DESCR): {
            /* Normal descriptor + split dict.  We're probably looking up a
             * method and likely have a splitoffset of -1 */
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res = _PyShadow_LoadAttrSplitDictDescr(
                &shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_TYPE): {
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *owner = TOP();
            PyObject *res =
                _PyShadow_LoadAttrType(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_MODULE): {
            PyObject *owner = TOP();
            _PyShadow_ModuleAttrEntry *entry =
                _PyShadow_GetModuleAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrModule(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_S_MODULE): {
            PyObject *owner = TOP();
            _PyShadow_ModuleAttrEntry *entry =
                _PyShadow_GetStrictModuleAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrStrictModule(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_UNCACHABLE): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            INLINE_CACHE_UNCACHABLE_TYPE(Py_TYPE(owner));

            INLINE_CACHE_RECORD_STAT(LOAD_ATTR_UNCACHABLE, hits);
            PyObject *res = PyObject_GetAttr(owner, name);
            Py_DECREF(owner);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_POLYMORPHIC): {
            PyObject *owner = TOP();
            PyObject *res;
            _PyShadow_InstanceAttrEntry **entries =
                _PyShadow_GetPolymorphicAttr(&shadow, oparg);
            PyTypeObject *type = Py_TYPE(owner);
            for (int i = 0; i < POLYMORPHIC_CACHE_SIZE; i++) {
                _PyShadow_InstanceAttrEntry *entry = entries[i];
                if (entry == NULL) {
                    continue;
                } else if (entry->type != type) {
                    if (entry->type == NULL) {
                        Py_CLEAR(entries[i]);
                    }
                    continue;
                }

                switch (((_PyCacheType *)Py_TYPE(entry))->load_attr_opcode) {
                case LOAD_ATTR_NO_DICT_DESCR:
                    res = _PyShadow_LoadAttrNoDictDescrHit(entry, owner);
                    break;
                case LOAD_ATTR_DICT_DESCR:
                    res = _PyShadow_LoadAttrDictDescrHit(entry, owner);
                    break;
                case LOAD_ATTR_DICT_NO_DESCR:
                    res = _PyShadow_LoadAttrDictNoDescrHit(entry, owner);
                    break;
                case LOAD_ATTR_SLOT:
                    res = _PyShadow_LoadAttrSlotHit(entry, owner);
                    break;
                case LOAD_ATTR_SPLIT_DICT:
                    res = _PyShadow_LoadAttrSplitDictHit(entry, owner);
                    break;
                case LOAD_ATTR_SPLIT_DICT_DESCR:
                    res = _PyShadow_LoadAttrSplitDictDescrHit(entry, owner);
                    break;
                default:
                    Py_UNREACHABLE();
                    return NULL;
                }
                if (res == NULL)
                    goto error;

                Py_DECREF(owner);
                SET_TOP(res);
                DISPATCH();
            }
            res = _PyShadow_LoadAttrPolymorphic(
                &shadow, next_instr, entries, owner);

            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_UNCACHABLE): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            int err;
            STACK_SHRINK(2);
            err = PyObject_SetAttr(owner, name, v);
            Py_DECREF(v);
            Py_DECREF(owner);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(STORE_ATTR_DICT): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrDict(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_DESCR): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrDescr(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_SPLIT_DICT): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrSplitDict(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_SLOT): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrSlot(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

#define SHADOW_LOAD_METHOD(func, type, helper)                                \
    PyObject *obj = TOP();                                                    \
    PyObject *meth = NULL;                                                    \
    type *entry = helper(&shadow, oparg);                                     \
    int meth_found = func(&shadow, next_instr, entry, obj, &meth);            \
    if (meth == NULL) {                                                       \
        /* Most likely attribute wasn't found. */                             \
        goto error;                                                           \
    }                                                                         \
    if (meth_found) {                                                         \
        SET_TOP(meth);                                                        \
        PUSH(obj);                                                            \
    } else {                                                                  \
        SET_TOP(NULL);                                                        \
        Py_DECREF(obj);                                                       \
        PUSH(meth);                                                           \
    }                                                                         \
    DISPATCH();

        case TARGET(LOAD_METHOD_MODULE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodModule,
                               _PyShadow_ModuleAttrEntry,
                               _PyShadow_GetModuleAttr);
        }

        case TARGET(LOAD_METHOD_S_MODULE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodStrictModule,
                               _PyShadow_ModuleAttrEntry,
                               _PyShadow_GetStrictModuleAttr);
        }


        case TARGET(LOAD_METHOD_SPLIT_DICT_DESCR): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodSplitDictDescr,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_DICT_DESCR): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodDictDescr,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_NO_DICT_DESCR): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodNoDictDescr,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_TYPE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodType,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_TYPE_METHODLIKE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodTypeMethodLike,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_DICT_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodDictMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_SPLIT_DICT_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodSplitDictMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_NO_DICT_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodNoDictMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_UNSHADOWED_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodUnshadowedMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_UNCACHABLE): {
            /* Designed to work in tandem with CALL_METHOD. */
            PyObject *name = GETITEM(names, oparg);
            PyObject *obj = TOP();
            PyObject *meth = NULL;

            int meth_found = _PyObject_GetMethod(obj, name, &meth);

            if (meth == NULL) {
                /* Most likely attribute wasn't found. */
                goto error;
            }

            if (meth_found) {
                /* We can bypass temporary bound method object.
                   meth is unbound method and obj is self.

                   meth | self | arg1 | ... | argN
                 */
                SET_TOP(meth);
                PUSH(obj); // self
            } else {
                /* meth is not an unbound method (but a regular attr, or
                   something was returned by a descriptor protocol).  Set
                   the second element of the stack to NULL, to signal
                   CALL_METHOD that it's not a method call.

                   NULL | meth | arg1 | ... | argN
                */
                SET_TOP(NULL);
                Py_DECREF(obj);
                PUSH(meth);
            }
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_TUPLE_CONST_INT): {
            PyObject *container = TOP();
            PyObject *res;
            PyObject *sub;
            if (PyTuple_CheckExact(container)) {
                Py_ssize_t i = (Py_ssize_t)oparg;
                if (i < 0) {
                    i += PyTuple_GET_SIZE(container);
                }
                if (i < 0 || i >= Py_SIZE(container)) {
                    PyErr_SetString(PyExc_IndexError,
                                    "tuple index out of range");
                    res = NULL;
                } else {
                    res = ((PyTupleObject *)container)->ob_item[oparg];
                    Py_INCREF(res);
                }
            } else {
                sub = PyLong_FromLong(oparg);
                res = PyObject_GetItem(container, sub);
                Py_DECREF(sub);
            }
            Py_DECREF(container);

            SET_TOP(res);
            if (res == NULL)
                goto error;
            // This shadow code is applied when we have
            //      LOAD_CONST i
            //      BINARY_SUBSCR
            // And is patched into BINARY_SUBSCR_TUPLE_CONST_INT i
            // at the position of LOAD_CONST.
            // This means that we should always skip the next instruction
            // (i.e. the BINARY_SUBSCR)
            NEXTOPARG();

            DISPATCH();
        }
        case TARGET(BINARY_SUBSCR_DICT_STR): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyDict_CheckExact(container) && PyUnicode_CheckExact(sub)) {
                res = _PyDict_GetItem_Unicode(container, sub);
                if (res == NULL) {
                    _PyErr_SetKeyError(sub);
                } else {
                    Py_INCREF(res);
                }
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_TUPLE): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyTuple_CheckExact(container)) {
                res = Ci_tuple_subscript(container, sub);
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_LIST): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyList_CheckExact(container)) {
                res = Ci_list_subscript(container, sub);
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_DICT): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyDict_CheckExact(container)) {
                res = Ci_dict_subscript(container, sub);
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;

            DISPATCH();
        }
#endif // ENABLE_CINDERX

        case TARGET(EXTENDED_ARG): {
            int oldoparg = oparg;
            NEXTOPARG();
            oparg |= oldoparg << 8;
            goto dispatch_opcode;
        }

#ifdef ENABLE_CINDERX
#define _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res)   \
            while (nargs--) {                                     \
                Py_DECREF(POP());                                 \
            }                                                     \
            if (res == NULL) {                                    \
                goto error;                                       \
            }                                                     \
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {     \
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);            \
            }                                                     \
            assert(!Ci_PyWaitHandle_CheckExact(res));             \
            PUSH(res);                                            \
            DISPATCH();                                           \

        case TARGET(INVOKE_METHOD): {
            PyObject *value = GETITEM(consts, oparg);
            Py_ssize_t nargs = PyLong_AsLong(PyTuple_GET_ITEM(value, 1)) + 1;
            PyObject *target = PyTuple_GET_ITEM(value, 0);
            int is_classmethod = PyTuple_GET_SIZE(value) == 3 && (PyTuple_GET_ITEM(value, 2) == Py_True);

            Py_ssize_t slot = _PyClassLoader_ResolveMethod(target);
            if (slot == -1) {
                while (nargs--) {
                    Py_DECREF(POP());
                }
                goto error;
            }

            assert(*(next_instr - 2) == EXTENDED_ARG);
            if (shadow.shadow != NULL && nargs < 0x80) {
                PyMethodDescrObject *method;
                if ((method = _PyClassLoader_ResolveMethodDef(target)) != NULL) {
                    int offset =
                        _PyShadow_CacheCastType(&shadow, (PyObject *)method);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, INVOKE_FUNCTION_CACHED, (nargs<<8) | offset);
                    }
                } else {
                  /* We smuggle in the information about whether the invocation was a classmethod
                   * in the low bit of the oparg. This is necessary, as without, the runtime won't
                   * be able to get the correct vtable from self when the type is passed in.
                   */
                    _PyShadow_PatchByteCode(&shadow,
                                            next_instr,
                                            INVOKE_METHOD_CACHED,
                                            (slot << 9) | (nargs << 1) | (is_classmethod ? 1 : 0));
                }
            }

            PyObject **stack = stack_pointer - nargs;
            PyObject *self = *stack;


            _PyType_VTable *vtable;
            if (is_classmethod) {
                vtable = (_PyType_VTable *)(((PyTypeObject *)self)->tp_cache);
            }
            else {
                vtable = (_PyType_VTable *)self->ob_type->tp_cache;
            }

            assert(!PyErr_Occurred());

            int awaited = IS_AWAITED();
            PyObject *res = _PyClassLoader_InvokeMethod(vtable, slot, stack, nargs | (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0));

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

#define FIELD_OFFSET(self, offset) (PyObject **)(((char *)self) + offset)
        case TARGET(LOAD_FIELD): {
            PyObject *field = GETITEM(consts, oparg);
            int field_type;
            Py_ssize_t offset =
                _PyClassLoader_ResolveFieldOffset(field, &field_type);
            if (offset == -1) {
                goto error;
            }
            PyObject *self = TOP();
            PyObject *value;
            if (field_type == TYPED_OBJECT) {
                value = *FIELD_OFFSET(self, offset);
                if (shadow.shadow != NULL) {
                    assert(offset % sizeof(PyObject *) == 0);
                    _PyShadow_PatchByteCode(&shadow,
                                            next_instr,
                                            LOAD_OBJ_FIELD,
                                            offset / sizeof(PyObject *));
                }

                if (value == NULL) {
                    PyObject *name = PyTuple_GET_ITEM(field, PyTuple_GET_SIZE(field) - 1);
                    PyErr_Format(PyExc_AttributeError,
                                 "'%.50s' object has no attribute '%U'",
                                 Py_TYPE(self)->tp_name, name);
                    goto error;
                }
                Py_INCREF(value);
            } else {
                if (shadow.shadow != NULL) {
                    int pos =
                        _PyShadow_CacheFieldType(&shadow, offset, field_type);
                    if (pos != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, LOAD_PRIMITIVE_FIELD, pos);
                    }
                }

                value =
                    load_field(field_type, (char *)FIELD_OFFSET(self, offset));
                if (value == NULL) {
                    goto error;
                }
            }
            Py_DECREF(self);
            SET_TOP(value);
            DISPATCH();
        }

        case TARGET(STORE_FIELD): {
            PyObject *field = GETITEM(consts, oparg);
            int field_type;
            Py_ssize_t offset =
                _PyClassLoader_ResolveFieldOffset(field, &field_type);
            if (offset == -1) {
                goto error;
            }

            PyObject *self = POP();
            PyObject *value = POP();
            PyObject **addr = FIELD_OFFSET(self, offset);

            if (field_type == TYPED_OBJECT) {
                Py_XDECREF(*addr);
                *addr = value;
                if (shadow.shadow != NULL) {
                    assert(offset % sizeof(PyObject *) == 0);
                    _PyShadow_PatchByteCode(&shadow,
                                           next_instr,
                                           STORE_OBJ_FIELD,
                                           offset / sizeof(PyObject *));
                }
            } else {
                if (shadow.shadow != NULL) {
                    int pos =
                        _PyShadow_CacheFieldType(&shadow, offset, field_type);
                    if (pos != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, STORE_PRIMITIVE_FIELD, pos);
                    }
                }
                store_field(field_type, (char *)addr, value);
            }
            Py_DECREF(self);
            DISPATCH();
        }

        case TARGET(SEQUENCE_REPEAT): {
            PyObject *num = TOP();
            PyObject *seq = SECOND();
            PyObject* res;
            STACK_SHRINK(2);

            int seq_inexact = oparg & SEQ_REPEAT_INEXACT_SEQ;
            int num_inexact = oparg & SEQ_REPEAT_INEXACT_NUM;
            int reversed = oparg & SEQ_REPEAT_REVERSED;
            oparg &= ~SEQ_REPEAT_FLAGS;

            assert((oparg == SEQ_LIST) || (oparg == SEQ_TUPLE));

            if (seq_inexact) {
                if ((oparg == SEQ_LIST && PyList_CheckExact(seq)) ||
                    (oparg == SEQ_TUPLE && PyTuple_CheckExact(seq))) {
                    seq_inexact = 0;
                }
            }

            if (num_inexact && PyLong_CheckExact(num)) {
                num_inexact = 0;
            }

            if (seq_inexact || num_inexact) {
                if (reversed) {
                    res = PyNumber_Multiply(num, seq);
                } else {
                    res = PyNumber_Multiply(seq, num);
                }
            } else {
                if (oparg == SEQ_LIST) {
                    res = Ci_List_Repeat((PyListObject*)seq, PyLong_AsSsize_t(num));
                } else {
                    res = Ci_Tuple_Repeat((PyTupleObject*)seq, PyLong_AsSsize_t(num));
                }
            }

            Py_DECREF(num);
            Py_DECREF(seq);
            PUSH(res);

            if (res == NULL) {
                goto error;
            }

            DISPATCH();
        }
#define CAST_COERCE_OR_ERROR(val, type, exact)                              \
    if (type == &PyFloat_Type && PyObject_TypeCheck(val, &PyLong_Type)) {   \
        long lval = PyLong_AsLong(val);                                     \
        Py_DECREF(val);                                                     \
        SET_TOP(PyFloat_FromDouble(lval));                                  \
    } else {                                                                \
        PyErr_Format(PyExc_TypeError,                                       \
                    exact ? "expected exactly '%s', got '%s'"               \
                     : "expected '%s', got '%s'",                           \
                    type->tp_name,                                          \
                    Py_TYPE(val)->tp_name);                                 \
        Py_DECREF(type);                                                    \
        goto error;                                                         \
    }

        case TARGET(CAST): {
            PyObject *val = TOP();
            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(GETITEM(consts, oparg), &optional, &exact);
            if (type == NULL) {
                goto error;
            }
            if (!_PyObject_TypeCheckOptional(val, type, optional, exact)) {
                CAST_COERCE_OR_ERROR(val, type, exact);
            }

            if (shadow.shadow != NULL) {
                int offset =
                    _PyShadow_CacheCastType(&shadow, (PyObject *)type);
                if (offset != -1) {
                    if (optional) {
                        if (exact) {
                            _PyShadow_PatchByteCode(
                                &shadow, next_instr, CAST_CACHED_OPTIONAL_EXACT, offset);
                        } else {
                            _PyShadow_PatchByteCode(
                                &shadow, next_instr, CAST_CACHED_OPTIONAL, offset);
                        }
                    } else if (exact) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, CAST_CACHED_EXACT, offset);
                    } else {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, CAST_CACHED, offset);
                    }
                }
            }
            Py_DECREF(type);
            DISPATCH();
        }

        case TARGET(LOAD_LOCAL): {
            int index =
                _PyLong_AsInt(PyTuple_GET_ITEM(GETITEM(consts, oparg), 0));

            PyObject *value = GETLOCAL(index);
            if (value == NULL) {
                value = PyLong_FromLong(0);
                SETLOCAL(index, value); /* will steal the ref */
            }
            PUSH(value);
            Py_INCREF(value);

            DISPATCH();
        }

        case TARGET(STORE_LOCAL): {
            PyObject *local = GETITEM(consts, oparg);
            int index = _PyLong_AsInt(PyTuple_GET_ITEM(local, 0));
            int type =
                _PyClassLoader_ResolvePrimitiveType(PyTuple_GET_ITEM(local, 1));

            if (type < 0) {
                goto error;
            }

            if (type == TYPED_DOUBLE) {
                SETLOCAL(index, POP());
            } else {
                Py_ssize_t val = unbox_primitive_int_and_decref(POP());
                SETLOCAL(index, box_primitive(type, val));
            }
            if (shadow.shadow != NULL) {
                assert(type < 8);
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, PRIMITIVE_STORE_FAST, (index << 4) | type);
            }

            DISPATCH();
        }

        case TARGET(PRIMITIVE_BOX): {
            if ((oparg & (TYPED_INT_SIGNED)) && oparg != (TYPED_DOUBLE)) {
                /* We have a boxed value on the stack already, but we may have to
                 * deal with sign extension */
                PyObject *val = TOP();
                size_t ival = (size_t)PyLong_AsVoidPtr(val);
                if (ival & ((size_t)1) << 63) {
                    SET_TOP(PyLong_FromSsize_t((int64_t)ival));
                    Py_DECREF(val);
                }
            }
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_ZERO): {
            PyObject *cond = POP();
            int is_nonzero = Py_SIZE(cond);
            Py_DECREF(cond);
            if (!is_nonzero) {
                JUMPTO(oparg);
            }
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_NONZERO): {
            PyObject *cond = POP();
            int is_nonzero = Py_SIZE(cond);
            Py_DECREF(cond);
            if (is_nonzero) { JUMPTO(oparg); }
            DISPATCH();
        }

        case TARGET(PRIMITIVE_UNBOX): {
            /* We always box values in the interpreter loop, so this just does
             * overflow checking here. Oparg indicates the type of the unboxed
             * value. */
            PyObject *top = TOP();
            if (PyLong_CheckExact(top)) {
                size_t value;
                if (!_PyClassLoader_OverflowCheck(top, oparg, &value)) {
                    PyErr_SetString(PyExc_OverflowError, "int overflow");
                    goto error;
                }
            }

            DISPATCH();
        }

#define INT_BIN_OPCODE_UNSIGNED(opid, op)                                     \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        PUSH(PyLong_FromVoidPtr((void *)(((size_t)PyLong_AsVoidPtr(l))op(     \
            (size_t)PyLong_AsVoidPtr(r)))));                                  \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        DISPATCH();                                                           \
    }

#define INT_BIN_OPCODE_SIGNED(opid, op)                                       \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        PUSH(PyLong_FromVoidPtr((void *)(((Py_ssize_t)PyLong_AsVoidPtr(l))op( \
            (Py_ssize_t)PyLong_AsVoidPtr(r)))));                              \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        DISPATCH();                                                           \
    }

#define DOUBLE_BIN_OPCODE(opid, op)                                           \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        PUSH((PyFloat_FromDouble((PyFloat_AS_DOUBLE(l))op(PyFloat_AS_DOUBLE(r))))); \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        DISPATCH();                                                           \
    }

        case TARGET(PRIMITIVE_BINARY_OP): {
            PyObject *l, *r;
            switch (oparg) {
                INT_BIN_OPCODE_SIGNED(PRIM_OP_ADD_INT, +)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_SUB_INT, -)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_MUL_INT, *)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_DIV_INT, /)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_MOD_INT, %)
            case PRIM_OP_POW_INT: {
                r = POP();
                l = POP();
                double power = pow((Py_ssize_t)PyLong_AsVoidPtr(l), (Py_ssize_t) PyLong_AsVoidPtr(r));
                PUSH(PyFloat_FromDouble(power));
                Py_DECREF(r);
                Py_DECREF(l);
                DISPATCH();
              }
            case PRIM_OP_POW_UN_INT: {
                r = POP();
                l = POP();
                double power = pow((size_t)PyLong_AsVoidPtr(l), (size_t) PyLong_AsVoidPtr(r));
                PUSH(PyFloat_FromDouble(power));
                Py_DECREF(r);
                Py_DECREF(l);
                DISPATCH();
              }

                INT_BIN_OPCODE_SIGNED(PRIM_OP_LSHIFT_INT, <<)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_RSHIFT_INT, >>)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_XOR_INT, ^)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_OR_INT, |)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_AND_INT, &)
                INT_BIN_OPCODE_UNSIGNED(PRIM_OP_MOD_UN_INT, %)
                INT_BIN_OPCODE_UNSIGNED(PRIM_OP_DIV_UN_INT, /)
                INT_BIN_OPCODE_UNSIGNED(PRIM_OP_RSHIFT_UN_INT, >>)
                DOUBLE_BIN_OPCODE(PRIM_OP_ADD_DBL, +)
                DOUBLE_BIN_OPCODE(PRIM_OP_SUB_DBL, -)
                DOUBLE_BIN_OPCODE(PRIM_OP_MUL_DBL, *)
                DOUBLE_BIN_OPCODE(PRIM_OP_DIV_DBL, /)
            case PRIM_OP_POW_DBL: {
                r = POP();
                l = POP();
                double power = pow(PyFloat_AsDouble(l), PyFloat_AsDouble(r));
                PUSH(PyFloat_FromDouble(power));
                Py_DECREF(r);
                Py_DECREF(l);
                DISPATCH();
              }
            }

            PyErr_SetString(PyExc_RuntimeError, "unknown op");
            goto error;
        }

#define INT_UNARY_OPCODE(opid, op)                                            \
    case opid: {                                                              \
        val = POP();                                                          \
        PUSH(PyLong_FromVoidPtr((void *)(op(size_t) PyLong_AsVoidPtr(val)))); \
        Py_DECREF(val);                                                       \
        DISPATCH();                                                      \
    }

#define DBL_UNARY_OPCODE(opid, op)                                            \
    case opid: {                                                              \
        val = POP();                                                          \
        PUSH(PyFloat_FromDouble(op(PyFloat_AS_DOUBLE(val))));                 \
        Py_DECREF(val);                                                       \
        DISPATCH();                                                      \
    }

        case TARGET(PRIMITIVE_UNARY_OP): {
            PyObject *val;
            switch (oparg) {
                INT_UNARY_OPCODE(PRIM_OP_NEG_INT, -)
                INT_UNARY_OPCODE(PRIM_OP_INV_INT, ~)
                DBL_UNARY_OPCODE(PRIM_OP_NEG_DBL, -)
                case PRIM_OP_NOT_INT: {
                    val = POP();
                    PyObject *res = PyLong_AsVoidPtr(val) ? Py_False : Py_True;
                    Py_INCREF(res);
                    PUSH(res);
                    Py_DECREF(val);
                    DISPATCH();
                }
            }
            PyErr_SetString(PyExc_RuntimeError, "unknown op");
            goto error;
        }

#define INT_CMP_OPCODE_UNSIGNED(opid, op)                                     \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        right = (size_t)PyLong_AsVoidPtr(r);                                  \
        left = (size_t)PyLong_AsVoidPtr(l);                                   \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        res = (left op right) ? Py_True : Py_False;                           \
        Py_INCREF(res);                                                       \
        PUSH(res);                                                            \
        DISPATCH();                                                           \
    }

#define INT_CMP_OPCODE_SIGNED(opid, op)                                       \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        sright = (Py_ssize_t)PyLong_AsVoidPtr(r);                             \
        sleft = (Py_ssize_t)PyLong_AsVoidPtr(l);                              \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        res = (sleft op sright) ? Py_True : Py_False;                         \
        Py_INCREF(res);                                                       \
        PUSH(res);                                                            \
        DISPATCH();                                                           \
    }

#define DBL_CMP_OPCODE(opid, op)                                              \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        res = ((PyFloat_AS_DOUBLE(l) op PyFloat_AS_DOUBLE(r)) ?               \
                Py_True : Py_False);                                          \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        Py_INCREF(res);                                                       \
        PUSH(res);                                                            \
        DISPATCH();                                                           \
    }

        case TARGET(PRIMITIVE_COMPARE_OP): {
            PyObject *l, *r, *res;
            Py_ssize_t sleft, sright;
            size_t left, right;
            switch (oparg) {
                INT_CMP_OPCODE_SIGNED(PRIM_OP_EQ_INT, ==)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_NE_INT, !=)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_LT_INT, <)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_GT_INT, >)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_LE_INT, <=)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_GE_INT, >=)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_LT_UN_INT, <)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_GT_UN_INT, >)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_LE_UN_INT, <=)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_GE_UN_INT, >=)
                DBL_CMP_OPCODE(PRIM_OP_EQ_DBL, ==)
                DBL_CMP_OPCODE(PRIM_OP_NE_DBL, !=)
                DBL_CMP_OPCODE(PRIM_OP_LT_DBL, <)
                DBL_CMP_OPCODE(PRIM_OP_GT_DBL, >)
                DBL_CMP_OPCODE(PRIM_OP_LE_DBL, <=)
                DBL_CMP_OPCODE(PRIM_OP_GE_DBL, >=)
            }
            PyErr_SetString(PyExc_RuntimeError, "unknown op");
            goto error;
        }

        case TARGET(LOAD_ITERABLE_ARG): {
            // TODO: Revisit this opcode, and perhaps get it to load all
            // elements of an iterable to a stack. That'll help with the
            // compiled code size.
            PyObject *tup = POP();
            int idx = oparg;
            if (!PyTuple_CheckExact(tup)) {
                if (tup->ob_type->tp_iter == NULL && !PySequence_Check(tup)) {
                    PyErr_Format(PyExc_TypeError,
                                 "argument after * "
                                 "must be an iterable, not %.200s",
                                 tup->ob_type->tp_name);
                    Py_DECREF(tup);
                    goto error;
                }
                Py_SETREF(tup, PySequence_Tuple(tup));
                if (tup == NULL) {
                    goto error;
                }
            }
            PyObject *element = PyTuple_GetItem(tup, idx);
            if (!element) {
                Py_DECREF(tup);
                goto error;
            }
            Py_INCREF(element);
            PUSH(element);
            PUSH(tup);
            DISPATCH();
        }

        case TARGET(LOAD_MAPPING_ARG): {
            PyObject *name = POP();
            PyObject *mapping = POP();

            if (!PyDict_Check(mapping) && !Ci_CheckedDict_Check(mapping)) {
                PyErr_Format(PyExc_TypeError,
                             "argument after ** "
                             "must be a dict, not %.200s",
                             mapping->ob_type->tp_name);
                Py_DECREF(name);
                Py_DECREF(mapping);
                goto error;
            }

            PyObject *value = PyDict_GetItemWithError(mapping, name);
            if (value == NULL) {
                if (_PyErr_Occurred(tstate)) {
                    Py_DECREF(name);
                    Py_DECREF(mapping);
                    goto error;
                } else if (oparg == 2) {
                    PyErr_Format(PyExc_TypeError, "missing argument %U", name);
                    goto error;
                } else {
                    /* Default value is on the stack */
                    Py_DECREF(name);
                    Py_DECREF(mapping);
                    DISPATCH();
                }
            } else if (oparg == 3) {
                /* Remove default value */
                Py_DECREF(POP());
            }
            Py_XINCREF(value);
            Py_DECREF(name);
            Py_DECREF(mapping);
            PUSH(value);
            DISPATCH();
        }
        case TARGET(INVOKE_FUNCTION): {
            PyObject *value = GETITEM(consts, oparg);
            Py_ssize_t nargs = PyLong_AsLong(PyTuple_GET_ITEM(value, 1));
            PyObject *target = PyTuple_GET_ITEM(value, 0);
            PyObject *container;
            PyObject *func = _PyClassLoader_ResolveFunction(target, &container);
            if (func == NULL) {
                goto error;
            }
             int awaited = IS_AWAITED();
            PyObject **sp = stack_pointer - nargs;
            PyObject *res = invoke_static_function(func, sp, nargs, awaited);

            if (shadow.shadow != NULL && nargs < 0x80) {
                if (_PyClassLoader_IsImmutable(container)) {
                    /* frozen type, we don't need to worry about indirecting */
                    int offset =
                        _PyShadow_CacheCastType(&shadow, func);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, INVOKE_FUNCTION_CACHED, (nargs<<8) | offset);
                    }
                } else {
                    PyObject **funcptr = _PyClassLoader_GetIndirectPtr(
                        target,
                        func,
                        container
                    );
                    int offset =
                        _PyShadow_CacheFunction(&shadow, funcptr);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, INVOKE_FUNCTION_INDIRECT_CACHED, (nargs<<8) | offset);
                    }
                }
            }

            Py_DECREF(func);
            Py_DECREF(container);

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

        case TARGET(INVOKE_NATIVE): {
            PyObject *value = GETITEM(consts, oparg);
            assert(PyTuple_CheckExact(value));
            PyObject *target = PyTuple_GET_ITEM(value, 0);
            PyObject *name = PyTuple_GET_ITEM(target, 0);
            PyObject *symbol = PyTuple_GET_ITEM(target, 1);
            PyObject *signature = PyTuple_GET_ITEM(value, 1);
            Py_ssize_t nargs = PyTuple_GET_SIZE(signature) - 1;
            PyObject **sp = stack_pointer - nargs;
            PyObject *res = _PyClassloader_InvokeNativeFunction(name, symbol, signature, sp, nargs);
            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, 0, res);
        }

        case TARGET(JUMP_IF_ZERO_OR_POP): {
            PyObject *cond = TOP();
            int is_nonzero = Py_SIZE(cond);
            if (is_nonzero) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            } else {
                JUMPTO(oparg);
            }
            DISPATCH();
        }

        case TARGET(JUMP_IF_NONZERO_OR_POP): {
            PyObject *cond = TOP();
            int is_nonzero = Py_SIZE(cond);
            if (!is_nonzero) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            } else {
                JUMPTO(oparg);
            }
            DISPATCH()
        }

        case TARGET(FAST_LEN): {
            PyObject *collection = POP(), *length = NULL;
            int inexact = oparg & FAST_LEN_INEXACT;
            oparg &= ~FAST_LEN_INEXACT;
            assert(FAST_LEN_LIST <= oparg && oparg <= FAST_LEN_STR);
            if (inexact) {
              if ((oparg == FAST_LEN_LIST && PyList_CheckExact(collection)) ||
                  (oparg == FAST_LEN_DICT && PyDict_CheckExact(collection)) ||
                  (oparg == FAST_LEN_SET && PyAnySet_CheckExact(collection)) ||
                  (oparg == FAST_LEN_TUPLE && PyTuple_CheckExact(collection)) ||
                  (oparg == FAST_LEN_ARRAY && PyStaticArray_CheckExact(collection)) ||
                  (oparg == FAST_LEN_STR && PyUnicode_CheckExact(collection))) {
                inexact = 0;
              }
            }
            if (inexact) {
              Py_ssize_t res = PyObject_Size(collection);
              if (res >= 0) {
                length = PyLong_FromSsize_t(res);
              }
            } else if (oparg == FAST_LEN_DICT) {
              length = PyLong_FromLong(((PyDictObject*)collection)->ma_used);
            } else if (oparg == FAST_LEN_SET) {
              length = PyLong_FromLong(((PySetObject*)collection)->used);
            } else {
              // lists, tuples, arrays are all PyVarObject and use ob_size
              length = PyLong_FromLong(Py_SIZE(collection));
            }
            Py_DECREF(collection);
            if (length == NULL) {
                goto error;
            }
            PUSH(length);
            DISPATCH();
        }

        case TARGET(CONVERT_PRIMITIVE): {
            Py_ssize_t from_type = oparg & 0xFF;
            Py_ssize_t to_type = oparg >> 4;
            Py_ssize_t extend_sign = (from_type & TYPED_INT_SIGNED) && (to_type & TYPED_INT_SIGNED);
            int size = to_type >> 1;
            PyObject *val = TOP();
            size_t ival = (size_t)PyLong_AsVoidPtr(val);

            ival &= trunc_masks[size];

            // Extend the sign if needed
            if (extend_sign != 0 && (ival & signed_bits[size])) {
                ival |= (signex_masks[size]);
            }

            Py_DECREF(val);
            SET_TOP(PyLong_FromSize_t(ival));
            DISPATCH();
        }

        case TARGET(CHECK_ARGS): {
            PyObject *checks = GETITEM(consts, oparg);
            if (shadow.shadow != NULL) {
                _PyTypedArgsInfo *shadow_value = _PyClassLoader_GetTypedArgsInfo(co, 0);
                if (shadow_value != NULL) {
                    int offset =
                        _PyShadow_CacheCastType(&shadow, (PyObject *)shadow_value);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, CHECK_ARGS_CACHED, offset);
                    }
                    Py_DECREF(shadow_value);
                }
            }

            PyObject *local;
            PyObject *type_descr;
            PyTypeObject *type;
            int optional;
            int exact;
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
                local = PyTuple_GET_ITEM(checks, i);
                type_descr = PyTuple_GET_ITEM(checks, i + 1);
                long idx = PyLong_AsLong(local);
                PyObject *val;
                // Look in freevars if necessary
                if (idx < 0) {
                    assert(!_PyErr_Occurred(tstate));
                    val = PyCell_GET(freevars[-(idx + 1)]);
                } else {
                    val = fastlocals[idx];
                }

                type = _PyClassLoader_ResolveType(type_descr, &optional, &exact);
                if (type == NULL) {
                    goto error;
                }

                int primitive = _PyClassLoader_GetTypeCode(type);
                if (primitive == TYPED_BOOL) {
                    optional = 0;
                    Py_DECREF(type);
                    type = &PyBool_Type;
                    Py_INCREF(type);
                } else if (primitive <= TYPED_INT64) {
                    optional = 0;
                    Py_DECREF(type);
                    type = &PyLong_Type;
                    Py_INCREF(type);
                } else if (primitive == TYPED_DOUBLE) {
                    optional = 0;
                    Py_DECREF(type);
                    type = &PyFloat_Type;
                    Py_INCREF(type);
                } else {
                    assert(primitive == TYPED_OBJECT);
                }

                if (!_PyObject_TypeCheckOptional(val, type, optional, exact)) {
                    PyErr_Format(
                        PyExc_TypeError,
                        "%U expected '%s' for argument %U, got '%s'",
                        co->co_name,
                        type->tp_name,
                        idx < 0 ?
                            PyTuple_GetItem(co->co_cellvars, -(idx + 1)) :
                            PyTuple_GetItem(co->co_varnames, idx),
                        Py_TYPE(val)->tp_name);
                    Py_DECREF(type);
                    goto error;
                }

                Py_DECREF(type);

                if (primitive <= TYPED_INT64) {
                    size_t value;
                    if (!_PyClassLoader_OverflowCheck(val, primitive, &value)) {
                        PyErr_SetString(
                            PyExc_OverflowError,
                            "int overflow"
                        );
                        goto error;
                    }
                }
            }

            DISPATCH();
        }

        case TARGET(LOAD_CLASS): {
            PyObject *type_descr = GETITEM(consts, oparg);
            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(type_descr, &optional, &exact);
            if (type == NULL) {
                goto error;
            }
            PUSH((PyObject*)type);
            DISPATCH();
        }

        case TARGET(BUILD_CHECKED_MAP): {
            PyObject *map_info = GETITEM(consts, oparg);
            PyObject *map_type = PyTuple_GET_ITEM(map_info, 0);
            Py_ssize_t map_size = PyLong_AsLong(PyTuple_GET_ITEM(map_info, 1));

            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(map_type, &optional, &exact);
            assert(!optional);

            if (shadow.shadow != NULL) {
                PyObject *cache = PyTuple_New(2);
                if (cache == NULL) {
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 0, (PyObject *)type);
                Py_INCREF(type);
                PyObject *size = PyLong_FromLong(map_size);
                if (size == NULL) {
                    Py_DECREF(cache);
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 1, size);

                int offset = _PyShadow_CacheCastType(&shadow, cache);
                Py_DECREF(cache);
                if (offset != -1) {
                    _PyShadow_PatchByteCode(
                        &shadow, next_instr, BUILD_CHECKED_MAP_CACHED, offset);
                }
            }

            PyObject *map = Ci_CheckedDict_NewPresized(type, map_size);
            if (map == NULL) {
                goto error;
            }
            Py_DECREF(type);

            Ci_BUILD_DICT(map_size);
            DISPATCH();
        }

        case TARGET(SEQUENCE_GET): {
            PyObject *idx = POP(), *sequence, *item;

            Py_ssize_t val = (Py_ssize_t)PyLong_AsVoidPtr(idx);

            if (val == -1 && _PyErr_Occurred(tstate)) {
                Py_DECREF(idx);
                goto error;
            }

            sequence = POP();

            // Adjust index
            if (val < 0) {
                val += Py_SIZE(sequence);
            }

            oparg &= ~SEQ_SUBSCR_UNCHECKED;

            if (oparg == SEQ_LIST) {
                item = PyList_GetItem(sequence, val);
                Py_DECREF(sequence);
                if (item == NULL) {
                    Py_DECREF(idx);
                    goto error;
                }
                Py_INCREF(item);
            } else if (oparg == SEQ_LIST_INEXACT) {
                if (PyList_CheckExact(sequence) ||
                    Py_TYPE(sequence)->tp_as_sequence->sq_item == PyList_Type.tp_as_sequence->sq_item) {
                    item = PyList_GetItem(sequence, val);
                    Py_DECREF(sequence);
                    if (item == NULL) {
                        Py_DECREF(idx);
                        goto error;
                    }
                    Py_INCREF(item);
                } else {
                    item = PyObject_GetItem(sequence, idx);
                    Py_DECREF(sequence);
                    if (item == NULL) {
                        Py_DECREF(idx);
                        goto error;
                    }
                }
            } else if (oparg == SEQ_CHECKED_LIST) {
                item = Ci_CheckedList_GetItem(sequence, val);
                Py_DECREF(sequence);
                if (item == NULL) {
                    Py_DECREF(idx);
                    goto error;
                }
            } else if (oparg == SEQ_ARRAY_INT64) {
                item = _Ci_StaticArray_Get(sequence, val);
                Py_DECREF(sequence);
                if (item == NULL) {
                    Py_DECREF(idx);
                    goto error;
                }
            } else {
                PyErr_Format(PyExc_SystemError, "bad oparg for SEQUENCE_GET: %d",
                    oparg);
                Py_DECREF(idx);
                goto error;
            }

            Py_DECREF(idx);
            PUSH(item);
            DISPATCH();
        }

        case TARGET(SEQUENCE_SET): {
            PyObject *subscr = TOP();
            PyObject *sequence = SECOND();
            PyObject *v = THIRD();
            int err;
            STACK_SHRINK(3);

            Py_ssize_t idx = (Py_ssize_t)PyLong_AsVoidPtr(subscr);
            Py_DECREF(subscr);

            if (idx == -1 && _PyErr_Occurred(tstate)) {
                Py_DECREF(v);
                Py_DECREF(sequence);
                goto error;
            }

            // Adjust index
            if (idx < 0) {
                idx += Py_SIZE(sequence);
            }

            if (oparg == SEQ_LIST) {
                err = PyList_SetItem(sequence, idx, v);

                Py_DECREF(sequence);
                if (err != 0) {
                    Py_DECREF(v);
                    goto error;
                }
            } else if (oparg == SEQ_LIST_INEXACT) {
                if (PyList_CheckExact(sequence) ||
                    Py_TYPE(sequence)->tp_as_sequence->sq_ass_item == PyList_Type.tp_as_sequence->sq_ass_item) {
                    err = PyList_SetItem(sequence, idx, v);

                    Py_DECREF(sequence);
                    if (err != 0) {
                        Py_DECREF(v);
                        goto error;
                    }
                } else {
                    err = PyObject_SetItem(sequence, subscr, v);
                    Py_DECREF(v);
                    Py_DECREF(sequence);
                    if (err != 0) {
                        goto error;
                    }
                }
            } else if (oparg == SEQ_ARRAY_INT64) {
                err = _Ci_StaticArray_Set(sequence, idx, v);

                Py_DECREF(sequence);
                if (err != 0) {
                    Py_DECREF(v);
                    goto error;
                }
            } else {
                PyErr_Format(PyExc_SystemError, "bad oparg for SEQUENCE_SET: %d",
                    oparg);
                goto error;
            }
            DISPATCH();
        }

        case TARGET(LIST_DEL): {
            PyObject *subscr = TOP();
            PyObject *list = SECOND();
            int err;
            STACK_SHRINK(2);

            Py_ssize_t idx = PyLong_AsLong(subscr);
            Py_DECREF(subscr);

            if (idx == -1 && _PyErr_Occurred(tstate)) {
                Py_DECREF(list);
                goto error;
            }

            err = PyList_SetSlice(list, idx, idx+1, NULL);

            Py_DECREF(list);
            if (err != 0) {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(REFINE_TYPE): {
            DISPATCH();
        }

        case TARGET(PRIMITIVE_LOAD_CONST): {
            PyObject* val = PyTuple_GET_ITEM(GETITEM(consts, oparg), 0);
            Py_INCREF(val);
            PUSH(val);
            DISPATCH();
        }

        case TARGET(RETURN_PRIMITIVE): {
            retval = POP();

            /* In the interpreter, we always return a boxed int. We have a boxed
             * value on the stack already, but we may have to deal with sign
             * extension. */
            if (oparg & TYPED_INT_SIGNED && oparg != TYPED_DOUBLE) {
                size_t ival = (size_t)PyLong_AsVoidPtr(retval);
                if (ival & ((size_t)1) << 63) {
                    Py_DECREF(retval);
                    retval = PyLong_FromSsize_t((int64_t)ival);
                }
            }

            assert(f->f_iblock == 0);
            goto exiting;
        }
#endif

        case TARGET(LOAD_METHOD_SUPER): {
            PyObject *pair = GETITEM(consts, oparg);
            PyObject *name_obj = PyTuple_GET_ITEM(pair, 0);
            int name_idx = _PyLong_AsInt(name_obj);
            PyObject *name = GETITEM(names, name_idx);

            assert (PyBool_Check(PyTuple_GET_ITEM(pair, 1)));
            int call_no_args = PyTuple_GET_ITEM(pair, 1) == Py_True;

            PyObject *self = POP();
            PyObject *type = POP();
            PyObject *global_super = POP();

            int meth_found = 0;
            PyObject *attr = Ci_SuperLookupMethodOrAttr(
                tstate, global_super, (PyTypeObject *)type, self, name, call_no_args, &meth_found);
            Py_DECREF(type);
            Py_DECREF(global_super);

            if (attr == NULL) {
                Py_DECREF(self);
                goto error;
            }
            if (meth_found) {
                PUSH(attr);
                PUSH(self);
            }
            else {
                Py_DECREF(self);

                PUSH(NULL);
                PUSH(attr);
            }
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SUPER): {
            PyObject *pair = GETITEM(consts, oparg);
            PyObject *name_obj = PyTuple_GET_ITEM(pair, 0);
            int name_idx = _PyLong_AsInt(name_obj);
            PyObject *name = GETITEM(names, name_idx);

            assert (PyBool_Check(PyTuple_GET_ITEM(pair, 1)));

            int call_no_args = PyTuple_GET_ITEM(pair, 1) == Py_True;

            PyObject *self = POP();
            PyObject *type = POP();
            PyObject *global_super = POP();
            PyObject *attr = Ci_SuperLookupMethodOrAttr(
                tstate, global_super, (PyTypeObject *)type, self, name, call_no_args, NULL);
            Py_DECREF(type);
            Py_DECREF(self);
            Py_DECREF(global_super);

            if (attr == NULL) {
                goto error;
            }
            PUSH(attr);
            DISPATCH();
        }

#ifdef ENABLE_CINDERX
        case TARGET(TP_ALLOC): {
            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(GETITEM(consts, oparg), &optional, &exact);
            assert(!optional);
            if (type == NULL) {
                goto error;
            }

            PyObject *inst = type->tp_alloc(type, 0);
            if (inst == NULL) {
                Py_DECREF(type);
                goto error;
            }
            PUSH(inst);

            if (shadow.shadow != NULL) {
                int offset =
                    _PyShadow_CacheCastType(&shadow, (PyObject *)type);
                if (offset != -1) {
                    _PyShadow_PatchByteCode(
                        &shadow, next_instr, TP_ALLOC_CACHED, offset);
                }
            }
            Py_DECREF(type);
            DISPATCH();
        }

        case TARGET(BUILD_CHECKED_LIST): {
            PyObject *list_info = GETITEM(consts, oparg);
            PyObject *list_type = PyTuple_GET_ITEM(list_info, 0);
            Py_ssize_t list_size = PyLong_AsLong(PyTuple_GET_ITEM(list_info, 1));

            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(list_type, &optional, &exact);
            assert(!optional);

            if (shadow.shadow != NULL) {
                PyObject *cache = PyTuple_New(2);
                if (cache == NULL) {
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 0, (PyObject *)type);
                Py_INCREF(type);
                PyObject *size = PyLong_FromLong(list_size);
                if (size == NULL) {
                    Py_DECREF(cache);
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 1, size);

                int offset = _PyShadow_CacheCastType(&shadow, cache);
                Py_DECREF(cache);
                if (offset != -1) {
                    _PyShadow_PatchByteCode(
                        &shadow, next_instr, BUILD_CHECKED_LIST_CACHED, offset);
                }
            }

            PyObject *list = Ci_CheckedList_New(type, list_size);
            if (list == NULL) {
                goto error;
            }
            Py_DECREF(type);

            while (--list_size >= 0) {
              PyObject *item = POP();
              Ci_List_SET_ITEM(list, list_size, item);
            }
            PUSH(list);
            DISPATCH();
        }

        case TARGET(LOAD_TYPE): {
            PyObject *instance = TOP();
            Py_INCREF(Py_TYPE(instance));
            SET_TOP((PyObject *)Py_TYPE(instance));
            Py_DECREF(instance);
            DISPATCH();
        }

        case TARGET(BUILD_CHECKED_LIST_CACHED): {
            PyObject *cache = _PyShadow_GetCastType(&shadow, oparg);
            PyTypeObject *type = (PyTypeObject *)PyTuple_GET_ITEM(cache, 0);
            Py_ssize_t list_size = PyLong_AsLong(PyTuple_GET_ITEM(cache, 1));

            PyObject *list = Ci_CheckedList_New(type, list_size);
            if (list == NULL) {
                goto error;
            }

            while (--list_size >= 0) {
              PyObject *item = POP();
              PyList_SET_ITEM(list, list_size, item);
            }
            PUSH(list);
            DISPATCH();
        }

        case TARGET(TP_ALLOC_CACHED): {
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            PyObject *inst = type->tp_alloc(type, 0);
            if (inst == NULL) {
                goto error;
            }

            PUSH(inst);
            DISPATCH();
        }

        case TARGET(INVOKE_FUNCTION_CACHED): {
            PyObject *func = _PyShadow_GetCastType(&shadow, oparg & 0xff);
            Py_ssize_t nargs = oparg >> 8;
            int awaited = IS_AWAITED();

            PyObject **sp = stack_pointer - nargs;
            PyObject *res = invoke_static_function(func, sp, nargs, awaited);

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

        case TARGET(INVOKE_FUNCTION_INDIRECT_CACHED): {
            PyObject **funcref = _PyShadow_GetFunction(&shadow, oparg & 0xff);
            Py_ssize_t nargs = oparg >> 8;
            int awaited = IS_AWAITED();

            PyObject **sp = stack_pointer - nargs;
            PyObject *func = *funcref;
            PyObject *res;
            /* For indirect calls we just use _PyObject_Vectorcall, which will
            * handle non-vector call objects as well.  We expect in high-perf
            * situations to either have frozen types or frozen strict modules */
            if (func == NULL) {
                PyObject *target = PyTuple_GET_ITEM(_PyShadow_GetOriginalConst(&shadow, next_instr), 0);
                func = _PyClassLoader_ResolveFunction(target, NULL);
                if (func == NULL) {
                    goto error;
                }

                res = _PyObject_VectorcallTstate(
                    tstate,
                    func,
                    sp,
                    (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) | nargs,
                    NULL
                );
                Py_DECREF(func);
            } else {
                res = _PyObject_VectorcallTstate(
                    tstate,
                    func,
                    sp,
                    (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) | nargs,
                    NULL
                );
            }

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

        case TARGET(BUILD_CHECKED_MAP_CACHED): {
            PyObject *cache = _PyShadow_GetCastType(&shadow, oparg);
            PyTypeObject *type = (PyTypeObject *)PyTuple_GET_ITEM(cache, 0);
            Py_ssize_t map_size = PyLong_AsLong(PyTuple_GET_ITEM(cache, 1));

            PyObject *map = Ci_CheckedDict_NewPresized(type, map_size);
            if (map == NULL) {
                goto error;
            }

            Ci_BUILD_DICT(map_size);
            DISPATCH();
        }

        case TARGET(CHECK_ARGS_CACHED): {
            _PyTypedArgsInfo *checks =
                (_PyTypedArgsInfo *)_PyShadow_GetCastType(&shadow, oparg);
            for (int i = 0; i < Py_SIZE(checks); i++) {
                _PyTypedArgInfo *check = &checks->tai_args[i];
                long idx = check->tai_argnum;
                PyObject *val;
                // Look in freevars if necessary
                if (idx < 0) {
                  assert(!_PyErr_Occurred(tstate));
                  val = PyCell_GET(freevars[-(idx + 1)]);
                } else {
                  val = fastlocals[idx];
                }

                if (!_PyObject_TypeCheckOptional(val, check->tai_type, check->tai_optional, check->tai_exact)) {
                    PyErr_Format(
                        PyExc_TypeError,
                        "%U expected '%s' for argument %U, got '%s'",
                        co->co_name,
                        check->tai_type->tp_name,
                        idx < 0 ?
                            PyTuple_GetItem(co->co_cellvars, -(idx + 1)) :
                            PyTuple_GetItem(co->co_varnames, idx),
                        Py_TYPE(val)->tp_name);
                    goto error;
                }

                if (check->tai_primitive_type != TYPED_OBJECT) {
                    size_t value;
                    if (!_PyClassLoader_OverflowCheck(val, check->tai_primitive_type, &value)) {
                        PyErr_SetString(
                            PyExc_OverflowError,
                            "int overflow"
                        );

                        goto error;
                    }
                }
            }

            DISPATCH();
        }

        case TARGET(PRIMITIVE_STORE_FAST): {
            int type = oparg & 0xF;
            int idx = oparg >> 4;
            PyObject *value = POP();
            if (type == TYPED_DOUBLE) {
                SETLOCAL(idx, POP());
            } else {
                Py_ssize_t val = unbox_primitive_int_and_decref(value);
                SETLOCAL(idx, box_primitive(type, val));
            }

            DISPATCH();
        }

        case TARGET(CAST_CACHED_OPTIONAL): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (!_PyObject_TypeCheckOptional(val, type, /* opt */ 1, /* exact */ 0)) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 0);
            }
            DISPATCH();
        }

        case TARGET(CAST_CACHED): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (!PyObject_TypeCheck(val, type)) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 0);
            }
            DISPATCH();
        }

        case TARGET(CAST_CACHED_EXACT): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (Py_TYPE(val) != type) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 1);
            }
            DISPATCH();
        }

        case TARGET(CAST_CACHED_OPTIONAL_EXACT): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (!_PyObject_TypeCheckOptional(val, type, /* opt */ 1, /* exact */ 1)) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 1);
            }
            DISPATCH();
        }

        case TARGET(LOAD_PRIMITIVE_FIELD): {
            _FieldCache *cache = _PyShadow_GetFieldCache(&shadow, oparg);
            PyObject *value = load_field(cache->type, ((char *)TOP()) + cache->offset);
            if (value == NULL) {
                goto error;
            }

            Py_DECREF(TOP());
            SET_TOP(value);
            DISPATCH();
        }

        case TARGET(STORE_PRIMITIVE_FIELD): {
            _FieldCache *cache = _PyShadow_GetFieldCache(&shadow, oparg);
            PyObject *self = POP();
            PyObject *value = POP();
            store_field(cache->type, ((char *)self) + cache->offset, value);
            Py_DECREF(self);
            DISPATCH();
        }

        case TARGET(LOAD_OBJ_FIELD): {
            PyObject *self = TOP();
            PyObject **addr = FIELD_OFFSET(self, oparg * sizeof(PyObject *));
            PyObject *value = *addr;
            if (value == NULL) {
                PyErr_Format(PyExc_AttributeError,
                             "'%.50s' object has no attribute",
                             Py_TYPE(self)->tp_name);
                goto error;
            }

            Py_INCREF(value);
            Py_DECREF(self);
            SET_TOP(value);
            DISPATCH();
        }

        case TARGET(STORE_OBJ_FIELD): {
            Py_ssize_t offset = oparg * sizeof(PyObject *);
            PyObject *self = POP();
            PyObject *value = POP();
            PyObject **addr = FIELD_OFFSET(self, offset);
            Py_XDECREF(*addr);
            *addr = value;
            Py_DECREF(self);
            DISPATCH();
        }

        case TARGET(INVOKE_METHOD_CACHED): {
            int is_classmethod = oparg & 1;
            Py_ssize_t nargs = (oparg >> 1) & 0xff;
            PyObject **stack = stack_pointer - nargs;
            PyObject *self = *stack;
            _PyType_VTable *vtable;
            if (is_classmethod) {
                vtable = (_PyType_VTable *)(((PyTypeObject *)self)->tp_cache);
            }
            else {
                vtable = (_PyType_VTable *)self->ob_type->tp_cache;
            }

            Py_ssize_t slot = oparg >> 9;

            int awaited = IS_AWAITED();

            assert(!PyErr_Occurred());
            PyObject *res = _PyClassLoader_InvokeMethod(vtable, slot, stack, nargs | (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0));

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }
#endif // ENABLE_CINDERX

#if USE_COMPUTED_GOTOS
        _unknown_opcode:
#endif
        default:
            fprintf(stderr,
                "XXX lineno: %d, opcode: %d\n",
                PyFrame_GetLineNumber(f),
                opcode);
            _PyErr_SetString(tstate, PyExc_SystemError, "unknown opcode");
            goto error;

        } /* switch */

        /* This should never be reached. Every opcode should end with DISPATCH()
           or goto error. */
        Py_UNREACHABLE();

error:
        /* Double-check exception status. */
#ifdef NDEBUG
        if (!_PyErr_Occurred(tstate)) {
            _PyErr_SetString(tstate, PyExc_SystemError,
                             "error return without exception set");
        }
#else
        assert(_PyErr_Occurred(tstate));
#endif

        /* Log traceback info. */
        PyTraceBack_Here(f);

        if (tstate->c_tracefunc != NULL) {
            /* Make sure state is set to FRAME_EXECUTING for tracing */
            assert(f->f_state == FRAME_EXECUTING);
            f->f_state = FRAME_UNWINDING;
            call_exc_trace(tstate->c_tracefunc, tstate->c_traceobj,
                           tstate, f, &trace_info);
        }
exception_unwind:
        f->f_state = FRAME_UNWINDING;
        /* Unwind stacks if an exception occurred */
        while (f->f_iblock > 0) {
            /* Pop the current block. */
            PyTryBlock *b = &f->f_blockstack[--f->f_iblock];

            if (b->b_type == EXCEPT_HANDLER) {
                UNWIND_EXCEPT_HANDLER(b);
                continue;
            }
            UNWIND_BLOCK(b);
            if (b->b_type == SETUP_FINALLY) {
                PyObject *exc, *val, *tb;
                int handler = b->b_handler;
                _PyErr_StackItem *exc_info = tstate->exc_info;
                /* Beware, this invalidates all b->b_* fields */
                PyFrame_BlockSetup(f, EXCEPT_HANDLER, f->f_lasti, STACK_LEVEL());
                PUSH(exc_info->exc_traceback);
                PUSH(exc_info->exc_value);
                if (exc_info->exc_type != NULL) {
                    PUSH(exc_info->exc_type);
                }
                else {
                    Py_INCREF(Py_None);
                    PUSH(Py_None);
                }
                _PyErr_Fetch(tstate, &exc, &val, &tb);
                /* Make the raw exception data
                   available to the handler,
                   so a program can emulate the
                   Python main loop. */
                _PyErr_NormalizeException(tstate, &exc, &val, &tb);
                if (tb != NULL)
                    PyException_SetTraceback(val, tb);
                else
                    PyException_SetTraceback(val, Py_None);
                Py_INCREF(exc);
                exc_info->exc_type = exc;
                Py_INCREF(val);
                exc_info->exc_value = val;
                exc_info->exc_traceback = tb;
                if (tb == NULL)
                    tb = Py_None;
                Py_INCREF(tb);
                PUSH(tb);
                PUSH(val);
                PUSH(exc);
                JUMPTO(handler);
                /* Resume normal execution */
                f->f_state = FRAME_EXECUTING;
                goto main_loop;
            }
        } /* unwind stack */

        /* End the loop as we still have an error */
        break;
    } /* main loop */

    assert(retval == NULL);
    assert(_PyErr_Occurred(tstate));

    /* Pop remaining stack entries. */
    while (!EMPTY()) {
        PyObject *o = POP();
        Py_XDECREF(o);
    }
    f->f_stackdepth = 0;
    f->f_state = FRAME_RAISED;
exiting:
    if (trace_info.cframe.use_tracing) {
        if (tstate->c_tracefunc) {
            if (call_trace_protected(tstate->c_tracefunc, tstate->c_traceobj,
                                     tstate, f, &trace_info, PyTrace_RETURN, retval)) {
                Py_CLEAR(retval);
            }
        }
        if (tstate->c_profilefunc) {
            if (call_trace_protected(tstate->c_profilefunc, tstate->c_profileobj,
                                     tstate, f, &trace_info, PyTrace_RETURN, retval)) {
                Py_CLEAR(retval);
            }
        }
    }

    /* pop frame */
exit_eval_frame:
    /* Restore previous cframe */
    tstate->cframe = trace_info.cframe.previous;
    tstate->cframe->use_tracing = trace_info.cframe.use_tracing;

#ifdef ENABLE_CINDERX
    if (profiled_instrs != 0) {
        _PyJIT_CountProfiledInstrs(f->f_code, profiled_instrs);
    }
#endif

    if (f->f_gen == NULL) {
        _PyShadowFrame_Pop(tstate, &shadow_frame);
    }

    if (PyDTrace_FUNCTION_RETURN_ENABLED())
        dtrace_function_return(f);
    _Py_LeaveRecursiveCall(tstate);
    tstate->frame = f->f_back;
    co->co_mutable->curcalls--;

    return _Py_CheckFunctionResult(tstate, NULL, retval, __func__);
}

static void
format_missing(PyThreadState *tstate, const char *kind,
               PyCodeObject *co, PyObject *names, PyObject *qualname)
{
    int err;
    Py_ssize_t len = PyList_GET_SIZE(names);
    PyObject *name_str, *comma, *tail, *tmp;

    assert(PyList_CheckExact(names));
    assert(len >= 1);
    /* Deal with the joys of natural language. */
    switch (len) {
    case 1:
        name_str = PyList_GET_ITEM(names, 0);
        Py_INCREF(name_str);
        break;
    case 2:
        name_str = PyUnicode_FromFormat("%U and %U",
                                        PyList_GET_ITEM(names, len - 2),
                                        PyList_GET_ITEM(names, len - 1));
        break;
    default:
        tail = PyUnicode_FromFormat(", %U, and %U",
                                    PyList_GET_ITEM(names, len - 2),
                                    PyList_GET_ITEM(names, len - 1));
        if (tail == NULL)
            return;
        /* Chop off the last two objects in the list. This shouldn't actually
           fail, but we can't be too careful. */
        err = PyList_SetSlice(names, len - 2, len, NULL);
        if (err == -1) {
            Py_DECREF(tail);
            return;
        }
        /* Stitch everything up into a nice comma-separated list. */
        comma = PyUnicode_FromString(", ");
        if (comma == NULL) {
            Py_DECREF(tail);
            return;
        }
        tmp = PyUnicode_Join(comma, names);
        Py_DECREF(comma);
        if (tmp == NULL) {
            Py_DECREF(tail);
            return;
        }
        name_str = PyUnicode_Concat(tmp, tail);
        Py_DECREF(tmp);
        Py_DECREF(tail);
        break;
    }
    if (name_str == NULL)
        return;
    _PyErr_Format(tstate, PyExc_TypeError,
                  "%U() missing %i required %s argument%s: %U",
                  qualname,
                  len,
                  kind,
                  len == 1 ? "" : "s",
                  name_str);
    Py_DECREF(name_str);
}

static void
missing_arguments(PyThreadState *tstate, PyCodeObject *co,
                  Py_ssize_t missing, Py_ssize_t defcount,
                  PyObject **fastlocals, PyObject *qualname)
{
    Py_ssize_t i, j = 0;
    Py_ssize_t start, end;
    int positional = (defcount != -1);
    const char *kind = positional ? "positional" : "keyword-only";
    PyObject *missing_names;

    /* Compute the names of the arguments that are missing. */
    missing_names = PyList_New(missing);
    if (missing_names == NULL)
        return;
    if (positional) {
        start = 0;
        end = co->co_argcount - defcount;
    }
    else {
        start = co->co_argcount;
        end = start + co->co_kwonlyargcount;
    }
    for (i = start; i < end; i++) {
        if (GETLOCAL(i) == NULL) {
            PyObject *raw = PyTuple_GET_ITEM(co->co_varnames, i);
            PyObject *name = PyObject_Repr(raw);
            if (name == NULL) {
                Py_DECREF(missing_names);
                return;
            }
            PyList_SET_ITEM(missing_names, j++, name);
        }
    }
    assert(j == missing);
    format_missing(tstate, kind, co, missing_names, qualname);
    Py_DECREF(missing_names);
}

static void
too_many_positional(PyThreadState *tstate, PyCodeObject *co,
                    Py_ssize_t given, PyObject *defaults,
                    PyObject **fastlocals, PyObject *qualname)
{
    int plural;
    Py_ssize_t kwonly_given = 0;
    Py_ssize_t i;
    PyObject *sig, *kwonly_sig;
    Py_ssize_t co_argcount = co->co_argcount;

    assert((co->co_flags & CO_VARARGS) == 0);
    /* Count missing keyword-only args. */
    for (i = co_argcount; i < co_argcount + co->co_kwonlyargcount; i++) {
        if (GETLOCAL(i) != NULL) {
            kwonly_given++;
        }
    }
    Py_ssize_t defcount = defaults == NULL ? 0 : PyTuple_GET_SIZE(defaults);
    if (defcount) {
        Py_ssize_t atleast = co_argcount - defcount;
        plural = 1;
        sig = PyUnicode_FromFormat("from %zd to %zd", atleast, co_argcount);
    }
    else {
        plural = (co_argcount != 1);
        sig = PyUnicode_FromFormat("%zd", co_argcount);
    }
    if (sig == NULL)
        return;
    if (kwonly_given) {
        const char *format = " positional argument%s (and %zd keyword-only argument%s)";
        kwonly_sig = PyUnicode_FromFormat(format,
                                          given != 1 ? "s" : "",
                                          kwonly_given,
                                          kwonly_given != 1 ? "s" : "");
        if (kwonly_sig == NULL) {
            Py_DECREF(sig);
            return;
        }
    }
    else {
        /* This will not fail. */
        kwonly_sig = PyUnicode_FromString("");
        assert(kwonly_sig != NULL);
    }
    _PyErr_Format(tstate, PyExc_TypeError,
                  "%U() takes %U positional argument%s but %zd%U %s given",
                  qualname,
                  sig,
                  plural ? "s" : "",
                  given,
                  kwonly_sig,
                  given == 1 && !kwonly_given ? "was" : "were");
    Py_DECREF(sig);
    Py_DECREF(kwonly_sig);
}

static int
positional_only_passed_as_keyword(PyThreadState *tstate, PyCodeObject *co,
                                  Py_ssize_t kwcount, PyObject* kwnames,
                                  PyObject *qualname)
{
    int posonly_conflicts = 0;
    PyObject* posonly_names = PyList_New(0);

    for(int k=0; k < co->co_posonlyargcount; k++){
        PyObject* posonly_name = PyTuple_GET_ITEM(co->co_varnames, k);

        for (int k2=0; k2<kwcount; k2++){
            /* Compare the pointers first and fallback to PyObject_RichCompareBool*/
            PyObject* kwname = PyTuple_GET_ITEM(kwnames, k2);
            if (kwname == posonly_name){
                if(PyList_Append(posonly_names, kwname) != 0) {
                    goto fail;
                }
                posonly_conflicts++;
                continue;
            }

            int cmp = PyObject_RichCompareBool(posonly_name, kwname, Py_EQ);

            if ( cmp > 0) {
                if(PyList_Append(posonly_names, kwname) != 0) {
                    goto fail;
                }
                posonly_conflicts++;
            } else if (cmp < 0) {
                goto fail;
            }

        }
    }
    if (posonly_conflicts) {
        PyObject* comma = PyUnicode_FromString(", ");
        if (comma == NULL) {
            goto fail;
        }
        PyObject* error_names = PyUnicode_Join(comma, posonly_names);
        Py_DECREF(comma);
        if (error_names == NULL) {
            goto fail;
        }
        _PyErr_Format(tstate, PyExc_TypeError,
                      "%U() got some positional-only arguments passed"
                      " as keyword arguments: '%U'",
                      qualname, error_names);
        Py_DECREF(error_names);
        goto fail;
    }

    Py_DECREF(posonly_names);
    return 0;

fail:
    Py_XDECREF(posonly_names);
    return 1;

}


PyFrameObject *
_PyEval_MakeFrameVector(PyThreadState *tstate,
           PyFrameConstructor *con, PyObject *locals,
           PyObject *const *args, Py_ssize_t argcount,
           PyObject *kwnames)
{
    assert(is_tstate_valid(tstate));

    PyCodeObject *co = (PyCodeObject*)con->fc_code;
    assert(con->fc_defaults == NULL || PyTuple_CheckExact(con->fc_defaults));
    const Py_ssize_t total_args = co->co_argcount + co->co_kwonlyargcount;

    /* Create the frame */
    PyFrameObject *f = _PyFrame_New_NoTrack(tstate, con, locals);
    if (f == NULL) {
        return NULL;
    }
    PyObject **fastlocals = f->f_localsplus;
    PyObject **freevars = f->f_localsplus + co->co_nlocals;

    /* Create a dictionary for keyword parameters (**kwags) */
    PyObject *kwdict;
    Py_ssize_t i;
    if (co->co_flags & CO_VARKEYWORDS) {
        kwdict = PyDict_New();
        if (kwdict == NULL)
            goto fail;
        i = total_args;
        if (co->co_flags & CO_VARARGS) {
            i++;
        }
        SETLOCAL(i, kwdict);
    }
    else {
        kwdict = NULL;
    }

    /* Copy all positional arguments into local variables */
    Py_ssize_t j, n;
    if (argcount > co->co_argcount) {
        n = co->co_argcount;
    }
    else {
        n = argcount;
    }
    for (j = 0; j < n; j++) {
        PyObject *x = args[j];
        Py_INCREF(x);
        SETLOCAL(j, x);
    }

    /* Pack other positional arguments into the *args argument */
    if (co->co_flags & CO_VARARGS) {
        PyObject *u = _PyTuple_FromArray(args + n, argcount - n);
        if (u == NULL) {
            goto fail;
        }
        SETLOCAL(total_args, u);
    }

    /* Handle keyword arguments */
    if (kwnames != NULL) {
        Py_ssize_t kwcount = PyTuple_GET_SIZE(kwnames);
        for (i = 0; i < kwcount; i++) {
            PyObject **co_varnames;
            PyObject *keyword = PyTuple_GET_ITEM(kwnames, i);
            PyObject *value = args[i+argcount];
            Py_ssize_t j;

            if (keyword == NULL || !PyUnicode_Check(keyword)) {
                _PyErr_Format(tstate, PyExc_TypeError,
                            "%U() keywords must be strings",
                          con->fc_qualname);
                goto fail;
            }

            /* Speed hack: do raw pointer compares. As names are
            normally interned this should almost always hit. */
            co_varnames = ((PyTupleObject *)(co->co_varnames))->ob_item;
            for (j = co->co_posonlyargcount; j < total_args; j++) {
                PyObject *varname = co_varnames[j];
                if (varname == keyword) {
                    goto kw_found;
                }
            }

            /* Slow fallback, just in case */
            for (j = co->co_posonlyargcount; j < total_args; j++) {
                PyObject *varname = co_varnames[j];
                int cmp = PyObject_RichCompareBool( keyword, varname, Py_EQ);
                if (cmp > 0) {
                    goto kw_found;
                }
                else if (cmp < 0) {
                    goto fail;
                }
            }

            assert(j >= total_args);
            if (kwdict == NULL) {

                if (co->co_posonlyargcount
                    && positional_only_passed_as_keyword(tstate, co,
                                                        kwcount, kwnames,
                                                     con->fc_qualname))
                {
                    goto fail;
                }

                _PyErr_Format(tstate, PyExc_TypeError,
                            "%U() got an unexpected keyword argument '%S'",
                          con->fc_qualname, keyword);
                goto fail;
            }

            if (PyDict_SetItem(kwdict, keyword, value) == -1) {
                goto fail;
            }
            continue;

        kw_found:
            if (GETLOCAL(j) != NULL) {
                _PyErr_Format(tstate, PyExc_TypeError,
                            "%U() got multiple values for argument '%S'",
                          con->fc_qualname, keyword);
                goto fail;
            }
            Py_INCREF(value);
            SETLOCAL(j, value);
        }
    }

    /* Check the number of positional arguments */
    if ((argcount > co->co_argcount) && !(co->co_flags & CO_VARARGS)) {
        too_many_positional(tstate, co, argcount, con->fc_defaults, fastlocals,
                            con->fc_qualname);
        goto fail;
    }

    /* Add missing positional arguments (copy default values from defs) */
    if (argcount < co->co_argcount) {
        Py_ssize_t defcount = con->fc_defaults == NULL ? 0 : PyTuple_GET_SIZE(con->fc_defaults);
        Py_ssize_t m = co->co_argcount - defcount;
        Py_ssize_t missing = 0;
        for (i = argcount; i < m; i++) {
            if (GETLOCAL(i) == NULL) {
                missing++;
            }
        }
        if (missing) {
            missing_arguments(tstate, co, missing, defcount, fastlocals,
                              con->fc_qualname);
            goto fail;
        }
        if (n > m)
            i = n - m;
        else
            i = 0;
        if (defcount) {
            PyObject **defs = &PyTuple_GET_ITEM(con->fc_defaults, 0);
            for (; i < defcount; i++) {
                if (GETLOCAL(m+i) == NULL) {
                    PyObject *def = defs[i];
                    Py_INCREF(def);
                    SETLOCAL(m+i, def);
                }
            }
        }
    }

    /* Add missing keyword arguments (copy default values from kwdefs) */
    if (co->co_kwonlyargcount > 0) {
        Py_ssize_t missing = 0;
        for (i = co->co_argcount; i < total_args; i++) {
            if (GETLOCAL(i) != NULL)
                continue;
            PyObject *varname = PyTuple_GET_ITEM(co->co_varnames, i);
            if (con->fc_kwdefaults != NULL) {
                PyObject *def = PyDict_GetItemWithError(con->fc_kwdefaults, varname);
                if (def) {
                    Py_INCREF(def);
                    SETLOCAL(i, def);
                    continue;
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto fail;
                }
            }
            missing++;
        }
        if (missing) {
            missing_arguments(tstate, co, missing, -1, fastlocals,
                              con->fc_qualname);
            goto fail;
        }
    }

    /* Allocate and initialize storage for cell vars, and copy free
       vars into frame. */
    for (i = 0; i < PyTuple_GET_SIZE(co->co_cellvars); ++i) {
        PyObject *c;
        Py_ssize_t arg;
        /* Possibly account for the cell variable being an argument. */
        if (co->co_cell2arg != NULL &&
            (arg = co->co_cell2arg[i]) != CO_CELL_NOT_AN_ARG) {
            c = PyCell_New(GETLOCAL(arg));
            /* Clear the local copy. */
            SETLOCAL(arg, NULL);
        }
        else {
            c = PyCell_New(NULL);
        }
        if (c == NULL)
            goto fail;
        SETLOCAL(co->co_nlocals + i, c);
    }

    /* Copy closure variables to free variables */
    for (i = 0; i < PyTuple_GET_SIZE(co->co_freevars); ++i) {
        PyObject *o = PyTuple_GET_ITEM(con->fc_closure, i);
        Py_INCREF(o);
        freevars[PyTuple_GET_SIZE(co->co_cellvars) + i] = o;
    }

    return f;

fail: /* Jump here from prelude on failure */

    /* decref'ing the frame can cause __del__ methods to get invoked,
       which can call back into Python.  While we're done with the
       current Python frame (f), the associated C stack is still in use,
       so recursion_depth must be boosted for the duration.
    */
    if (Py_REFCNT(f) > 1) {
        Py_DECREF(f);
        _PyObject_GC_TRACK(f);
    }
    else {
        ++tstate->recursion_depth;
        Py_DECREF(f);
        --tstate->recursion_depth;
    }
    return NULL;
}

static PyObject *
make_coro(PyFrameConstructor *con, PyFrameObject *f)
{
    assert (((PyCodeObject *)con->fc_code)->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR));
    PyObject *gen;
    int is_coro = ((PyCodeObject *)con->fc_code)->co_flags & CO_COROUTINE;

    /* Don't need to keep the reference to f_back, it will be set
        * when the generator is resumed. */
    Py_CLEAR(f->f_back);

    /* Create a new generator that owns the ready to run frame
        * and return that as the value. */
    if (is_coro) {
            gen = PyCoro_New(f, con->fc_name, con->fc_qualname);
    } else if (((PyCodeObject *)con->fc_code)->co_flags & CO_ASYNC_GENERATOR) {
            gen = PyAsyncGen_New(f, con->fc_name, con->fc_qualname);
    } else {
            gen = PyGen_NewWithQualName(f, con->fc_name, con->fc_qualname);
    }
    if (gen == NULL) {
        return NULL;
    }

    _PyObject_GC_TRACK(f);

    return gen;
}

PyObject *
_PyEval_Vector(PyThreadState *tstate, PyFrameConstructor *con,
               PyObject *locals,
               PyObject* const* args, size_t argcountf,
               PyObject *kwnames)
{
    Py_ssize_t argcount = PyVectorcall_NARGS(argcountf);
    Py_ssize_t awaited = Ci_Py_AWAITED_CALL(argcountf);
    PyFrameObject *f = _PyEval_MakeFrameVector(
        tstate, con, locals, args, argcount, kwnames);
    if (f == NULL) {
        return NULL;
    }
    const int co_flags = ((PyCodeObject *)con->fc_code)->co_flags;
    if (awaited && (co_flags & CO_COROUTINE)) {
        return _PyEval_EvalEagerCoro(tstate, f, f->f_code->co_name, con->fc_qualname);
    }
    if (co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)) {
        return make_coro(con, f);
    }
    PyObject *retval = _PyEval_EvalFrame(tstate, f, 0);

    /* decref'ing the frame can cause __del__ methods to get invoked,
       which can call back into Python.  While we're done with the
       current Python frame (f), the associated C stack is still in use,
       so recursion_depth must be boosted for the duration.
    */
    if (Py_REFCNT(f) > 1) {
        Py_DECREF(f);
        _PyObject_GC_TRACK(f);
    }
    else {
        ++tstate->recursion_depth;
        Py_DECREF(f);
        --tstate->recursion_depth;
    }
    return retval;
}

/* Legacy API */
PyObject *
PyEval_EvalCodeEx(PyObject *_co, PyObject *globals, PyObject *locals,
                  PyObject *const *args, int argcount,
                  PyObject *const *kws, int kwcount,
                  PyObject *const *defs, int defcount,
                  PyObject *kwdefs, PyObject *closure)
{
    PyThreadState *tstate = _PyThreadState_GET();
    PyObject *res = NULL;
    PyObject *defaults = _PyTuple_FromArray(defs, defcount);
    if (defaults == NULL) {
        return NULL;
    }
    PyObject *builtins = _PyEval_BuiltinsFromGlobals(tstate, globals); // borrowed ref
    if (builtins == NULL) {
        Py_DECREF(defaults);
        return NULL;
    }
    if (locals == NULL) {
        locals = globals;
    }
    PyObject *kwnames = NULL;
    PyObject *const *allargs;
    PyObject **newargs = NULL;
    if (kwcount == 0) {
        allargs = args;
    }
    else {
        kwnames = PyTuple_New(kwcount);
        if (kwnames == NULL) {
            goto fail;
        }
        newargs = PyMem_Malloc(sizeof(PyObject *)*(kwcount+argcount));
        if (newargs == NULL) {
            goto fail;
        }
        for (int i = 0; i < argcount; i++) {
            newargs[i] = args[i];
        }
        for (int i = 0; i < kwcount; i++) {
            Py_INCREF(kws[2*i]);
            PyTuple_SET_ITEM(kwnames, i, kws[2*i]);
            newargs[argcount+i] = kws[2*i+1];
        }
        allargs = newargs;
    }
    for (int i = 0; i < kwcount; i++) {
        Py_INCREF(kws[2*i]);
        PyTuple_SET_ITEM(kwnames, i, kws[2*i]);
    }
    PyFrameConstructor constr = {
        .fc_globals = globals,
        .fc_builtins = builtins,
        .fc_name = ((PyCodeObject *)_co)->co_name,
        .fc_qualname = ((PyCodeObject *)_co)->co_name,
        .fc_code = _co,
        .fc_defaults = defaults,
        .fc_kwdefaults = kwdefs,
        .fc_closure = closure
    };
    res = _PyEval_Vector(tstate, &constr, locals,
                         allargs, argcount,
                         kwnames);
fail:
    Py_XDECREF(kwnames);
    PyMem_Free(newargs);
    Py_DECREF(defaults);
    return res;
}

#ifdef ENABLE_CINDERX
static inline int8_t
unbox_primitive_bool_and_decref(PyObject *x)
{
    assert(PyBool_Check(x));
    int8_t res = (x == Py_True) ? 1 : 0;
    Py_DECREF(x);
    return res;
}
#endif

PyObject *
special_lookup(PyThreadState *tstate, PyObject *o, _Py_Identifier *id)
{
    PyObject *res;
    res = _PyObject_LookupSpecial(o, id);
    if (res == NULL && !_PyErr_Occurred(tstate)) {
        _PyErr_SetObject(tstate, PyExc_AttributeError, _PyUnicode_FromId(id));
        return NULL;
    }
    return res;
}


/* Logic for the raise statement (too complicated for inlining).
   This *consumes* a reference count to each of its arguments. */
int
do_raise(PyThreadState *tstate, PyObject *exc, PyObject *cause)
{
    PyObject *type = NULL, *value = NULL;

    if (exc == NULL) {
        /* Reraise */
        _PyErr_StackItem *exc_info = _PyErr_GetTopmostException(tstate);
        PyObject *tb;
        type = exc_info->exc_type;
        value = exc_info->exc_value;
        tb = exc_info->exc_traceback;
        if (Py_IsNone(type) || type == NULL) {
            _PyErr_SetString(tstate, PyExc_RuntimeError,
                             "No active exception to reraise");
            return 0;
        }
        Py_XINCREF(type);
        Py_XINCREF(value);
        Py_XINCREF(tb);
        _PyErr_Restore(tstate, type, value, tb);
        return 1;
    }

    /* We support the following forms of raise:
       raise
       raise <instance>
       raise <type> */

    if (PyExceptionClass_Check(exc)) {
        type = exc;
        value = _PyObject_CallNoArg(exc);
        if (value == NULL)
            goto raise_error;
        if (!PyExceptionInstance_Check(value)) {
            _PyErr_Format(tstate, PyExc_TypeError,
                          "calling %R should have returned an instance of "
                          "BaseException, not %R",
                          type, Py_TYPE(value));
             goto raise_error;
        }
    }
    else if (PyExceptionInstance_Check(exc)) {
        value = exc;
        type = PyExceptionInstance_Class(exc);
        Py_INCREF(type);
    }
    else {
        /* Not something you can raise.  You get an exception
           anyway, just not what you specified :-) */
        Py_DECREF(exc);
        _PyErr_SetString(tstate, PyExc_TypeError,
                         "exceptions must derive from BaseException");
        goto raise_error;
    }

    assert(type != NULL);
    assert(value != NULL);

    if (cause) {
        PyObject *fixed_cause;
        if (PyExceptionClass_Check(cause)) {
            fixed_cause = _PyObject_CallNoArg(cause);
            if (fixed_cause == NULL)
                goto raise_error;
            Py_DECREF(cause);
        }
        else if (PyExceptionInstance_Check(cause)) {
            fixed_cause = cause;
        }
        else if (Py_IsNone(cause)) {
            Py_DECREF(cause);
            fixed_cause = NULL;
        }
        else {
            _PyErr_SetString(tstate, PyExc_TypeError,
                             "exception causes must derive from "
                             "BaseException");
            goto raise_error;
        }
        PyException_SetCause(value, fixed_cause);
    }

    _PyErr_SetObject(tstate, type, value);
    /* _PyErr_SetObject incref's its arguments */
    Py_DECREF(value);
    Py_DECREF(type);
    return 0;

raise_error:
    Py_XDECREF(value);
    Py_XDECREF(type);
    Py_XDECREF(cause);
    return 0;
}

/* Iterate v argcnt times and store the results on the stack (via decreasing
   sp).  Return 1 for success, 0 if error.

   If argcntafter == -1, do a simple unpack. If it is >= 0, do an unpack
   with a variable target.
*/

static int
unpack_iterable(PyThreadState *tstate, PyObject *v,
                int argcnt, int argcntafter, PyObject **sp)
{
    int i = 0, j = 0;
    Py_ssize_t ll = 0;
    PyObject *it;  /* iter(v) */
    PyObject *w;
    PyObject *l = NULL; /* variable list */

    assert(v != NULL);

    it = PyObject_GetIter(v);
    if (it == NULL) {
        if (_PyErr_ExceptionMatches(tstate, PyExc_TypeError) &&
            Py_TYPE(v)->tp_iter == NULL && !PySequence_Check(v))
        {
            _PyErr_Format(tstate, PyExc_TypeError,
                          "cannot unpack non-iterable %.200s object",
                          Py_TYPE(v)->tp_name);
        }
        return 0;
    }

    for (; i < argcnt; i++) {
        w = PyIter_Next(it);
        if (w == NULL) {
            /* Iterator done, via error or exhaustion. */
            if (!_PyErr_Occurred(tstate)) {
                if (argcntafter == -1) {
                    _PyErr_Format(tstate, PyExc_ValueError,
                                  "not enough values to unpack "
                                  "(expected %d, got %d)",
                                  argcnt, i);
                }
                else {
                    _PyErr_Format(tstate, PyExc_ValueError,
                                  "not enough values to unpack "
                                  "(expected at least %d, got %d)",
                                  argcnt + argcntafter, i);
                }
            }
            goto Error;
        }
        *--sp = w;
    }

    if (argcntafter == -1) {
        /* We better have exhausted the iterator now. */
        w = PyIter_Next(it);
        if (w == NULL) {
            if (_PyErr_Occurred(tstate))
                goto Error;
            Py_DECREF(it);
            return 1;
        }
        Py_DECREF(w);
        _PyErr_Format(tstate, PyExc_ValueError,
                      "too many values to unpack (expected %d)",
                      argcnt);
        goto Error;
    }

    l = PySequence_List(it);
    if (l == NULL)
        goto Error;
    *--sp = l;
    i++;

    ll = PyList_GET_SIZE(l);
    if (ll < argcntafter) {
        _PyErr_Format(tstate, PyExc_ValueError,
            "not enough values to unpack (expected at least %d, got %zd)",
            argcnt + argcntafter, argcnt + ll);
        goto Error;
    }

    /* Pop the "after-variable" args off the list. */
    for (j = argcntafter; j > 0; j--, i++) {
        *--sp = PyList_GET_ITEM(l, ll - j);
    }
    /* Resize the list. */
    Py_SET_SIZE(l, ll - argcntafter);
    Py_DECREF(it);
    return 1;

Error:
    for (; i > 0; i--, sp++)
        Py_DECREF(*sp);
    Py_XDECREF(it);
    return 0;
}

#ifdef LLTRACE
static int
prtrace(PyThreadState *tstate, PyObject *v, const char *str)
{
    printf("%s ", str);
    PyObject *type, *value, *traceback;
    PyErr_Fetch(&type, &value, &traceback);
    if (PyObject_Print(v, stdout, 0) != 0) {
        /* Don't know what else to do */
        _PyErr_Clear(tstate);
    }
    printf("\n");
    PyErr_Restore(type, value, traceback);
    // gh-91924: PyObject_Print() can indirectly set lltrace to 0
    lltrace = 1;
    return 1;
}
#endif

static void
call_exc_trace(Py_tracefunc func, PyObject *self,
               PyThreadState *tstate,
               PyFrameObject *f,
               PyTraceInfo *trace_info)
{
    PyObject *type, *value, *traceback, *orig_traceback, *arg;
    int err;
    _PyErr_Fetch(tstate, &type, &value, &orig_traceback);
    if (value == NULL) {
        value = Py_None;
        Py_INCREF(value);
    }
    _PyErr_NormalizeException(tstate, &type, &value, &orig_traceback);
    traceback = (orig_traceback != NULL) ? orig_traceback : Py_None;
    arg = PyTuple_Pack(3, type, value, traceback);
    if (arg == NULL) {
        _PyErr_Restore(tstate, type, value, orig_traceback);
        return;
    }
    err = call_trace(func, self, tstate, f, trace_info, PyTrace_EXCEPTION, arg);
    Py_DECREF(arg);
    if (err == 0) {
        _PyErr_Restore(tstate, type, value, orig_traceback);
    }
    else {
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(orig_traceback);
    }
}

static int
call_trace_protected(Py_tracefunc func, PyObject *obj,
                     PyThreadState *tstate, PyFrameObject *frame,
                     PyTraceInfo *trace_info,
                     int what, PyObject *arg)
{
    PyObject *type, *value, *traceback;
    int err;
    _PyErr_Fetch(tstate, &type, &value, &traceback);
    err = call_trace(func, obj, tstate, frame, trace_info, what, arg);
    if (err == 0)
    {
        _PyErr_Restore(tstate, type, value, traceback);
        return 0;
    }
    else {
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        return -1;
    }
}

static void
initialize_trace_info(PyTraceInfo *trace_info, PyFrameObject *frame)
{
    if (trace_info->code != frame->f_code) {
        trace_info->code = frame->f_code;
        _PyCode_InitAddressRange(frame->f_code, &trace_info->bounds);
    }
}

static int
call_trace(Py_tracefunc func, PyObject *obj,
           PyThreadState *tstate, PyFrameObject *frame,
           PyTraceInfo *trace_info,
           int what, PyObject *arg)
{
    int result;
    if (tstate->tracing)
        return 0;
    tstate->tracing++;
    tstate->cframe->use_tracing = 0;
    if (frame->f_lasti < 0) {
        frame->f_lineno = frame->f_code->co_firstlineno;
    }
    else {
        initialize_trace_info(trace_info, frame);
        frame->f_lineno = _PyCode_CheckLineNumber(frame->f_lasti*sizeof(_Py_CODEUNIT), &trace_info->bounds);
    }
    result = func(obj, frame, what, arg);
    frame->f_lineno = 0;
    tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
    tstate->tracing--;
    return result;
}

PyObject *
_PyEval_CallTracing(PyObject *func, PyObject *args)
{
    PyThreadState *tstate = _PyThreadState_GET();
    int save_tracing = tstate->tracing;
    int save_use_tracing = tstate->cframe->use_tracing;
    PyObject *result;

    tstate->tracing = 0;
    tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
    result = PyObject_Call(func, args, NULL);
    tstate->tracing = save_tracing;
    tstate->cframe->use_tracing = save_use_tracing;
    return result;
}

/* See Objects/lnotab_notes.txt for a description of how tracing works. */
static int
maybe_call_line_trace(Py_tracefunc func, PyObject *obj,
                      PyThreadState *tstate, PyFrameObject *frame,
                      PyTraceInfo *trace_info, int instr_prev)
{
    int result = 0;

    /* If the last instruction falls at the start of a line or if it
       represents a jump backwards, update the frame's line number and
       then call the trace function if we're tracing source lines.
    */
    initialize_trace_info(trace_info, frame);
    int lastline = _PyCode_CheckLineNumber(instr_prev*sizeof(_Py_CODEUNIT), &trace_info->bounds);
    int line = _PyCode_CheckLineNumber(frame->f_lasti*sizeof(_Py_CODEUNIT), &trace_info->bounds);
    if (line != -1 && frame->f_trace_lines) {
        /* Trace backward edges or if line number has changed */
        if (frame->f_lasti < instr_prev || line != lastline) {
            result = call_trace(func, obj, tstate, frame, trace_info, PyTrace_LINE, Py_None);
        }
    }
    /* Always emit an opcode event if we're tracing all opcodes. */
    if (frame->f_trace_opcodes) {
        result = call_trace(func, obj, tstate, frame, trace_info, PyTrace_OPCODE, Py_None);
    }
    return result;
}

int
_PyEval_SetProfile(PyThreadState *tstate, Py_tracefunc func, PyObject *arg)
{
    assert(is_tstate_valid(tstate));
    /* The caller must hold the GIL */
    assert(PyGILState_Check());

    /* Call _PySys_Audit() in the context of the current thread state,
       even if tstate is not the current thread state. */
    PyThreadState *current_tstate = _PyThreadState_GET();
    if (_PySys_Audit(current_tstate, "sys.setprofile", NULL) < 0) {
        return -1;
    }

    PyObject *profileobj = tstate->c_profileobj;

    tstate->c_profilefunc = NULL;
    tstate->c_profileobj = NULL;
    /* Must make sure that tracing is not ignored if 'profileobj' is freed */
    tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
    Py_XDECREF(profileobj);

    Py_XINCREF(arg);
    tstate->c_profileobj = arg;
    tstate->c_profilefunc = func;

    /* Flag that tracing or profiling is turned on */
    tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
    return 0;
}

void
PyEval_SetProfile(Py_tracefunc func, PyObject *arg)
{
    PyThreadState *tstate = _PyThreadState_GET();
    if (_PyEval_SetProfile(tstate, func, arg) < 0) {
        /* Log _PySys_Audit() error */
        _PyErr_WriteUnraisableMsg("in PyEval_SetProfile", NULL);
    }
}

int
_PyEval_SetTrace(PyThreadState *tstate, Py_tracefunc func, PyObject *arg)
{
    assert(is_tstate_valid(tstate));
    /* The caller must hold the GIL */
    assert(PyGILState_Check());

    /* Call _PySys_Audit() in the context of the current thread state,
       even if tstate is not the current thread state. */
    PyThreadState *current_tstate = _PyThreadState_GET();
    if (_PySys_Audit(current_tstate, "sys.settrace", NULL) < 0) {
        return -1;
    }

    PyObject *traceobj = tstate->c_traceobj;

    tstate->c_tracefunc = NULL;
    tstate->c_traceobj = NULL;
    /* Must make sure that profiling is not ignored if 'traceobj' is freed */
    tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
    Py_XDECREF(traceobj);

    Py_XINCREF(arg);
    tstate->c_traceobj = arg;
    tstate->c_tracefunc = func;

    /* Flag that tracing or profiling is turned on */
    tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);

    return 0;
}

void
PyEval_SetTrace(Py_tracefunc func, PyObject *arg)
{
    PyThreadState *tstate = _PyThreadState_GET();
    if (_PyEval_SetTrace(tstate, func, arg) < 0) {
        /* Log _PySys_Audit() error */
        _PyErr_WriteUnraisableMsg("in PyEval_SetTrace", NULL);
    }
}


void
_PyEval_SetCoroutineOriginTrackingDepth(PyThreadState *tstate, int new_depth)
{
    assert(new_depth >= 0);
    tstate->coroutine_origin_tracking_depth = new_depth;
}

int
_PyEval_GetCoroutineOriginTrackingDepth(void)
{
    PyThreadState *tstate = _PyThreadState_GET();
    return tstate->coroutine_origin_tracking_depth;
}

int
_PyEval_SetAsyncGenFirstiter(PyObject *firstiter)
{
    PyThreadState *tstate = _PyThreadState_GET();

    if (_PySys_Audit(tstate, "sys.set_asyncgen_hook_firstiter", NULL) < 0) {
        return -1;
    }

    Py_XINCREF(firstiter);
    Py_XSETREF(tstate->async_gen_firstiter, firstiter);
    return 0;
}

PyObject *
_PyEval_GetAsyncGenFirstiter(void)
{
    PyThreadState *tstate = _PyThreadState_GET();
    return tstate->async_gen_firstiter;
}

int
_PyEval_SetAsyncGenFinalizer(PyObject *finalizer)
{
    PyThreadState *tstate = _PyThreadState_GET();

    if (_PySys_Audit(tstate, "sys.set_asyncgen_hook_finalizer", NULL) < 0) {
        return -1;
    }

    Py_XINCREF(finalizer);
    Py_XSETREF(tstate->async_gen_finalizer, finalizer);
    return 0;
}

PyObject *
_PyEval_GetAsyncGenFinalizer(void)
{
    PyThreadState *tstate = _PyThreadState_GET();
    return tstate->async_gen_finalizer;
}

PyFrameObject *
PyEval_GetFrame(void)
{
    PyThreadState* tstate = _PyThreadState_GET();
#ifdef ENABLE_CINDERX
    return _PyJIT_GetFrame(tstate);
#else
    return tstate->frame;
#endif
}

PyObject *
_PyEval_GetBuiltins(PyThreadState *tstate)
{
#ifdef ENABLE_CINDERX
    return _PyJIT_GetBuiltins(tstate);
#else
    PyFrameObject *frame = tstate->frame;
    if (frame != NULL) {
        return frame->f_builtins;
    }
    return tstate->interp->builtins;
#endif
}

PyObject *
PyEval_GetBuiltins(void)
{
    PyThreadState *tstate = _PyThreadState_GET();
    return _PyEval_GetBuiltins(tstate);
}

/* Convenience function to get a builtin from its name */
PyObject *
_PyEval_GetBuiltinId(_Py_Identifier *name)
{
    PyThreadState *tstate = _PyThreadState_GET();
    PyObject *attr = _PyDict_GetItemIdWithError(PyEval_GetBuiltins(), name);
    if (attr) {
        Py_INCREF(attr);
    }
    else if (!_PyErr_Occurred(tstate)) {
        _PyErr_SetObject(tstate, PyExc_AttributeError, _PyUnicode_FromId(name));
    }
    return attr;
}

PyObject *
PyEval_GetLocals(void)
{
    PyThreadState *tstate = _PyThreadState_GET();
    PyFrameObject *current_frame = PyThreadState_GetFrame(tstate);
    if (current_frame == NULL) {
        _PyErr_SetString(tstate, PyExc_SystemError, "frame does not exist");
        return NULL;
    }
    Py_DECREF(current_frame);

    if (PyFrame_FastToLocalsWithError(current_frame) < 0) {
        return NULL;
    }

    assert(current_frame->f_locals != NULL);
    return current_frame->f_locals;
}

PyObject *
_PyEval_GetGlobals(PyThreadState *tstate)
{
#ifdef ENABLE_CINDERX
    return _PyJIT_GetGlobals(tstate);
#else
    PyFrameObject *current_frame = tstate->frame;
    if (current_frame == NULL) {
        return NULL;
    }
    assert(current_frame->f_globals != NULL);
    return current_frame->f_globals;
#endif
}

PyObject *
PyEval_GetGlobals(void)
{
    PyThreadState *tstate = _PyThreadState_GET();
    return _PyEval_GetGlobals(tstate);
}

#ifdef ENABLE_CINDERX
static CiStackWalkDirective
Ci_get_topmost_code(void *ptr, PyCodeObject *code, int lineno)
{
    PyCodeObject **topmost_code = (PyCodeObject **) ptr;
    *topmost_code = code;
    return CI_SWD_STOP_STACK_WALK;
}
#endif

int
PyEval_MergeCompilerFlags(PyCompilerFlags *cf)
{
    PyThreadState *tstate = _PyThreadState_GET();
    int result = cf->cf_flags != 0;

#ifdef ENABLE_CINDERX
    PyCodeObject *cur_code = NULL;
    Ci_WalkStack(tstate, Ci_get_topmost_code, &cur_code);

    if (cur_code != NULL) {
        const int codeflags = cur_code->co_flags;
#else
    PyFrameObject *current_frame = tstate->frame;
    if (current_frame != NULL) {
        const int codeflags = current_frame->f_code->co_flags;
#endif
        const int compilerflags = codeflags & PyCF_MASK;
        if (compilerflags) {
            result = 1;
            cf->cf_flags |= compilerflags;
        }
#if 0 /* future keyword */
        if (codeflags & CO_GENERATOR_ALLOWED) {
            result = 1;
            cf->cf_flags |= CO_GENERATOR_ALLOWED;
        }
#endif
    }
    return result;
}


const char *
PyEval_GetFuncName(PyObject *func)
{
    if (PyMethod_Check(func))
        return PyEval_GetFuncName(PyMethod_GET_FUNCTION(func));
    else if (PyFunction_Check(func))
        return PyUnicode_AsUTF8(((PyFunctionObject*)func)->func_name);
    else if (PyCFunction_Check(func))
        return ((PyCFunctionObject*)func)->m_ml->ml_name;
    else
        return Py_TYPE(func)->tp_name;
}

const char *
PyEval_GetFuncDesc(PyObject *func)
{
    if (PyMethod_Check(func))
        return "()";
    else if (PyFunction_Check(func))
        return "()";
    else if (PyCFunction_Check(func))
        return "()";
    else
        return " object";
}

#define C_TRACE(x, call) \
if (trace_info->cframe.use_tracing && tstate->c_profilefunc) { \
    if (call_trace(tstate->c_profilefunc, tstate->c_profileobj, \
        tstate, tstate->frame, trace_info, \
        PyTrace_C_CALL, func)) { \
        x = NULL; \
    } \
    else { \
        x = call; \
        if (tstate->c_profilefunc != NULL) { \
            if (x == NULL) { \
                call_trace_protected(tstate->c_profilefunc, \
                    tstate->c_profileobj, \
                    tstate, tstate->frame, trace_info, \
                    PyTrace_C_EXCEPTION, func); \
                /* XXX should pass (type, value, tb) */ \
            } else { \
                if (call_trace(tstate->c_profilefunc, \
                    tstate->c_profileobj, \
                    tstate, tstate->frame, trace_info, \
                    PyTrace_C_RETURN, func)) { \
                    Py_DECREF(x); \
                    x = NULL; \
                } \
            } \
        } \
    } \
} else { \
    x = call; \
    }


static PyObject *
trace_call_function(PyThreadState *tstate,
                    PyTraceInfo *trace_info,
                    PyObject *func,
                    PyObject **args, Py_ssize_t nargs,
                    PyObject *kwnames)
{
    PyObject *x;
    if (PyCFunction_CheckExact(func) || PyCMethod_CheckExact(func)) {
        C_TRACE(x, PyObject_Vectorcall(func, args, nargs, kwnames));
        return x;
    }
    else if (Py_IS_TYPE(func, &PyMethodDescr_Type) && nargs > 0) {
        /* We need to create a temporary bound method as argument
           for profiling.

           If nargs == 0, then this cannot work because we have no
           "self". In any case, the call itself would raise
           TypeError (foo needs an argument), so we just skip
           profiling. */
        PyObject *self = args[0];
        func = Py_TYPE(func)->tp_descr_get(func, self, (PyObject*)Py_TYPE(self));
        if (func == NULL) {
            return NULL;
        }
        C_TRACE(x, PyObject_Vectorcall(func,
                                        args+1, nargs-1,
                                        kwnames));
        Py_DECREF(func);
        return x;
    }
    return PyObject_Vectorcall(func, args, nargs | PY_VECTORCALL_ARGUMENTS_OFFSET, kwnames);
}


/* Issue #29227: Inline call_function() into _PyEval_EvalFrameDefault()
   to reduce the stack consumption. */
Py_LOCAL_INLINE(PyObject *) _Py_HOT_FUNCTION
call_function(PyThreadState *tstate,
              PyTraceInfo *trace_info,
              PyObject ***pp_stack,
              Py_ssize_t oparg,
              PyObject *kwnames,
              size_t flags)
{
    PyObject **pfunc = (*pp_stack) - oparg - 1;
    PyObject *func = *pfunc;
    PyObject *x, *w;
    Py_ssize_t nkwargs = (kwnames == NULL) ? 0 : PyTuple_GET_SIZE(kwnames);
    Py_ssize_t nargs = oparg - nkwargs;
    PyObject **stack = (*pp_stack) - nargs - nkwargs;
    flags |= PY_VECTORCALL_ARGUMENTS_OFFSET;
    if (trace_info->cframe.use_tracing) {
        x = trace_call_function(tstate, trace_info, func, stack, nargs, kwnames);
    }
    else {
        x = PyObject_Vectorcall(func, stack, nargs | flags, kwnames);
    }

    assert((x != NULL) ^ (_PyErr_Occurred(tstate) != NULL));

    /* Clear the stack of the function object. */
    while ((*pp_stack) > pfunc) {
        w = EXT_POP(*pp_stack);
        Py_DECREF(w);
    }

    return x;
}

static PyObject *
do_call_core(PyThreadState *tstate,
             PyTraceInfo *trace_info,
             PyObject *func,
             PyObject *callargs,
             PyObject *kwdict,
             int awaited)
{
    PyObject *result;

    if (PyCFunction_CheckExact(func) || PyCMethod_CheckExact(func)) {
        if ((kwdict == NULL || PyDict_GET_SIZE(kwdict) == 0) && ((PyCFunction_GET_FLAGS(func) & METH_VARARGS) == 0)) {
            C_TRACE(result, _PyObject_Vectorcall(
                                func,
                                _PyTuple_ITEMS(callargs),
                                PyTuple_GET_SIZE(callargs) | (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0),
                                NULL));
        }
        else {
            C_TRACE(result, PyObject_Call(func, callargs, kwdict));
        }
        return result;
    }
    else if (Py_IS_TYPE(func, &PyMethodDescr_Type)) {
        Py_ssize_t nargs = PyTuple_GET_SIZE(callargs);
        if (nargs > 0 && trace_info->cframe.use_tracing) {
            /* We need to create a temporary bound method as argument
               for profiling.

               If nargs == 0, then this cannot work because we have no
               "self". In any case, the call itself would raise
               TypeError (foo needs an argument), so we just skip
               profiling. */
            PyObject *self = PyTuple_GET_ITEM(callargs, 0);
            func = Py_TYPE(func)->tp_descr_get(func, self, (PyObject*)Py_TYPE(self));
            if (func == NULL) {
                return NULL;
            }

            C_TRACE(result, _PyObject_FastCallDictTstate(
                                    tstate, func,
                                    &_PyTuple_ITEMS(callargs)[1],
                                    nargs - 1,
                                    kwdict));
            Py_DECREF(func);
            return result;
        }
    }
    if (awaited && _PyVectorcall_Function(func) != NULL) {
        return Ci_PyVectorcall_Call_WithFlags(func, callargs, kwdict, Ci_Py_AWAITED_CALL_MARKER);
    }
    return PyObject_Call(func, callargs, kwdict);
}

#ifdef ENABLE_CINDERX
static inline PyObject *
box_primitive(int type, Py_ssize_t value)
{
    switch (type) {
    case TYPED_BOOL:
        return PyBool_FromLong((int8_t)value);
    case TYPED_INT8:
    case TYPED_CHAR:
        return PyLong_FromSsize_t((int8_t)value);
    case TYPED_INT16:
        return PyLong_FromSsize_t((int16_t)value);
    case TYPED_INT32:
        return PyLong_FromSsize_t((int32_t)value);
    case TYPED_INT64:
        return PyLong_FromSsize_t((int64_t)value);
    case TYPED_UINT8:
        return PyLong_FromSize_t((uint8_t)value);
    case TYPED_UINT16:
        return PyLong_FromSize_t((uint16_t)value);
    case TYPED_UINT32:
        return PyLong_FromSize_t((uint32_t)value);
    case TYPED_UINT64:
        return PyLong_FromSize_t((uint64_t)value);
    default:
        assert(0);
        return NULL;
    }
}

PyObject *_Py_HOT_FUNCTION
_PyFunction_CallStatic(PyFunctionObject *func,
                       PyObject* const* args,
                       Py_ssize_t nargsf,
                       PyObject *kwnames)
{
    assert(PyFunction_Check(func));
    PyCodeObject *co = (PyCodeObject *)func->func_code;

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    assert(nargs == 0 || args != NULL);
    PyFrameConstructor *con = PyFunction_AS_FRAME_CONSTRUCTOR(func);
    PyThreadState *tstate = _PyThreadState_GET();
    assert(tstate != NULL);

    /* We are bound to a specific function that is known at compile time, and
     * all of the arguments are guaranteed to be provided */
    assert(co->co_argcount == nargs);
    assert(co->co_flags & CO_STATICALLY_COMPILED);
    assert(co->co_flags & CO_OPTIMIZED);
    assert(kwnames == NULL);

    // the rest of this is _PyEval_Vector plus skipping CHECK_ARGS

    // we could save some unnecessary checks by copying in
    // and simplifying _PyEval_MakeFrameVector instead of calling it
    PyFrameObject *f = _PyEval_MakeFrameVector(
        tstate, con, NULL, args, nargs, kwnames);
    if (f == NULL) {
        return NULL;
    }
    assert(((unsigned char *)PyBytes_AS_STRING(co->co_code))[0] == CHECK_ARGS);
    f->f_lasti = 0; /* skip CHECK_ARGS */

    Py_ssize_t awaited = Ci_Py_AWAITED_CALL(nargsf);
    if (awaited && (co->co_flags & CO_COROUTINE)) {
        return _PyEval_EvalEagerCoro(tstate, f, func->func_name, func->func_qualname);
    }
    if (co->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)) {
        return make_coro(con, f);
    }
    PyObject *retval = _PyEval_EvalFrame(tstate, f, 0);

    /* decref'ing the frame can cause __del__ methods to get invoked,
       which can call back into Python.  While we're done with the
       current Python frame (f), the associated C stack is still in use,
       so recursion_depth must be boosted for the duration.
    */
    if (Py_REFCNT(f) > 1) {
        Py_DECREF(f);
        _PyObject_GC_TRACK(f);
    }
    else {
        ++tstate->recursion_depth;
        Py_DECREF(f);
        --tstate->recursion_depth;
    }
    return retval;
}

void
PyEntry_initnow(PyFunctionObject *func)
{
    // Check that func hasn't already been initialized.
    assert(func->vectorcall == (vectorcallfunc)PyEntry_LazyInit);
    func->vectorcall = (vectorcallfunc)_PyFunction_Vectorcall;
}

PyObject *
PyEntry_LazyInit(PyFunctionObject *func,
                 PyObject **stack,
                 Py_ssize_t nargsf,
                 PyObject *kwnames)
{
  if (!_PyJIT_IsEnabled() || _PyJIT_CompileFunction(func) != PYJIT_RESULT_OK) {
    PyEntry_initnow(func);
  }
  assert(func->vectorcall != (vectorcallfunc)PyEntry_LazyInit);
  return func->vectorcall((PyObject *)func, stack, nargsf, kwnames);
}

static unsigned int count_calls(PyCodeObject* code) {
  // The interpreter will only increment up to the shadowcode threshold
  // PYSHADOW_INIT_THRESHOLD. After that, it will stop incrementing. If someone
  // sets -X jit-auto above the PYSHADOW_INIT_THRESHOLD, we still have to keep
  // counting.
  unsigned int ncalls = code->co_mutable->ncalls;
  if (ncalls > PYSHADOW_INIT_THRESHOLD) {
    ncalls++;
    code->co_mutable->ncalls = ncalls;
  }
  return ncalls;
}

PyObject*
PyEntry_AutoJIT(PyFunctionObject *func,
                PyObject **stack,
                Py_ssize_t nargsf,
                PyObject *kwnames) {
    PyCodeObject* code = (PyCodeObject*)func->func_code;
    if (count_calls(code) > _PyJIT_AutoJITThreshold()) {
        if (_PyJIT_CompileFunction(func) != PYJIT_RESULT_OK) {
            func->vectorcall = (vectorcallfunc)PyEntry_LazyInit;
            PyEntry_initnow(func);
        }
        assert(func->vectorcall != (vectorcallfunc)PyEntry_AutoJIT);
        return func->vectorcall((PyObject *)func, stack, nargsf, kwnames);
    }
    return _PyFunction_Vectorcall((PyObject *)func, stack, nargsf, kwnames);
}

void
PyEntry_init(PyFunctionObject *func)
{
  assert(!_PyJIT_IsCompiled((PyObject *)func));
  if (_PyJIT_IsAutoJITEnabled()) {
    func->vectorcall = (vectorcallfunc)PyEntry_AutoJIT;
    return;
  }
  func->vectorcall = (vectorcallfunc)PyEntry_LazyInit;
  if (!_PyJIT_RegisterFunction(func)) {
    PyEntry_initnow(func);
  }
}
#endif

/* Extract a slice index from a PyLong or an object with the
   nb_index slot defined, and store in *pi.
   Silently reduce values larger than PY_SSIZE_T_MAX to PY_SSIZE_T_MAX,
   and silently boost values less than PY_SSIZE_T_MIN to PY_SSIZE_T_MIN.
   Return 0 on error, 1 on success.
*/
int
_PyEval_SliceIndex(PyObject *v, Py_ssize_t *pi)
{
    PyThreadState *tstate = _PyThreadState_GET();
    if (!Py_IsNone(v)) {
        Py_ssize_t x;
        if (_PyIndex_Check(v)) {
            x = PyNumber_AsSsize_t(v, NULL);
            if (x == -1 && _PyErr_Occurred(tstate))
                return 0;
        }
        else {
            _PyErr_SetString(tstate, PyExc_TypeError,
                             "slice indices must be integers or "
                             "None or have an __index__ method");
            return 0;
        }
        *pi = x;
    }
    return 1;
}

int
_PyEval_SliceIndexNotNone(PyObject *v, Py_ssize_t *pi)
{
    PyThreadState *tstate = _PyThreadState_GET();
    Py_ssize_t x;
    if (_PyIndex_Check(v)) {
        x = PyNumber_AsSsize_t(v, NULL);
        if (x == -1 && _PyErr_Occurred(tstate))
            return 0;
    }
    else {
        _PyErr_SetString(tstate, PyExc_TypeError,
                         "slice indices must be integers or "
                         "have an __index__ method");
        return 0;
    }
    *pi = x;
    return 1;
}

static int
import_all_from(PyThreadState *tstate, PyObject *locals, PyObject *v)
{
    _Py_IDENTIFIER(__all__);
    _Py_IDENTIFIER(__dict__);
    PyObject *all, *dict, *name, *value;
    int skip_leading_underscores = 0;
    int pos, err;

    if (_PyObject_LookupAttrId(v, &PyId___all__, &all) < 0) {
        return -1; /* Unexpected error */
    }
    if (_PyObject_LookupAttrId(v, &PyId___dict__, &dict) < 0) {
        Py_XDECREF(all);
        return -1; /* Unexpected error */
    }

    if (all == NULL) {
        if (dict == NULL) {
            _PyErr_SetString(tstate, PyExc_ImportError,
                    "from-import-* object has no __dict__ and no __all__");
            return -1;
        }
        all = PyMapping_Keys(dict);
        if (all == NULL) {
            Py_DECREF(dict);
            return -1;
        }
        skip_leading_underscores = 1;
    }

    for (pos = 0, err = 0; ; pos++) {
        name = PySequence_GetItem(all, pos);
        if (name == NULL) {
            if (!_PyErr_ExceptionMatches(tstate, PyExc_IndexError)) {
                err = -1;
            }
            else {
                _PyErr_Clear(tstate);
            }
            break;
        }
        if (!PyUnicode_Check(name)) {
            PyObject *modname = _PyObject_GetAttrId(v, &PyId___name__);
            if (modname == NULL) {
                Py_DECREF(name);
                err = -1;
                break;
            }
            if (!PyUnicode_Check(modname)) {
                _PyErr_Format(tstate, PyExc_TypeError,
                              "module __name__ must be a string, not %.100s",
                              Py_TYPE(modname)->tp_name);
            }
            else {
                _PyErr_Format(tstate, PyExc_TypeError,
                              "%s in %U.%s must be str, not %.100s",
                              skip_leading_underscores ? "Key" : "Item",
                              modname,
                              skip_leading_underscores ? "__dict__" : "__all__",
                              Py_TYPE(name)->tp_name);
            }
            Py_DECREF(modname);
            Py_DECREF(name);
            err = -1;
            break;
        }
        if (skip_leading_underscores) {
            if (PyUnicode_READY(name) == -1) {
                Py_DECREF(name);
                err = -1;
                break;
            }
            if (PyUnicode_READ_CHAR(name, 0) == '_') {
                Py_DECREF(name);
                continue;
            }
        }
        if (PyDict_CheckExact(locals) && dict != NULL && PyDict_CheckExact(dict)) {
            value = _PyDict_GetItemKeepLazy(dict, name);
            if (value != NULL) {
                Py_INCREF(value);
            } else if (!_PyErr_Occurred(tstate)) {
                value = PyObject_GetAttr(v, name);
            }
        } else {
            value = PyObject_GetAttr(v, name);
        }
        if (value == NULL) {
            err = -1;
        } else if (PyDict_CheckExact(locals)) {
            err = PyDict_SetItem(locals, name, value);
        } else {
            err = PyObject_SetItem(locals, name, value);
        }
        Py_DECREF(name);
        Py_XDECREF(value);
        if (err != 0)
            break;
    }
    Py_DECREF(all);
    Py_XDECREF(dict);
    return err;
}

int
check_args_iterable(PyThreadState *tstate, PyObject *func, PyObject *args)
{
    if (Py_TYPE(args)->tp_iter == NULL && !PySequence_Check(args)) {
        /* check_args_iterable() may be called with a live exception:
         * clear it to prevent calling _PyObject_FunctionStr() with an
         * exception set. */
        _PyErr_Clear(tstate);
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            _PyErr_Format(tstate, PyExc_TypeError,
                          "%U argument after * must be an iterable, not %.200s",
                          funcstr, Py_TYPE(args)->tp_name);
            Py_DECREF(funcstr);
        }
        return -1;
    }
    return 0;
}

void
format_kwargs_error(PyThreadState *tstate, PyObject *func, PyObject *kwargs)
{
    /* _PyDict_MergeEx raises attribute
     * error (percolated from an attempt
     * to get 'keys' attribute) instead of
     * a type error if its second argument
     * is not a mapping.
     */
    if (_PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
        _PyErr_Clear(tstate);
        PyObject *funcstr = _PyObject_FunctionStr(func);
        if (funcstr != NULL) {
            _PyErr_Format(
                tstate, PyExc_TypeError,
                "%U argument after ** must be a mapping, not %.200s",
                funcstr, Py_TYPE(kwargs)->tp_name);
            Py_DECREF(funcstr);
        }
    }
    else if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
        PyObject *exc, *val, *tb;
        _PyErr_Fetch(tstate, &exc, &val, &tb);
        if (val && PyTuple_Check(val) && PyTuple_GET_SIZE(val) == 1) {
            _PyErr_Clear(tstate);
            PyObject *funcstr = _PyObject_FunctionStr(func);
            if (funcstr != NULL) {
                PyObject *key = PyTuple_GET_ITEM(val, 0);
                _PyErr_Format(
                    tstate, PyExc_TypeError,
                    "%U got multiple values for keyword argument '%S'",
                    funcstr, key);
                Py_DECREF(funcstr);
            }
            Py_XDECREF(exc);
            Py_XDECREF(val);
            Py_XDECREF(tb);
        }
        else {
            _PyErr_Restore(tstate, exc, val, tb);
        }
    }
}

void
format_exc_check_arg(PyThreadState *tstate, PyObject *exc,
                     const char *format_str, PyObject *obj)
{
    const char *obj_str;

    if (!obj)
        return;

    obj_str = PyUnicode_AsUTF8(obj);
    if (!obj_str)
        return;

    _PyErr_Format(tstate, exc, format_str, obj_str);

    if (exc == PyExc_NameError) {
        // Include the name in the NameError exceptions to offer suggestions later.
        _Py_IDENTIFIER(name);
        PyObject *type, *value, *traceback;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        if (PyErr_GivenExceptionMatches(value, PyExc_NameError)) {
            PyNameErrorObject* exc = (PyNameErrorObject*) value;
            if (exc->name == NULL) {
                // We do not care if this fails because we are going to restore the
                // NameError anyway.
                (void)_PyObject_SetAttrId(value, &PyId_name, obj);
            }
        }
        PyErr_Restore(type, value, traceback);
    }
}

static void
format_exc_unbound(PyThreadState *tstate, PyCodeObject *co, int oparg)
{
    PyObject *name;
    /* Don't stomp existing exception */
    if (_PyErr_Occurred(tstate))
        return;
    if (oparg < PyTuple_GET_SIZE(co->co_cellvars)) {
        name = PyTuple_GET_ITEM(co->co_cellvars,
                                oparg);
        format_exc_check_arg(tstate,
            PyExc_UnboundLocalError,
            UNBOUNDLOCAL_ERROR_MSG,
            name);
    } else {
        name = PyTuple_GET_ITEM(co->co_freevars, oparg -
                                PyTuple_GET_SIZE(co->co_cellvars));
        format_exc_check_arg(tstate, PyExc_NameError,
                             UNBOUNDFREE_ERROR_MSG, name);
    }
}

void
format_awaitable_error(PyThreadState *tstate, PyTypeObject *type, int prevprevopcode, int prevopcode)
{
    if (type->tp_as_async == NULL || type->tp_as_async->am_await == NULL) {
        if (prevopcode == BEFORE_ASYNC_WITH) {
            _PyErr_Format(tstate, PyExc_TypeError,
                          "'async with' received an object from __aenter__ "
                          "that does not implement __await__: %.100s",
                          type->tp_name);
        }
        else if (prevopcode == WITH_EXCEPT_START || (prevopcode == CALL_FUNCTION && prevprevopcode == DUP_TOP)) {
            _PyErr_Format(tstate, PyExc_TypeError,
                          "'async with' received an object from __aexit__ "
                          "that does not implement __await__: %.100s",
                          type->tp_name);
        }
    }
}

static PyObject *
unicode_concatenate(PyThreadState *tstate, PyObject *v, PyObject *w,
                    PyFrameObject *f, const _Py_CODEUNIT *next_instr)
{
    PyObject *res;
    if (Py_REFCNT(v) == 2) {
        /* In the common case, there are 2 references to the value
         * stored in 'variable' when the += is performed: one on the
         * value stack (in 'v') and one still stored in the
         * 'variable'.  We try to delete the variable now to reduce
         * the refcnt to 1.
         */
        int opcode, oparg;
        NEXTOPARG();
        switch (opcode) {
        case STORE_FAST:
        {
            PyObject **fastlocals = f->f_localsplus;
            if (GETLOCAL(oparg) == v)
                SETLOCAL(oparg, NULL);
            break;
        }
        case STORE_DEREF:
        {
            PyObject **freevars = (f->f_localsplus +
                                   f->f_code->co_nlocals);
            PyObject *c = freevars[oparg];
            if (PyCell_GET(c) ==  v) {
                PyCell_SET(c, NULL);
                Py_DECREF(v);
            }
            break;
        }
        case STORE_NAME:
        {
            PyObject *names = f->f_code->co_names;
            PyObject *name = GETITEM(names, oparg);
            PyObject *locals = f->f_locals;
            if (locals && PyDict_CheckExact(locals)) {
                PyObject *w = PyDict_GetItemWithError(locals, name);
                if ((w == v && PyDict_DelItem(locals, name) != 0) ||
                    (w == NULL && _PyErr_Occurred(tstate)))
                {
                    Py_DECREF(v);
                    return NULL;
                }
            }
            break;
        }
        }
    }
    res = v;
    PyUnicode_Append(&res, w);
    return res;
}

#ifdef ENABLE_CINDERX
static inline void try_profile_next_instr(PyFrameObject* f,
                                          PyObject** stack_pointer,
                                          const _Py_CODEUNIT* next_instr) {
    int opcode, oparg;
    NEXTOPARG();
    while (opcode == EXTENDED_ARG) {
        int oldoparg = oparg;
        NEXTOPARG();
        oparg |= oldoparg << 8;
    }

    /* _PyJIT_ProfileCurrentInstr owns the canonical list of which instructions
     * we want to record types for. To save a little work, filter out a few
     * opcodes that we know the JIT will never care about and account for
     * roughly 50% of dynamic bytecodes. */
    switch (opcode) {
        case LOAD_FAST:
        case STORE_FAST:
        case LOAD_CONST:
        case RETURN_VALUE: {
            break;
        }
        default: {
          _PyJIT_ProfileCurrentInstr(f, stack_pointer, opcode, oparg);
          break;
        }
    }
}

static inline PyObject *
load_field(int field_type, void *addr)
{
    PyObject *value;
    switch (field_type) {
    case TYPED_BOOL:
        value = PyBool_FromLong(*(int8_t *)addr);
        break;
    case TYPED_INT8:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int8_t *)addr));
        break;
    case TYPED_INT16:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int16_t *)addr));
        break;
    case TYPED_INT32:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int32_t *)addr));
        break;
    case TYPED_INT64:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int64_t *)addr));
        break;
    case TYPED_UINT8:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint8_t *)addr));
        break;
    case TYPED_UINT16:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint16_t *)addr));
        break;
    case TYPED_UINT32:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint32_t *)addr));
        break;
    case TYPED_UINT64:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint64_t *)addr));
        break;
    case TYPED_DOUBLE:
        value = PyFloat_FromDouble(*(double *)addr);
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "unsupported field type");
        return NULL;
    }
    return value;
}

static inline void
store_field(int field_type, void *addr, PyObject *value)
{
    switch (field_type) {
    case TYPED_BOOL:
        *(int8_t *)addr = (int8_t)unbox_primitive_bool_and_decref(value);
        break;
    case TYPED_INT8:
        *(int8_t *)addr = (int8_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_INT16:
        *(int16_t *)addr = (int16_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_INT32:
        *(int32_t *)addr = (int32_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_INT64:
        *(int64_t *)addr = (int64_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT8:
        *(uint8_t *)addr = (uint8_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT16:
        *(uint16_t *)addr = (uint16_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT32:
        *(uint32_t *)addr = (uint32_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT64:
        *(uint64_t *)addr = (uint64_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_DOUBLE:
        *((double*)addr) = PyFloat_AsDouble(value);
        Py_DECREF(value);
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "unsupported field type");
    }
}
#endif

#ifdef DYNAMIC_EXECUTION_PROFILE

static PyObject *
getarray(long a[256])
{
    int i;
    PyObject *l = PyList_New(256);
    if (l == NULL) return NULL;
    for (i = 0; i < 256; i++) {
        PyObject *x = PyLong_FromLong(a[i]);
        if (x == NULL) {
            Py_DECREF(l);
            return NULL;
        }
        PyList_SET_ITEM(l, i, x);
    }
    for (i = 0; i < 256; i++)
        a[i] = 0;
    return l;
}

PyObject *
_Py_GetDXProfile(PyObject *self, PyObject *args)
{
#ifndef DXPAIRS
    return getarray(dxp);
#else
    int i;
    PyObject *l = PyList_New(257);
    if (l == NULL) return NULL;
    for (i = 0; i < 257; i++) {
        PyObject *x = getarray(dxpairs[i]);
        if (x == NULL) {
            Py_DECREF(l);
            return NULL;
        }
        PyList_SET_ITEM(l, i, x);
    }
    return l;
#endif
}

#endif

Py_ssize_t
_PyEval_RequestCodeExtraIndex(freefunc free)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    Py_ssize_t new_index;

    if (interp->co_extra_user_count == MAX_CO_EXTRA_USERS - 1) {
        return -1;
    }
    new_index = interp->co_extra_user_count++;
    interp->co_extra_freefuncs[new_index] = free;
    return new_index;
}

static void
dtrace_function_entry(PyFrameObject *f)
{
    const char *filename;
    const char *funcname;
    int lineno;

    PyCodeObject *code = f->f_code;
    filename = PyUnicode_AsUTF8(code->co_filename);
    funcname = PyUnicode_AsUTF8(code->co_name);
    lineno = PyFrame_GetLineNumber(f);

    PyDTrace_FUNCTION_ENTRY(filename, funcname, lineno);
}

static void
dtrace_function_return(PyFrameObject *f)
{
    const char *filename;
    const char *funcname;
    int lineno;

    PyCodeObject *code = f->f_code;
    filename = PyUnicode_AsUTF8(code->co_filename);
    funcname = PyUnicode_AsUTF8(code->co_name);
    lineno = PyFrame_GetLineNumber(f);

    PyDTrace_FUNCTION_RETURN(filename, funcname, lineno);
}

/* DTrace equivalent of maybe_call_line_trace. */
static void
maybe_dtrace_line(PyFrameObject *frame,
                  PyTraceInfo *trace_info, int instr_prev)
{
    const char *co_filename, *co_name;

    /* If the last instruction executed isn't in the current
       instruction window, reset the window.
    */
    initialize_trace_info(trace_info, frame);
    int line = _PyCode_CheckLineNumber(frame->f_lasti*sizeof(_Py_CODEUNIT), &trace_info->bounds);
    /* If the last instruction falls at the start of a line or if
       it represents a jump backwards, update the frame's line
       number and call the trace function. */
    if (line != frame->f_lineno || frame->f_lasti < instr_prev) {
        if (line != -1) {
            frame->f_lineno = line;
            co_filename = PyUnicode_AsUTF8(frame->f_code->co_filename);
            if (!co_filename)
                co_filename = "?";
            co_name = PyUnicode_AsUTF8(frame->f_code->co_name);
            if (!co_name)
                co_name = "?";
            PyDTrace_LINE(co_filename, co_name, line);
        }
    }
}


/* Implement Py_EnterRecursiveCall() and Py_LeaveRecursiveCall() as functions
   for the limited API. */

#undef Py_EnterRecursiveCall

int Py_EnterRecursiveCall(const char *where)
{
    return _Py_EnterRecursiveCall_inline(where);
}

#undef Py_LeaveRecursiveCall

void Py_LeaveRecursiveCall(void)
{
    _Py_LeaveRecursiveCall_inline();
}
