
/* Generator object interface */

#ifndef Py_LIMITED_API
#ifndef Py_GENOBJECT_H
#define Py_GENOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "pystate.h"   /* _PyErr_StackItem */

#include "internal/pycore_shadow_frame_struct.h"

#include "cinder/ci_api.h"


/* Opaque type used by JIT internals. For more details see comments around
   GenDataFooter in the JIT. */
struct Ci_JITGenData;

/* _PyGenObject_HEAD defines the initial segment of generator
   and coroutine objects. */
#define _PyGenObject_HEAD(prefix)                                           \
    PyObject_HEAD                                                           \
    /* Note: gi_frame can be NULL if the generator is "finished" */         \
    PyFrameObject *prefix##_frame;                                          \
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
       generator should immediately be treated as non-JIT-backed. */        \
    struct Ci_JITGenData *prefix##_jit_data;                                \
    _PyShadowFrame prefix##_shadow_frame;

typedef struct {
    /* The gi_ prefix is intended to remind of generator-iterator. */
    _PyGenObject_HEAD(gi)
} PyGenObject;

PyAPI_DATA(PyTypeObject) PyGen_Type;

#define PyGen_Check(op) PyObject_TypeCheck(op, &PyGen_Type)
#define PyGen_CheckExact(op) Py_IS_TYPE(op, &PyGen_Type)

PyAPI_FUNC(PyObject *) PyGen_New(PyFrameObject *);
PyAPI_FUNC(PyObject *) PyGen_NewWithQualName(PyFrameObject *,
    PyObject *name, PyObject *qualname);
PyAPI_FUNC(int) _PyGen_SetStopIterationValue(PyObject *);
PyAPI_FUNC(int) _PyGen_FetchStopIterationValue(PyObject **);
CiAPI_FUNC(PyObject *) _PyGen_yf(PyGenObject *);
PyAPI_FUNC(void) _PyGen_Finalize(PyObject *self);

CiAPI_FUNC(int) Ci_PyGen_IsSuspended(PyGenObject *self);
CiAPI_FUNC(void) Ci_PyGen_MarkJustStartedGenAsCompleted(PyGenObject *gen);

typedef struct {
    PyObject_HEAD
    PyObject *wh_coro_or_result;
    PyObject *wh_waiter;
} Ci_PyWaitHandleObject;

CiAPI_DATA(PyTypeObject) Ci_PyWaitHandle_Type;

#define Ci_PyWaitHandle_CheckExact(op) (Py_TYPE(op) == &Ci_PyWaitHandle_Type)

CiAPI_FUNC(PyObject *) Ci_PyWaitHandle_New(PyObject *coro_or_result, PyObject *waiter);
CiAPI_FUNC(void) Ci_PyWaitHandle_Release(PyObject *wait_handle);

#ifndef Py_LIMITED_API
typedef struct _coro {
    _PyGenObject_HEAD(cr)
    PyObject *cr_origin;
    struct _coro *ci_cr_awaiter;
} PyCoroObject;

PyAPI_DATA(PyTypeObject) PyCoro_Type;
PyAPI_DATA(PyTypeObject) _PyCoroWrapper_Type;

PyAPI_DATA(int) CiGen_FreeListEnabled;

#define PyCoro_CheckExact(op) Py_IS_TYPE(op, &PyCoro_Type)
CiAPI_FUNC(PyObject *) _PyCoro_GetAwaitableIter(PyObject *o);
PyAPI_FUNC(PyObject *) PyCoro_New(PyFrameObject *,
    PyObject *name, PyObject *qualname);

static inline void _PyAwaitable_SetAwaiter(PyObject *receiver, PyObject *awaiter) {
    PyTypeObject *ty = Py_TYPE(receiver);
    if (!PyType_HasFeature(ty, Py_TPFLAGS_HAVE_AM_EXTRA)) {
        return;
    }
    PyAsyncMethodsWithExtra *ame = (PyAsyncMethodsWithExtra *)ty->tp_as_async;
    assert(ame != NULL);
    if (ame->ame_setawaiter != NULL) {
        ame->ame_setawaiter(receiver, awaiter);
    }
}

CiAPI_FUNC(PyObject *) _PyCoro_ForFrame(
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

PyAPI_FUNC(PyObject *) PyAsyncGen_New(PyFrameObject *,
    PyObject *name, PyObject *qualname);

#define PyAsyncGen_CheckExact(op) Py_IS_TYPE(op, &PyAsyncGen_Type)

CiAPI_FUNC(PyObject *) _PyAsyncGenValueWrapperNew(PyObject *);

PyAPI_FUNC(int) CiGen_ClearFreeList(void);

#endif

#undef _PyGenObject_HEAD

#ifdef __cplusplus
}
#endif
#endif /* !Py_GENOBJECT_H */
#endif /* Py_LIMITED_API */
