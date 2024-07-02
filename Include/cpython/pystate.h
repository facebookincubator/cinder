#ifndef Py_CPYTHON_PYSTATE_H
#  error "this header file must not be included directly"
#endif


/* private interpreter helpers */

PyAPI_FUNC(int) _PyInterpreterState_RequiresIDRef(PyInterpreterState *);
PyAPI_FUNC(void) _PyInterpreterState_RequireIDRef(PyInterpreterState *, int);

PyAPI_FUNC(PyObject *) PyUnstable_InterpreterState_GetMainModule(PyInterpreterState *);

/* State unique per thread */

/* Py_tracefunc return -1 when raising an exception, or 0 for success. */
typedef int (*Py_tracefunc)(PyObject *, PyFrameObject *, int, PyObject *);

/* The following values are used for 'what' for tracefunc functions
 *
 * To add a new kind of trace event, also update "trace_init" in
 * Python/sysmodule.c to define the Python level event name
 */
#define PyTrace_CALL 0
#define PyTrace_EXCEPTION 1
#define PyTrace_LINE 2
#define PyTrace_RETURN 3
#define PyTrace_C_CALL 4
#define PyTrace_C_EXCEPTION 5
#define PyTrace_C_RETURN 6
#define PyTrace_OPCODE 7

typedef struct _err_stackitem {
    /* This struct represents a single execution context where we might
     * be currently handling an exception.  It is a per-coroutine state
     * (coroutine in the computer science sense, including the thread
     * and generators).
     *
     * This is used as an entry on the exception stack, where each
     * entry indicates if it is currently handling an exception.
     * This ensures that the exception state is not impacted
     * by "yields" from an except handler.  The thread
     * always has an entry (the bottom-most one).
     */

    /* The exception currently being handled in this context, if any. */
    PyObject *exc_value;

    struct _err_stackitem *previous_item;

} _PyErr_StackItem;

typedef struct _stack_chunk {
    struct _stack_chunk *previous;
    size_t size;
    size_t top;
    PyObject * data[1]; /* Variable sized */
} _PyStackChunk;

struct _ts {
    /* See Python/ceval.c for comments explaining most fields */

    PyThreadState *prev;
    PyThreadState *next;
    PyInterpreterState *interp;

    /* The global instrumentation version in high bits, plus flags indicating
       when to break out of the interpreter loop in lower bits. See details in
       pycore_ceval.h. */
    uintptr_t eval_breaker;

    struct {
        /* Has been initialized to a safe state.

           In order to be effective, this must be set to 0 during or right
           after allocation. */
        unsigned int initialized:1;

        /* Has been bound to an OS thread. */
        unsigned int bound:1;
        /* Has been unbound from its OS thread. */
        unsigned int unbound:1;
        /* Has been bound aa current for the GILState API. */
        unsigned int bound_gilstate:1;
        /* Currently in use (maybe holds the GIL). */
        unsigned int active:1;
        /* Currently holds the GIL. */
        unsigned int holds_gil:1;

        /* various stages of finalization */
        unsigned int finalizing:1;
        unsigned int cleared:1;
        unsigned int finalized:1;

        /* padding to align to 4 bytes */
        unsigned int :23;
    } _status;
#ifdef Py_BUILD_CORE
#  define _PyThreadState_WHENCE_NOTSET -1
#  define _PyThreadState_WHENCE_UNKNOWN 0
#  define _PyThreadState_WHENCE_INIT 1
#  define _PyThreadState_WHENCE_FINI 2
#  define _PyThreadState_WHENCE_THREADING 3
#  define _PyThreadState_WHENCE_GILSTATE 4
#  define _PyThreadState_WHENCE_EXEC 5
#endif
    int _whence;

    /* Thread state (_Py_THREAD_ATTACHED, _Py_THREAD_DETACHED, _Py_THREAD_SUSPENDED).
       See Include/internal/pycore_pystate.h for more details. */
    int state;

    int py_recursion_remaining;
    int py_recursion_limit;

    int c_recursion_remaining;
    int recursion_headroom; /* Allow 50 more calls to handle any errors. */

    /* 'tracing' keeps track of the execution depth when tracing/profiling.
       This is to prevent the actual trace/profile code from being recorded in
       the trace/profile. */
    int tracing;
    int what_event; /* The event currently being monitored, if any. */

    /* Pointer to currently executing frame. */
    struct _PyInterpreterFrame *current_frame;

    Py_tracefunc c_profilefunc;
    Py_tracefunc c_tracefunc;
    PyObject *c_profileobj;
    PyObject *c_traceobj;

    /* The exception currently being raised */
    PyObject *current_exception;

    /* Pointer to the top of the exception stack for the exceptions
     * we may be currently handling.  (See _PyErr_StackItem above.)
     * This is never NULL. */
    _PyErr_StackItem *exc_info;

    PyObject *dict;  /* Stores per-thread state */

    int gilstate_counter;

    PyObject *async_exc; /* Asynchronous exception to raise */
    unsigned long thread_id; /* Thread id where this tstate was created */

    /* Native thread id where this tstate was created. This will be 0 except on
     * those platforms that have the notion of native thread id, for which the
     * macro PY_HAVE_THREAD_NATIVE_ID is then defined.
     */
    unsigned long native_thread_id;

    PyObject *delete_later;

    /* Tagged pointer to top-most critical section, or zero if there is no
     * active critical section. Critical sections are only used in
     * `--disable-gil` builds (i.e., when Py_GIL_DISABLED is defined to 1). In the
     * default build, this field is always zero.
     */
    uintptr_t critical_section;

    int coroutine_origin_tracking_depth;

    PyObject *async_gen_firstiter;
    PyObject *async_gen_finalizer;

    PyObject *context;
    uint64_t context_ver;

    /* Unique thread state id. */
    uint64_t id;

    _PyStackChunk *datastack_chunk;
    PyObject **datastack_top;
    PyObject **datastack_limit;
    /* XXX signal handlers should also be here */

    /* The following fields are here to avoid allocation during init.
       The data is exposed through PyThreadState pointer fields.
       These fields should not be accessed directly outside of init.
       This is indicated by an underscore prefix on the field names.

       All other PyInterpreterState pointer fields are populated when
       needed and default to NULL.
       */
       // Note some fields do not have a leading underscore for backward
       // compatibility.  See https://bugs.python.org/issue45953#msg412046.

    /* The thread's exception stack entry.  (Always the last entry.) */
    _PyErr_StackItem exc_state;

    PyObject *previous_executor;

    uint64_t dict_global_version;
};

#ifdef Py_DEBUG
   // A debug build is likely built with low optimization level which implies
   // higher stack memory usage than a release build: use a lower limit.
#  define Py_C_RECURSION_LIMIT 500
#elif defined(__s390x__)
#  define Py_C_RECURSION_LIMIT 800
#elif defined(_WIN32) && defined(_M_ARM64)
#  define Py_C_RECURSION_LIMIT 1000
#elif defined(_WIN32)
#  define Py_C_RECURSION_LIMIT 3000
#elif defined(__ANDROID__)
   // On an ARM64 emulator, API level 34 was OK with 10000, but API level 21
   // crashed in test_compiler_recursion_limit.
#  define Py_C_RECURSION_LIMIT 3000
#elif defined(_Py_ADDRESS_SANITIZER)
#  define Py_C_RECURSION_LIMIT 4000
#elif defined(__wasi__)
   // Based on wasmtime 16.
#  define Py_C_RECURSION_LIMIT 5000
#else
   // This value is duplicated in Lib/test/support/__init__.py
#  define Py_C_RECURSION_LIMIT 10000
#endif


/* other API */

/* Similar to PyThreadState_Get(), but don't issue a fatal error
 * if it is NULL. */
PyAPI_FUNC(PyThreadState *) PyThreadState_GetUnchecked(void);

// Alias kept for backward compatibility
#define _PyThreadState_UncheckedGet PyThreadState_GetUnchecked


// Disable tracing and profiling.
PyAPI_FUNC(void) PyThreadState_EnterTracing(PyThreadState *tstate);

// Reset tracing and profiling: enable them if a trace function or a profile
// function is set, otherwise disable them.
PyAPI_FUNC(void) PyThreadState_LeaveTracing(PyThreadState *tstate);

/* PyGILState */

/* Helper/diagnostic function - return 1 if the current thread
   currently holds the GIL, 0 otherwise.

   The function returns 1 if _PyGILState_check_enabled is non-zero. */
PyAPI_FUNC(int) PyGILState_Check(void);

/* The implementation of sys._current_frames()  Returns a dict mapping
   thread id to that thread's current frame.
*/
PyAPI_FUNC(PyObject*) _PyThread_CurrentFrames(void);

/* Routines for advanced debuggers, requested by David Beazley.
   Don't use unless you know what you are doing! */
PyAPI_FUNC(PyInterpreterState *) PyInterpreterState_Main(void);
PyAPI_FUNC(PyInterpreterState *) PyInterpreterState_Head(void);
PyAPI_FUNC(PyInterpreterState *) PyInterpreterState_Next(PyInterpreterState *);
PyAPI_FUNC(PyThreadState *) PyInterpreterState_ThreadHead(PyInterpreterState *);
PyAPI_FUNC(PyThreadState *) PyThreadState_Next(PyThreadState *);
PyAPI_FUNC(void) PyThreadState_DeleteCurrent(void);

/* Frame evaluation API */

typedef PyObject* (*_PyFrameEvalFunction)(PyThreadState *tstate, struct _PyInterpreterFrame *, int);

PyAPI_FUNC(_PyFrameEvalFunction) _PyInterpreterState_GetEvalFrameFunc(
    PyInterpreterState *interp);
PyAPI_FUNC(void) _PyInterpreterState_SetEvalFrameFunc(
    PyInterpreterState *interp,
    _PyFrameEvalFunction eval_frame);
