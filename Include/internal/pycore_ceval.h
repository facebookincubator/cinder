#ifndef Py_INTERNAL_CEVAL_H
#define Py_INTERNAL_CEVAL_H

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pycore_atomic.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "pycore_pystate.h"
#include "pythread.h"

/* facebook begin T57511654 */
PyAPI_DATA(int64_t) __strobe_PyObject_type;
PyAPI_DATA(int64_t) __strobe_PyTypeObject_name;
PyAPI_DATA(int64_t) __strobe_PyThreadState_frame;
PyAPI_DATA(int64_t) __strobe_PyThreadState_thread;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_back;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_code;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_lineno;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_localsplus;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_gen;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_co_flags;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_filename;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_name;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_varnames;
PyAPI_DATA(int64_t) __strobe_PyTupleObject_item;
PyAPI_DATA(int64_t) __strobe_PyCoroObject_creator;
PyAPI_DATA(int64_t) __strobe_String_data;
PyAPI_DATA(int64_t) __strobe_String_size;
PyAPI_DATA(int64_t) __strobe_TLSKey_offset;
PyAPI_DATA(int64_t) __strobe_TCurrentState_offset;
PyAPI_DATA(int32_t) __strobe_PyVersion_major;
PyAPI_DATA(int32_t) __strobe_PyVersion_minor;
PyAPI_DATA(int32_t) __strobe_PyVersion_micro;
/* facebook end T57511654 */

PyAPI_FUNC(void) _Py_FinishPendingCalls(_PyRuntimeState *runtime);
PyAPI_FUNC(void) _PyEval_Initialize(struct _ceval_runtime_state *);
PyAPI_FUNC(void) _PyEval_FiniThreads(
    struct _ceval_runtime_state *ceval);
PyAPI_FUNC(void) _PyEval_SignalReceived(
    struct _ceval_runtime_state *ceval);
PyAPI_FUNC(int) _PyEval_AddPendingCall(
    PyThreadState *tstate,
    struct _ceval_runtime_state *ceval,
    int (*func)(void *),
    void *arg);
PyAPI_FUNC(void) _PyEval_SignalAsyncExc(
    struct _ceval_runtime_state *ceval);
PyAPI_FUNC(void) _PyEval_ReInitThreads(
    _PyRuntimeState *runtime);

#define GIL_REQUEST _Py_atomic_load_relaxed(&ceval->gil_drop_request)

/* This can set eval_breaker to 0 even though gil_drop_request became
   1.  We believe this is all right because the eval loop will release
   the GIL eventually anyway. */
#define COMPUTE_EVAL_BREAKER(ceval) \
    _Py_atomic_store_relaxed( \
        &(ceval)->eval_breaker, \
        GIL_REQUEST | \
        _Py_atomic_load_relaxed(&(ceval)->signals_pending) | \
        _Py_atomic_load_relaxed(&(ceval)->pending.calls_to_do) | \
        (ceval)->pending.async_exc)

#define SET_GIL_DROP_REQUEST(ceval) \
    do { \
        _Py_atomic_store_relaxed(&(ceval)->gil_drop_request, 1); \
        _Py_atomic_store_relaxed(&(ceval)->eval_breaker, 1); \
    } while (0)

#define RESET_GIL_DROP_REQUEST(ceval) \
    do { \
        _Py_atomic_store_relaxed(&(ceval)->gil_drop_request, 0); \
        COMPUTE_EVAL_BREAKER(ceval); \
    } while (0)

/* Pending calls are only modified under pending_lock */
#define SIGNAL_PENDING_CALLS(ceval) \
    do { \
        _Py_atomic_store_relaxed(&(ceval)->pending.calls_to_do, 1); \
        _Py_atomic_store_relaxed(&(ceval)->eval_breaker, 1); \
    } while (0)

#define UNSIGNAL_PENDING_CALLS(ceval) \
    do { \
        _Py_atomic_store_relaxed(&(ceval)->pending.calls_to_do, 0); \
        COMPUTE_EVAL_BREAKER(ceval); \
    } while (0)

#define SIGNAL_PENDING_SIGNALS(ceval) \
    do { \
        _Py_atomic_store_relaxed(&(ceval)->signals_pending, 1); \
        _Py_atomic_store_relaxed(&(ceval)->eval_breaker, 1); \
    } while (0)

#define UNSIGNAL_PENDING_SIGNALS(ceval) \
    do { \
        _Py_atomic_store_relaxed(&(ceval)->signals_pending, 0); \
        COMPUTE_EVAL_BREAKER(ceval); \
    } while (0)

#define SIGNAL_ASYNC_EXC(ceval) \
    do { \
        (ceval)->pending.async_exc = 1; \
        _Py_atomic_store_relaxed(&(ceval)->eval_breaker, 1); \
    } while (0)

#define UNSIGNAL_ASYNC_EXC(ceval) \
    do { \
        (ceval)->pending.async_exc = 0; \
        COMPUTE_EVAL_BREAKER(ceval); \
    } while (0)

/* Private function */
void _PyEval_Fini(void);

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_CEVAL_H */
