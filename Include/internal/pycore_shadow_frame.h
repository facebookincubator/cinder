/* Copyright (c) Meta Platforms, Inc. and affiliates. */
#ifndef Py_SHADOW_FRAME_H
#define Py_SHADOW_FRAME_H

#include "Python.h"
#include "cinder/hooks.h"
#include "frameobject.h"
#include "internal/pycore_shadow_frame_struct.h"

#include <stdint.h>
#include <stddef.h>

#ifndef Py_LIMITED_API

#ifdef __cplusplus
extern "C" {
#endif

// TODO(mpage) - Generalize bit setting/getting into helpers?
static const unsigned int _PyShadowFrame_NumTagBits = 4;
static const uintptr_t _PyShadowFrame_TagMask =
    (1 << _PyShadowFrame_NumTagBits) - 1;
static const uintptr_t _PyShadowFrame_PtrMask = ~_PyShadowFrame_TagMask;

#define TAG_MASK(num_bits, off) ((1 << num_bits) - 1) << off

static const unsigned int kShadowFrameSize = sizeof(_PyShadowFrame);
#define SHADOW_FRAME_FIELD_OFF(field) (int{(offsetof(_PyShadowFrame, field))})
static const unsigned int kJITShadowFrameSize = sizeof(JITShadowFrame);
#define JIT_SHADOW_FRAME_FIELD_OFF(field)                                      \
  (int{(offsetof(JITShadowFrame, field))})
static const unsigned int _PyShadowFrame_NumPtrKindBits = 2;
static const unsigned int _PyShadowFrame_PtrKindOff = 0;
static const uintptr_t _PyShadowFrame_PtrKindMask =
    TAG_MASK(_PyShadowFrame_NumPtrKindBits, _PyShadowFrame_PtrKindOff);

static const unsigned int _PyShadowFrame_NumOwnerBits = 1;
static const unsigned int _PyShadowFrame_OwnerOff =
    _PyShadowFrame_PtrKindOff + _PyShadowFrame_NumPtrKindBits;
static const uintptr_t _PyShadowFrame_OwnerMask =
    TAG_MASK(_PyShadowFrame_NumOwnerBits, _PyShadowFrame_OwnerOff);

#undef TAG_MASK

static inline _PyShadowFrame_PtrKind
_PyShadowFrame_GetPtrKind(_PyShadowFrame *shadow_frame) {
  return (_PyShadowFrame_PtrKind)(shadow_frame->data &
                                  _PyShadowFrame_PtrKindMask);
}

static inline void _PyShadowFrame_SetOwner(_PyShadowFrame *shadow_frame,
                                           _PyShadowFrame_Owner owner) {
  uintptr_t data = shadow_frame->data & ~_PyShadowFrame_OwnerMask;
  shadow_frame->data = data | (owner << _PyShadowFrame_OwnerOff);
}

static inline _PyShadowFrame_Owner
_PyShadowFrame_GetOwner(_PyShadowFrame *shadow_frame) {
  uintptr_t data = shadow_frame->data & _PyShadowFrame_OwnerMask;
  return (_PyShadowFrame_Owner)(data >> _PyShadowFrame_OwnerOff);
}

static inline void *_PyShadowFrame_GetPtr(_PyShadowFrame *shadow_frame) {
  return (void *)(shadow_frame->data & _PyShadowFrame_PtrMask);
}

static inline PyFrameObject *
_PyShadowFrame_GetPyFrame(_PyShadowFrame *shadow_frame) {
  assert(_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME);
  return (PyFrameObject *)_PyShadowFrame_GetPtr(shadow_frame);
}

void _PyShadowFrame_DumpStack(PyThreadState* state);

static inline uintptr_t _PyShadowFrame_MakeData(void *ptr,
                                                _PyShadowFrame_PtrKind ptr_kind,
                                                _PyShadowFrame_Owner owner) {
  assert(((uintptr_t)ptr & _PyShadowFrame_PtrKindMask) == 0);
  return (uintptr_t)ptr | (owner << _PyShadowFrame_OwnerOff) |
         (ptr_kind << _PyShadowFrame_PtrKindOff);
}

static inline void _PyShadowFrame_PushInterp(PyThreadState *tstate,
                                             _PyShadowFrame *shadow_frame,
                                             PyFrameObject *py_frame) {
  shadow_frame->prev = tstate->shadow_frame;
  tstate->shadow_frame = shadow_frame;
  shadow_frame->data =
      _PyShadowFrame_MakeData(py_frame, PYSF_PYFRAME, PYSF_INTERP);
}

static inline void _PyShadowFrame_Pop(PyThreadState *tstate,
                                      _PyShadowFrame *shadow_frame) {
  assert(tstate->shadow_frame == shadow_frame);
  tstate->shadow_frame = shadow_frame->prev;
  shadow_frame->prev = NULL;
}

static inline void *JITShadowFrame_GetOrigPtr(JITShadowFrame *jit_sf) {
  return (void *)(jit_sf->orig_data & _PyShadowFrame_PtrMask);
}

static inline _PyShadowFrame_PtrKind
JITShadowFrame_GetOrigPtrKind(JITShadowFrame *jit_sf) {
  return (_PyShadowFrame_PtrKind)(jit_sf->orig_data &
                                  _PyShadowFrame_PtrKindMask);
}

/*
 * Return the kind of runtime pointer (PYSF_RTFS or PYSF_CODE_RT) held by
 * jit_sf.
 */
static inline _PyShadowFrame_PtrKind
JITShadowFrame_GetRTPtrKind(JITShadowFrame *jit_sf) {
  _PyShadowFrame_PtrKind kind = _PyShadowFrame_GetPtrKind(&jit_sf->sf);
  if (kind == PYSF_PYFRAME) {
    return JITShadowFrame_GetOrigPtrKind(jit_sf);
  }
  return kind;
}

/*
 * Return the runtime pointer (jit::CodeRuntime* or jit::RuntimeFrameState*)
 * held by jit_sf.
 */
static inline void *JITShadowFrame_GetRTPtr(JITShadowFrame *jit_sf) {
  _PyShadowFrame_PtrKind kind = _PyShadowFrame_GetPtrKind(&jit_sf->sf);
  if (kind == PYSF_PYFRAME) {
    return JITShadowFrame_GetOrigPtr(jit_sf);
  }
  return _PyShadowFrame_GetPtr((_PyShadowFrame *)jit_sf);
}

/* Populates codeobject pointers in the given arrays. Meant to only be called
  from profiling frameworks. Both async_stack and sync_stack contain borrowed
  references.

  `async_stack` - Contains pointers to PyCodeObject on successful run. If errors
  occur, the contents should not be used.

  `async_linenos` - Is populated with the line numbers (corresponding to each) entry
  in `async_stack`

  `sync_stack` - Similar to `async_stack`, but contains sync stack entries.

  `sync_linenos` - Similar to `async_linenos`, but contains for the sync stack.

  `array_len` - Assumes that all the above arrays are of the same length, which is
  specified by this parameter.

  `async_stack_len_out`, `sync_stack_len_out` - These are out parameters. On successful
  run, they contain the lengths of the async and sync stacks respectively.
*/
CiAPI_FUNC(int) _PyShadowFrame_WalkAndPopulate(
    PyCodeObject** async_stack,
    int* async_linenos,
    PyCodeObject** sync_stack,
    int* sync_linenos,
    int array_capacity,
    int* async_stack_len_out,
    int* sync_stack_len_out);

static inline int _PyShadowFrame_HasGen(_PyShadowFrame* shadow_frame) {
  if (Ci_hook_ShadowFrame_HasGen_JIT) {
    return Ci_hook_ShadowFrame_HasGen_JIT(shadow_frame);
  }
  return
    _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME &&
    _PyShadowFrame_GetPyFrame(shadow_frame)->f_gen != NULL;
}

static inline PyGenObject* _PyShadowFrame_GetGen(_PyShadowFrame* shadow_frame) {
  assert(_PyShadowFrame_HasGen(shadow_frame));

  // For generators, shadow frame is embedded in generator object. Thus we
  // can recover the generator object pointer from the shadow frame pointer.
  return (PyGenObject*)(
      ((uintptr_t)shadow_frame) -
      offsetof(PyGenObject, gi_shadow_frame));
}

/* Looks up the awaiter shadow frame (if any) from the given shadow frame */
static inline _PyShadowFrame* _PyShadowFrame_GetAwaiterFrame(_PyShadowFrame* shadow_frame) {
  if (_PyShadowFrame_HasGen(shadow_frame)) {
    PyGenObject* gen = _PyShadowFrame_GetGen(shadow_frame);
    if (!PyCoro_CheckExact((PyObject*)gen)) {
      // This means we have a real generator, so it cannot have awaiter frames.
      // but we also did not fail.
      return NULL;
    }
    PyCoroObject* awaiter = ((PyCoroObject*)gen)->ci_cr_awaiter;
    if (!awaiter) {
      // This is fine, not every coroutine needs to have an awaiter
      return NULL;
    }
    return &(awaiter->cr_shadow_frame);
  }
  return NULL;
}

/* Return a borrowed reference to the code object for shadow_frame */
static inline PyCodeObject* _PyShadowFrame_GetCode(_PyShadowFrame* shadow_frame) {
  if (Ci_hook_ShadowFrame_GetCode_JIT) {
    return Ci_hook_ShadowFrame_GetCode_JIT(shadow_frame);
  }

  assert(_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME);
  return ((PyFrameObject*)_PyShadowFrame_GetPtr(shadow_frame))->f_code;
}

static inline PyObject* _PyShadowFrame_GetModuleName(_PyShadowFrame* shadow_frame) {
  if (Ci_hook_ShadowFrame_GetModuleName_JIT) {
    return Ci_hook_ShadowFrame_GetModuleName_JIT(shadow_frame);
  }

  assert(_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME);
  PyFrameObject* pyframe =
      (PyFrameObject*)_PyShadowFrame_GetPtr(shadow_frame);
  PyObject* globals = pyframe->f_globals;
  assert(globals);
  PyObject* result = PyDict_GetItemString(globals, "__name__");
  Py_XINCREF(result);
  return result;
}

/* Returns the fully qualified name of code running in the frame */
static inline PyObject* _PyShadowFrame_GetFullyQualifiedName(_PyShadowFrame* shadow_frame) {
  PyObject* mod_name = _PyShadowFrame_GetModuleName(shadow_frame);
  if (!mod_name) {
    return NULL;
  }

  if (!PyUnicode_Check(mod_name)) {
    PyErr_Format(
        PyExc_RuntimeError,
        "expected module name to be a string, got %s",
        Py_TYPE(mod_name)->tp_name);
    Py_DECREF(mod_name);
    return NULL;
  }

  PyCodeObject* code = _PyShadowFrame_GetCode(shadow_frame);
  PyObject* code_name = code->co_qualname;
  char const* format = "%U:%U";
  // If co_qualname is some invalid value, we try to do our best by using the
  // co_name instead. While this is an error condition (and should be
  // investigated), we don't crash here, someone might be trying to debug the
  // issue itself by calling this function!
  if (!code->co_qualname || !PyUnicode_Check(code->co_qualname)) {
    code_name = code->co_name;
    format = "%U:!%U";
  }

  PyObject* result = PyUnicode_FromFormat(format, mod_name, code_name);
  Py_DECREF(mod_name);
  return result;
}

#ifdef __cplusplus
}
#endif

#endif /* Py_LIMITED_API */
#endif /* !Py_SHADOW_FRAME_H */
