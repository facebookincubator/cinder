// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/frame.h"

#include "Python.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

#include <unordered_set>

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

  _PyShadowFrame* shadowFrame() const {
    return reinterpret_cast<_PyShadowFrame*>(base) - 1;
  }

  PyGenObject* generator() const {
    return *reinterpret_cast<PyGenObject**>(
        reinterpret_cast<uintptr_t>(base) - sizeof(_PyShadowFrame) -
        sizeof(void*));
  }

  CodeRuntime* getCodeRuntime() const {
    jit::Runtime* rt = jit::codegen::NativeGenerator::runtime();
    return rt->getCodeRuntime(shadowFrame()->code_rt_id);
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

bool isGenWithPyFrame(_PyShadowFrame* shadow_frame) {
  JITFrame jit_frame{shadow_frame};
  CodeRuntime* code_rt = jit_frame.getCodeRuntime();
  if (!code_rt->isGen()) {
    return false;
  }
  PyGenObject* gen = jit_frame.generator();
  return gen->gi_frame != nullptr;
}

BorrowedRef<PyFrameObject> materializePyFrame(
    PyThreadState* tstate,
    PyFrameObject* prev,
    JITFrame& jit_frame) {
  if (jit_frame.shadowFrame()->has_pyframe) {
    return prev == nullptr ? tstate->frame : prev->f_back;
  }
  CodeRuntime* code_rt = jit_frame.getCodeRuntime();
  bool is_gen = code_rt->isGen();
  switch (code_rt->frameMode()) {
    case jit::hir::FrameMode::kNone:
      JIT_CHECK(false, "unsupported!");
      break;
    case jit::hir::FrameMode::kNormal:
      return prev == nullptr ? tstate->frame : prev->f_back;
    case jit::hir::FrameMode::kShadow:
      if (jit_frame.shadowFrame()->has_pyframe) {
        return prev == nullptr ? tstate->frame : prev->f_back;
      }
      if (is_gen) {
        PyFrameObject* gen_frame = jit_frame.generator()->gi_frame;
        if (gen_frame != nullptr) {
          return gen_frame;
        }
      }
      break;
  }
  // Python frame doesn't exist yet, create it and insert it into the
  // stack. Ownership of the new reference is transferred to whomever
  // unlinks the frame.
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
  // Transfer ownership of new reference to frame to either the unlink
  // trampoline or the generator epilogue.
  if (!is_gen) {
    // Generator epilogue handles detecting and unlinking the frame if the
    // generator is present on the stack below the shadow frame.
    //
    // A generator may be resumed multiple times. If a frame is materialized in
    // one activation, all subsequent activations must link/unlink the
    // materialized frame on function entry/exit. There's no active signal in
    // these cases, so we're forced to check for the presence of the
    // frame. Linking is handled by `_PyJIT_GenSend`, while unlinking is
    // handled by either the epilogue or the interpreter loop (in the event
    // that the generator deopts). In the future we may refactor things so that
    // `_PyJIT_GenSend` handles both linking and unlinking.
    jit_frame.insertPyFrameUnlinkTrampoline(frame);
  }

  jit_frame.shadowFrame()->has_pyframe = 1;
  if (is_gen) {
    PyGenObject* gen = jit_frame.generator();
    // f_gen is borrowed
    frame->f_gen = reinterpret_cast<PyObject*>(gen);
    gen->gi_frame = frame;
    Py_INCREF(frame);
  }

  return frame;
}

} // namespace

Ref<PyFrameObject> materializePyFrameForDeopt(
    PyThreadState* tstate,
    void** base) {
  JITFrame jf{base};
  auto py_frame =
      Ref<PyFrameObject>::steal(materializePyFrame(tstate, nullptr, jf));
  jf.removePyFrameUnlinkTrampoline(py_frame);
  return py_frame;
}

BorrowedRef<PyFrameObject> materializeShadowCallStack(PyThreadState* tstate) {
  PyFrameObject* py_frame = tstate->frame;
  PyFrameObject* prev_py_frame = nullptr;
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;

  while (shadow_frame) {
    if (shadow_frame->has_pyframe || isGenWithPyFrame(shadow_frame)) {
      prev_py_frame = py_frame;
      py_frame = py_frame->f_back;
    } else {
      JITFrame jit_frame{shadow_frame};
      prev_py_frame = materializePyFrame(tstate, prev_py_frame, jit_frame);
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

  return tstate->frame;
}

BorrowedRef<PyFrameObject> materializePyFrameForGen(
    PyThreadState* tstate,
    PyGenObject* gen) {
  JIT_CHECK(gen->gi_running, "gen must be running");
  if (gen->gi_frame) {
    return gen->gi_frame;
  }

  PyFrameObject* py_frame = tstate->frame;
  PyFrameObject* prev_py_frame = nullptr;
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;
  while (shadow_frame) {
    if (shadow_frame->has_pyframe || isGenWithPyFrame(shadow_frame)) {
      if (py_frame->f_gen == reinterpret_cast<PyObject*>(gen)) {
        return py_frame;
      }
      prev_py_frame = py_frame;
      py_frame = py_frame->f_back;
    } else {
      JITFrame jit_frame{shadow_frame};
      auto code_rt = jit_frame.getCodeRuntime();
      if (code_rt->isGen() && (jit_frame.generator() == gen)) {
        return materializePyFrame(tstate, prev_py_frame, jit_frame);
      }
    }
    shadow_frame = shadow_frame->prev;
  }

  JIT_CHECK(false, "failed to find frame for gen");

  return nullptr;
}

void unlinkShadowFrame(
    PyThreadState* tstate,
    void** base,
    CodeRuntime& code_rt) {
  JITFrame jit_frame(base);
  switch (code_rt.frameMode()) {
    case jit::hir::FrameMode::kNone:
      break;
    case jit::hir::FrameMode::kNormal:
    case jit::hir::FrameMode::kShadow:
      _PyShadowFrame_Pop(tstate, jit_frame.shadowFrame());
      break;
  }
}

} // namespace jit
