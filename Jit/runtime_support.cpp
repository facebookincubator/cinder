// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/runtime_support.h"

#include "Python.h"
#include "internal/pycore_ceval.h"
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"
#include "pyreadonly.h"

#include "Jit/log.h"

#include "cinder/port-assert.h"

extern "C" {
void take_gil(struct _ceval_runtime_state*, PyThreadState*);
void drop_gil(struct _ceval_runtime_state*, PyThreadState*);
void exit_thread_if_finalizing(_PyRuntimeState* runtime, PyThreadState* tstate);
int make_pending_calls(_PyRuntimeState* runtime);
int handle_signals(_PyRuntimeState* runtime);
}

namespace jit {

// TODO(T125857223): Use the external immortal refcount kImmortalInitialCount
// instead of this local copy.
static const Py_ssize_t kImmortalBitPos = 8 * sizeof(Py_ssize_t) - 4;
static const Py_ssize_t kImmortalBit = 1L << kImmortalBitPos;
static const Py_ssize_t kImmortalInitialCount = kImmortalBit;
PyObject g_iterDoneSentinel = {
    _PyObject_EXTRA_INIT kImmortalInitialCount,
    nullptr};

PyObject* invokeIterNext(PyObject* iterator) {
  PyObject* val = (*iterator->ob_type->tp_iternext)(iterator);
  if (val != nullptr) {
    return val;
  }
  if (PyErr_Occurred()) {
    if (!PyErr_ExceptionMatches(PyExc_StopIteration)) {
      return nullptr;
    }
    PyErr_Clear();
  }
  Py_INCREF(&g_iterDoneSentinel);
  return &g_iterDoneSentinel;
}

PyObject* invokeIterNextReadonly(PyObject* iterator, int readonly_mask) {
  if (readonly_mask && PyReadonly_BeginReadonlyOperation(readonly_mask) != 0) {
    return nullptr;
  }
  PyObject* val = (*iterator->ob_type->tp_iternext)(iterator);
  if (readonly_mask && PyReadonly_CheckReadonlyOperation(0, 0) != 0) {
    return nullptr;
  }
  if (readonly_mask && PyReadonly_VerifyReadonlyOperationCompleted() != 0) {
    return nullptr;
  }
  if (val != nullptr) {
    return val;
  }
  if (PyErr_Occurred()) {
    if (!PyErr_ExceptionMatches(PyExc_StopIteration)) {
      return nullptr;
    }
    PyErr_Clear();
  }
  Py_INCREF(&g_iterDoneSentinel);
  return &g_iterDoneSentinel;
}

// This duplicates the logic found at the beginning of the dispatch loop in
// _PyEval_EvalFrameDefault
PyObject* runPeriodicTasks() {
#ifdef CINDER_PORTING_DONE
  PyThreadState* tstate = PyThreadState_GET();
  _PyRuntimeState* const runtime = &_PyRuntime;
  struct _ceval_runtime_state* const ceval = &_PyRuntime.ceval;

  if (_Py_atomic_load_relaxed(&ceval->signals_pending)) {
    if (handle_signals(runtime) != 0) {
      return nullptr;
    }
  }

  if (_Py_atomic_load_relaxed(&ceval->pending.calls_to_do)) {
    if (make_pending_calls(runtime) != 0) {
      return nullptr;
    }
  }

  if (_Py_atomic_load_relaxed(&ceval->gil_drop_request)) {
    /* Give another thread a chance */
    if (_PyThreadState_Swap(&runtime->gilstate, NULL) != tstate) {
      Py_FatalError("ceval: tstate mix-up");
    }
    drop_gil(ceval, tstate);

    /* Other threads may run now */

    take_gil(ceval, tstate);

    /* Check if we should make a quick exit. */
    if (runtime->finalizing != NULL &&
        !_Py_CURRENTLY_FINALIZING(runtime, tstate)) {
      drop_gil(&runtime->ceval, tstate);
      PyThread_exit_thread();
    }

    if (_PyThreadState_Swap(&runtime->gilstate, tstate) != NULL) {
      Py_FatalError("ceval: orphan tstate");
    }
  }

  /* Check for asynchronous exceptions. */
  if (tstate->async_exc != nullptr) {
    PyObject* exc = tstate->async_exc;
    tstate->async_exc = nullptr;
    UNSIGNAL_ASYNC_EXC(ceval);
    _PyErr_SetNone(tstate, exc);
    Py_DECREF(exc);
    return nullptr;
  }

  return Py_True;
#else
  PORT_ASSERT("Several changes to where interpreter state is stored");
  // Looks like this can be replaced with ceval.c: eval_frame_handle_pending()
#endif
}

} // namespace jit
