
/* Generator object interface */

#ifndef Py_LIMITED_API
#ifndef Py_GENOBJECT_H
#define Py_GENOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "pystate.h"   /* _PyErr_StackItem */

#include "pycore_shadow_frame_struct.h"

struct _frame; /* Avoid including frameobject.h */

/* Opaque type used by JIT internals. For more details see comments around
   GenDataFooter in the JIT. */
struct _PyJIT_GenData;

typedef enum {
    /* Generator has freshly been returned from a call to the function itself.
       Execution of user code has not yet begun. */
    _PyJitGenState_JustStarted,
    /* Execution is in progress and is currently active or the generator is
       suspended. */
    _PyJitGenState_Running,
    /* Generator has completed execution and should not be resumed again. */
    _PyJitGenState_Completed,
} _PyJitGenState;

/* _PyGenObject_HEAD defines the initial segment of generator
   and coroutine objects. */
#define _PyGenObject_HEAD(prefix)                                           \
    PyObject_HEAD                                                           \
    /* Note: gi_frame can be NULL if the generator is "finished" */         \
    struct _frame *prefix##_frame;                                          \
    _PyShadowFrame prefix##_shadow_frame;                                   \
    /* True if generator is being executed. */                              \
    char prefix##_running;                                                  \
    /* The code object backing the generator */                             \
    PyObject *prefix##_code;                                                \
    /* List of weak reference. */                                           \
    PyObject *prefix##_weakreflist;                                         \
    /* Name of the generator. */                                            \
    PyObject *prefix##_name;                                                \
    /* Qualified name of the generator. */                                  \
    PyObject *prefix##_qualname;                                            \
    _PyErr_StackItem prefix##_exc_state;                                    \
    /* Opaque JIT related data. If this is NULL then this generator is not JIT-\
       backed. A deopt may cause this value to change to NULL in which case the\
       generator should immediately be treated as non-JIT-backed. */           \
    struct _PyJIT_GenData *prefix##_jit_data;

typedef struct {
    /* The gi_ prefix is intended to remind of generator-iterator. */
    _PyGenObject_HEAD(gi)
} PyGenObject;

PyAPI_DATA(PyTypeObject) PyGen_Type;

#define PyGen_Check(op) PyObject_TypeCheck(op, &PyGen_Type)
#define PyGen_CheckExact(op) (Py_TYPE(op) == &PyGen_Type)

PyAPI_FUNC(PyObject *) _PyGen_NewNoFrame(PyCodeObject *code);
PyAPI_FUNC(PyObject *) PyGen_New(struct _frame *);
PyAPI_FUNC(PyObject *) PyGen_NewWithQualName(struct _frame *,
    PyObject *name, PyObject *qualname);
PyAPI_FUNC(int) PyGen_NeedsFinalizing(PyGenObject *);
PyAPI_FUNC(int) _PyGen_SetStopIterationValue(PyObject *);
PyAPI_FUNC(int) _PyGen_FetchStopIterationValue(PyObject **);
PyAPI_FUNC(PyObject *) _PyGen_Send(PyGenObject *, PyObject *);
PyAPI_FUNC(PyObject *) _PyGen_Send_NoStopIteration(PyThreadState *tstate,
                                                   PyGenObject *,
                                                   PyObject *,
                                                   PyObject **);
int _PyGen_close_yf(PyObject *yf);

int _PyGen_restore_error(PyObject *et, PyObject *ev, PyObject *tb);

PyObject *_PyGen_yf(PyGenObject *);
PyAPI_FUNC(void) _PyGen_Finalize(PyObject *self);

PyAPI_FUNC(int) _PyGen_IsSuspended(PyGenObject *self);

typedef struct {
    PyObject_HEAD
    PyObject *wh_coro_or_result;
    PyObject *wh_waiter;
} PyWaitHandleObject;

PyAPI_DATA(PyTypeObject) PyWaitHandle_Type;

#define _PyWaitHandle_CheckExact(op) (Py_TYPE(op) == &PyWaitHandle_Type)

PyAPI_FUNC(PyObject *)
    _PyWaitHandle_New(PyObject *coro_or_result, PyObject *waiter);
PyAPI_FUNC(void) _PyWaitHandle_Release(PyObject *wait_handle);

#ifndef Py_LIMITED_API
typedef struct {
    _PyGenObject_HEAD(cr)
    PyObject *cr_origin;
    struct _frame * creator;
} PyCoroObject;

PyAPI_DATA(PyTypeObject) PyCoro_Type;
PyAPI_DATA(PyTypeObject) _PyCoroWrapper_Type;

PyAPI_DATA(PyTypeObject) _PyAIterWrapper_Type;

extern int _PyGen_FreeListEnabled;

#define PyCoro_CheckExact(op) (Py_TYPE(op) == &PyCoro_Type)
PyObject *_PyCoro_GetAwaitableIter(PyObject *o);
PyAPI_FUNC(PyObject *) _PyCoro_NewNoFrame(
    PyThreadState *tstate, PyCodeObject *code);
PyAPI_FUNC(PyObject *) PyCoro_New(struct _frame *,
    PyObject *name, PyObject *qualname);
PyAPI_FUNC(PyObject *) _PyCoro_NewTstate(
    PyThreadState *tstate,
    struct _frame *,
    PyObject *name,
    PyObject *qualname);

PyAPI_FUNC(PyObject *) _PyCoro_ForFrame(
    PyThreadState *tstate,
    struct _frame *,
    PyObject *name,
    PyObject *qualname);

/* Asynchronous Generators */

typedef struct {
    _PyGenObject_HEAD(ag)
    PyObject *ag_finalizer;

    /* Flag is set to 1 when hooks set up by sys.set_asyncgen_hooks
       were called on the generator, to avoid calling them more
       than once. */
    int ag_hooks_inited;

    /* Flag is set to 1 when aclose() is called for the first time, or
       when a StopAsyncIteration exception is raised. */
    int ag_closed;

    int ag_running_async;
} PyAsyncGenObject;

PyAPI_DATA(PyTypeObject) PyAsyncGen_Type;
PyAPI_DATA(PyTypeObject) _PyAsyncGenASend_Type;
PyAPI_DATA(PyTypeObject) _PyAsyncGenWrappedValue_Type;
PyAPI_DATA(PyTypeObject) _PyAsyncGenAThrow_Type;

PyAPI_FUNC(PyObject *) _PyAsyncGen_NewNoFrame(PyCodeObject *code);
PyAPI_FUNC(PyObject *) PyAsyncGen_New(struct _frame *,
    PyObject *name, PyObject *qualname);

#define PyAsyncGen_CheckExact(op) (Py_TYPE(op) == &PyAsyncGen_Type)

PyObject *_PyAsyncGenValueWrapperNew(PyObject *);

int PyAsyncGen_ClearFreeLists(void);

int _PyGen_ClearFreeList(void);

#endif

#undef _PyGenObject_HEAD

#ifdef __cplusplus
}
#endif
#endif /* !Py_GENOBJECT_H */
#endif /* Py_LIMITED_API */
