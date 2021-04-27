// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/runtime_support.h"

#include "Python.h"
#include "internal/pycore_ceval.h"
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"

extern "C" {
void take_gil(struct _ceval_runtime_state*, PyThreadState*);
void drop_gil(struct _ceval_runtime_state*, PyThreadState*);
void exit_thread_if_finalizing(_PyRuntimeState* runtime, PyThreadState* tstate);
int make_pending_calls(_PyRuntimeState* runtime);
int handle_signals(_PyRuntimeState* runtime);
}

namespace jit {

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

// This duplicates the logic found at the beginning of the dispatch loop in
// _PyEval_EvalFrameDefault
PyObject* runPeriodicTasks() {
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
}

} // namespace jit
