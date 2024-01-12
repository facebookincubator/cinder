#pragma once

#include <Python.h>
#include <frameobject.h>

/* Enum shared with the JIT to communicate the current state of a generator.
 * These should be queried via the utility functions below. These may use some
 * other features to determine the overall state of a JIT generator. Notably
 * the yield-point field being null indicates execution is currently active when
 * used in combination with the less specific Ci_JITGenState_Running state.
 */
typedef enum {
  /* Generator has freshly been returned from a call to the function itself.
     Execution of user code has not yet begun. */
  Ci_JITGenState_JustStarted,
  /* Execution is in progress and is currently active or the generator is
     suspended. */
  Ci_JITGenState_Running,
  /* Generator has completed execution and should not be resumed again. */
  Ci_JITGenState_Completed,
  /* An exception/close request is being processed.  */
  Ci_JITGenState_Throwing,
} CiJITGenState;

/* Offsets for fields in jit::GenFooterData for fast access from C code.
 * These values are verified by static_assert in runtime.h. */
#define Ci_GEN_JIT_DATA_OFFSET_STATE 32
#define Ci_GEN_JIT_DATA_OFFSET_YIELD_POINT 24

/* Functions for extracting the state for generator objects known to be JIT
 * controlled. Implemented as inline functions using hard-coded offsets for
 * speed in C code which doesn't have access to C++ types.
 */

static inline CiJITGenState Ci_GetJITGenState(PyGenObject *gen) {
  return (
      (CiJITGenState)((char *)gen->gi_jit_data)[Ci_GEN_JIT_DATA_OFFSET_STATE]);
}

static inline int Ci_JITGenIsExecuting(PyGenObject *gen) {
  return (Ci_GetJITGenState(gen) == Ci_JITGenState_Running &&
          !(*(uint64_t *)((uintptr_t)gen->gi_jit_data +
                          Ci_GEN_JIT_DATA_OFFSET_YIELD_POINT))) ||
         Ci_GetJITGenState(gen) == Ci_JITGenState_Throwing;
}

static inline int Ci_JITGenIsRunnable(PyGenObject *gen) {
  return Ci_GetJITGenState(gen) == Ci_JITGenState_JustStarted ||
         (Ci_GetJITGenState(gen) == Ci_JITGenState_Running &&
          (*(uint64_t *)((uintptr_t)gen->gi_jit_data +
                         Ci_GEN_JIT_DATA_OFFSET_YIELD_POINT)));
}

static inline void Ci_SetJITGenState(PyGenObject *gen, CiJITGenState state) {
  *((CiJITGenState *)((char *)gen->gi_jit_data +
                      Ci_GEN_JIT_DATA_OFFSET_STATE)) = state;
}

static inline void Ci_MarkJITGenCompleted(PyGenObject *gen) {
  Ci_SetJITGenState(gen, Ci_JITGenState_Completed);
}

static inline void Ci_MarkJITGenThrowing(PyGenObject *gen) {
  Ci_SetJITGenState(gen, Ci_JITGenState_Throwing);
}

/* Functions similar to the ones above but also work for generators which may
 * not be JIT controlled.
 */

static inline int Ci_GenIsCompleted(PyGenObject *gen) {
  if (gen->gi_jit_data) {
    return Ci_GetJITGenState(gen) == Ci_JITGenState_Completed;
  }
  return gen->gi_frame == NULL || _PyFrameHasCompleted(gen->gi_frame);
}

static inline int Ci_GenIsJustStarted(PyGenObject *gen) {
  if (gen->gi_jit_data) {
    return Ci_GetJITGenState(gen) == Ci_JITGenState_JustStarted;
  }
  return gen->gi_frame && gen->gi_frame->f_lasti == -1;
}

static inline int Ci_GenIsExecuting(PyGenObject *gen) {
  if (gen->gi_jit_data) {
    return Ci_JITGenIsExecuting(gen);
  }
  return gen->gi_frame != NULL && _PyFrame_IsExecuting(gen->gi_frame);
}

static inline int Ci_GenIsRunnable(PyGenObject *gen) {
  if (gen->gi_jit_data) {
    return Ci_JITGenIsRunnable(gen);
  }
  return gen->gi_frame != NULL && _PyFrame_IsRunnable(gen->gi_frame);
}
