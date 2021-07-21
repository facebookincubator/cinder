// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/frame.h"

#include "Python.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

#include <optional>
#include <unordered_set>

static bool is_shadow_frame_for_gen(_PyShadowFrame* shadow_frame) {
  bool is_jit_gen = _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_CODE_RT &&
      static_cast<jit::CodeRuntime*>(_PyShadowFrame_GetPtr(shadow_frame))
          ->isGen();
  // Note this may be JIT or interpreted.
  bool is_gen_with_frame =
      _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME &&
      (_PyShadowFrame_GetPyFrame(shadow_frame)->f_code->co_flags &
       jit::kCoFlagsAnyGenerator);
  return is_jit_gen || is_gen_with_frame;
}

namespace jit {

namespace {

struct JITFrame {
  static const int kRetAddrIdx = 1;

  explicit JITFrame(void** b) : base(b) {}
  explicit JITFrame(_PyShadowFrame* shadow_frame)
      : base(reinterpret_cast<void**>(
            reinterpret_cast<uintptr_t>(shadow_frame) +
            sizeof(_PyShadowFrame))) {}

  void* retAddr() const {
    return base[kRetAddrIdx];
  }

  void setRetAddr(void* addr) {
    base[kRetAddrIdx] = addr;
  }

  void insertPyFrameUnlinkTrampoline(PyFrameObject* frame) {
    void* trampoline =
        jit::codegen::NativeGeneratorFactory::pyFrameUnlinkTrampoline();
    void* orig_retaddr = retAddr();
    // f_stacktop points to the first empty slot in the value stack
    *(frame->f_stacktop) = reinterpret_cast<PyObject*>(orig_retaddr);
    setRetAddr(trampoline);
  }

  void removePyFrameUnlinkTrampoline(PyFrameObject* frame) {
    void* trampoline =
        jit::codegen::NativeGeneratorFactory::pyFrameUnlinkTrampoline();
    if (retAddr() != trampoline) {
      return;
    }
    setRetAddr(*(frame->f_stacktop));
  }

  void** base;
};

PyFrameObject* createPyFrame(PyThreadState* tstate, jit::CodeRuntime& code_rt) {
  PyFrameObject* new_frame =
      PyFrame_New(tstate, code_rt.GetCode(), code_rt.GetGlobals(), nullptr);
  JIT_CHECK(new_frame != nullptr, "failed allocating frame");
  // PyFrame_New links the frame into the thread stack.
  Py_CLEAR(new_frame->f_back);
  new_frame->f_executing = 1;
  return new_frame;
}

BorrowedRef<PyFrameObject> materializePyFrame(
    PyThreadState* tstate,
    PyFrameObject* prev,
    _PyShadowFrame* shadow_frame) {
  if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
    return _PyShadowFrame_GetPyFrame(shadow_frame);
  }
  // Python frame doesn't exist yet, create it and insert it into the
  // stack. Ownership of the new reference is transferred to whomever
  // unlinks the frame.
  JIT_CHECK(
      _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_CODE_RT,
      "Unexpected shadow frame type");
  auto code_rt = static_cast<CodeRuntime*>(_PyShadowFrame_GetPtr(shadow_frame));
  PyFrameObject* frame = createPyFrame(tstate, *code_rt);
  if (prev != nullptr) {
    // New frame steals reference from previous frame to next frame.
    frame->f_back = prev->f_back;
    // Need to create a new reference for prev to the newly created frame.
    Py_INCREF(frame);
    prev->f_back = frame;
  } else {
    Py_XINCREF(tstate->frame);
    frame->f_back = tstate->frame;
    // ThreadState holds a borrowed reference
    tstate->frame = frame;
  }
  if (code_rt->isGen()) {
    // Transfer ownership of the new reference to frame to the generator
    // epilogue.  It handles detecting and unlinking the frame if the generator
    // is present in the `data` field of the shadow frame.
    //
    // A generator may be resumed multiple times. If a frame is materialized in
    // one activation, all subsequent activations must link/unlink the
    // materialized frame on function entry/exit. There's no active signal in
    // these cases, so we're forced to check for the presence of the
    // frame. Linking is handled by `_PyJIT_GenSend`, while unlinking is
    // handled by either the epilogue or, in the event that the generator
    // deopts, the interpreter loop. In the future we may refactor things so
    // that `_PyJIT_GenSend` handles both linking and unlinking.
    PyGenObject* gen = _PyShadowFrame_GetGen(shadow_frame);
    // f_gen is borrowed
    frame->f_gen = reinterpret_cast<PyObject*>(gen);
    gen->gi_frame = frame;
    Py_INCREF(frame);
  } else {
    // Transfer ownership of the new reference to frame to the unlink
    // trampoline.
    JITFrame{shadow_frame}.insertPyFrameUnlinkTrampoline(frame);
  }
  shadow_frame->data = _PyShadowFrame_MakeData(frame, PYSF_PYFRAME);

  return frame;
}

} // namespace

Ref<PyFrameObject> materializePyFrameForDeopt(
    PyThreadState* tstate,
    void** base) {
  JITFrame jf{base};
  auto py_frame = Ref<PyFrameObject>::steal(
      materializePyFrame(tstate, nullptr, tstate->shadow_frame));
  jf.removePyFrameUnlinkTrampoline(py_frame);
  return py_frame;
}

void assertShadowCallStackConsistent(PyThreadState* tstate) {
  PyFrameObject* py_frame = tstate->frame;
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;

  while (shadow_frame) {
    if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
      JIT_CHECK(
          py_frame == _PyShadowFrame_GetPyFrame(shadow_frame),
          "Inconsistent shadow and py frame");
      py_frame = py_frame->f_back;
    }
    shadow_frame = shadow_frame->prev;
  }

  if (py_frame != nullptr) {
    std::unordered_set<PyFrameObject*> seen;
    JIT_LOG(
        "Stack walk didn't consume entire python stack! Here's what's left:");
    PyFrameObject* left = py_frame;
    while (left && !seen.count(left)) {
      JIT_LOG("%s", PyUnicode_AsUTF8(left->f_code->co_name));
      seen.insert(left);
      left = left->f_back;
    }
    JIT_CHECK(false, "stack walk didn't consume entire python stack");
  }
}

BorrowedRef<PyFrameObject> materializeShadowCallStack(PyThreadState* tstate) {
  PyFrameObject* prev_py_frame = nullptr;
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;

  while (shadow_frame) {
    if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
      prev_py_frame = _PyShadowFrame_GetPyFrame(shadow_frame);
    } else {
      prev_py_frame = materializePyFrame(tstate, prev_py_frame, shadow_frame);
    }
    shadow_frame = shadow_frame->prev;
  }

  if (py_debug) {
    assertShadowCallStackConsistent(tstate);
  }

  return tstate->frame;
}

BorrowedRef<PyFrameObject> materializePyFrameForGen(
    PyThreadState* tstate,
    PyGenObject* gen) {
  JIT_CHECK(gen->gi_running, "gen must be running");
  if (gen->gi_frame) {
    return gen->gi_frame;
  }

  PyFrameObject* prev_py_frame = nullptr;
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;
  while (shadow_frame) {
    if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
      prev_py_frame = _PyShadowFrame_GetPyFrame(shadow_frame);
    } else if (is_shadow_frame_for_gen(shadow_frame)) {
      PyGenObject* cur_gen = _PyShadowFrame_GetGen(shadow_frame);
      if (cur_gen == gen) {
        return materializePyFrame(tstate, prev_py_frame, shadow_frame);
      }
    }
    shadow_frame = shadow_frame->prev;
  }

  JIT_CHECK(false, "failed to find frame for gen");

  return nullptr;
}

} // namespace jit

PyGenObject* _PyShadowFrame_GetGen(_PyShadowFrame* shadow_frame) {
  JIT_DCHECK(
      is_shadow_frame_for_gen(shadow_frame),
      "Not shadow-frame for a generator");

  // For generators, shadow frame is embedded in generator object. Thus we
  // can recover the generator object pointer from the shadow frame pointer.
  return reinterpret_cast<PyGenObject*>(
      reinterpret_cast<uintptr_t>(shadow_frame) -
      offsetof(PyGenObject, gi_shadow_frame));
}
