// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/frame.h"

#include "Python.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/log.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

#include <optional>
#include <unordered_set>

static bool is_shadow_frame_for_gen(_PyShadowFrame* shadow_frame) {
  // TODO(bsimmers): This condition will need to change when we support eager
  // coroutine execution in the JIT, since there is no PyGenObject* for the
  // frame while executing eagerly (but isGen() will still return true).
  bool is_jit_gen = _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_CODE_RT &&
      static_cast<jit::CodeRuntime*>(_PyShadowFrame_GetPtr(shadow_frame))
          ->isGen();

  // Note this may be JIT or interpreted.
  bool is_gen_with_frame =
      _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME &&
      _PyShadowFrame_GetPyFrame(shadow_frame)->f_gen != nullptr;
  return is_jit_gen || is_gen_with_frame;
}

namespace jit {

namespace {

PyObject* getModuleName(_PyShadowFrame* shadow_frame) {
  PyObject* globals;
  PyObject* result;
  switch (_PyShadowFrame_GetPtrKind(shadow_frame)) {
    case PYSF_PYFRAME: {
      PyFrameObject* pyframe =
          static_cast<PyFrameObject*>(_PyShadowFrame_GetPtr(shadow_frame));
      globals = pyframe->f_globals;
      JIT_DCHECK(
          globals != nullptr, "Python frame (%p) has NULL globals", pyframe);
      result = PyDict_GetItemString(globals, "__name__");
      break;
    }
    case PYSF_CODE_RT: {
      jit::CodeRuntime* code_rt =
          static_cast<CodeRuntime*>(_PyShadowFrame_GetPtr(shadow_frame));
      globals = code_rt->GetGlobals();
      JIT_DCHECK(
          globals != nullptr,
          "JIT Runtime frame (%p) has NULL globals",
          code_rt);
      result = PyDict_GetItemString(globals, "__name__");
      break;
    }
    case PYSF_PYCODE: {
      // TODO(emacs): Implement this once the inliner is out in prod
      result = PyUnicode_FromStringAndSize("<inlined>", 9);
    }
    default: {
      JIT_CHECK(false, "unknown ptr kind");
    }
  }
  Py_XINCREF(result);
  return result;
}

// Return the base of the stack frame given its shadow frame.
uintptr_t getFrameBaseFromOnStackShadowFrame(_PyShadowFrame* shadow_frame) {
  // The shadow frame is embedded in the frame header at the beginning of the
  // stack frame.
  return reinterpret_cast<uintptr_t>(shadow_frame) +
      offsetof(FrameHeader, shadow_frame) + sizeof(_PyShadowFrame);
}

CodeRuntime* getCodeRuntime(_PyShadowFrame* shadow_frame) {
  JIT_CHECK(
      _PyShadowFrame_GetOwner(shadow_frame) == PYSF_JIT,
      "shadow frame not owned by the JIT");
  if (is_shadow_frame_for_gen(shadow_frame)) {
    // The shadow frame belongs to a generator; retrieve the CodeRuntime
    // directly from the generator.
    PyGenObject* gen = _PyShadowFrame_GetGen(shadow_frame);
    return reinterpret_cast<GenDataFooter*>(gen->gi_jit_data)->code_rt;
  }
  // The shadow frame belongs to a JIT-compiled function that is on the stack;
  // read the CodeRuntime from the stack.
  uintptr_t frame_base = getFrameBaseFromOnStackShadowFrame(shadow_frame);
  CodeRuntime* ret = nullptr;
  uintptr_t code_rt_loc =
      frame_base - offsetof(FrameHeader, code_rt) - kPointerSize;
  memcpy(&ret, reinterpret_cast<CodeRuntime*>(code_rt_loc), kPointerSize);
  return ret;
}

// Find a shadow frame in the call stack. If the frame was found, returns the
// last Python frame seen during the search, or nullptr if there was none.
std::optional<PyFrameObject*> findInnermostPyFrameForShadowFrame(
    PyThreadState* tstate,
    _PyShadowFrame* needle) {
  PyFrameObject* prev_py_frame = nullptr;
  _PyShadowFrame* shadow_frame = tstate->shadow_frame;
  while (shadow_frame) {
    if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
      prev_py_frame = _PyShadowFrame_GetPyFrame(shadow_frame);
    } else if (shadow_frame == needle) {
      return prev_py_frame;
    }
    shadow_frame = shadow_frame->prev;
  }
  return {};
}

// Return whether or not needle is linked into a call stack
bool isShadowFrameLinked(PyThreadState* tstate, _PyShadowFrame* needle) {
  if (tstate->shadow_frame == needle || needle->prev != nullptr) {
    return true;
  }
  // Handle the case where needle is the last frame on the call stack
  return findInnermostPyFrameForShadowFrame(tstate, needle).has_value();
}

// Return the instruction pointer for the JIT-compiled function that is
// executing shadow_frame.
uintptr_t
getIP(PyThreadState* tstate, _PyShadowFrame* shadow_frame, int frame_size) {
  JIT_CHECK(
      _PyShadowFrame_GetOwner(shadow_frame) == PYSF_JIT,
      "shadow frame not executed by the JIT");
  uintptr_t frame_base;
  if (is_shadow_frame_for_gen(shadow_frame)) {
    PyGenObject* gen = _PyShadowFrame_GetGen(shadow_frame);
    auto footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);
    if (gen->gi_running && isShadowFrameLinked(tstate, shadow_frame)) {
      // The generator is running. Under rare circumstances the generater will
      // be marked as running but won't yet be resumed. We check to make sure
      // the shadow frame in linked into the call stack to account for this
      // case. See the comment in materializePyFrameForGen() for details.
      frame_base = footer->originalRbp;
    } else {
      // The generator is suspended.
      return footer->yieldPoint->resumeTarget();
    }
  } else {
    frame_base = getFrameBaseFromOnStackShadowFrame(shadow_frame);
  }
  // Read the saved IP from the stack
  uintptr_t ip;
  auto saved_ip =
      reinterpret_cast<uintptr_t*>(frame_base - frame_size - kPointerSize);
  memcpy(&ip, saved_ip, kPointerSize);
  return ip;
}

// If shadow_frame is being executed by the JIT, update py_frame to reflect the
// state of the Python function being executed.
void updatePyFrame(
    PyThreadState* tstate,
    BorrowedRef<PyFrameObject> py_frame,
    _PyShadowFrame* shadow_frame) {
  if (_PyShadowFrame_GetOwner(shadow_frame) != PYSF_JIT) {
    // Interpreter is executing this frame; don't touch the PyFrameObject.
    return;
  }
  CodeRuntime* code_rt = getCodeRuntime(shadow_frame);
  uintptr_t ip = getIP(tstate, shadow_frame, code_rt->frame_size());
  std::optional<int> bc_off = code_rt->getBCOffForIP(ip);
  if (!bc_off.has_value()) {
    // This can happen if we forget to record the address following a call made
    // by JIT-compiled code. Just emit a warning instead of crashing since this
    // should only result in incorrect bytecode offsets being reported.
    auto qn = Ref<>::steal(_PyShadowFrame_GetFullyQualifiedName(shadow_frame));
    const char* fqname = qn != nullptr ? PyUnicode_AsUTF8(qn) : "<unknown>";
    JIT_LOG("WARNING: Couldn't find bc off for ip %lx in %s", ip, fqname);
    return;
  }
  py_frame->f_lasti = bc_off.value();
}

// Create an unlinked PyFrameObject for the given shadow frame.
Ref<PyFrameObject> createPyFrame(
    PyThreadState* tstate,
    _PyShadowFrame* shadow_frame) {
  JIT_CHECK(
      _PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_CODE_RT,
      "Unexpected shadow frame type");
  auto code_rt = static_cast<CodeRuntime*>(_PyShadowFrame_GetPtr(shadow_frame));
  Ref<PyFrameObject> py_frame = Ref<PyFrameObject>::steal(
      PyFrame_New(tstate, code_rt->GetCode(), code_rt->GetGlobals(), nullptr));
  JIT_CHECK(py_frame != nullptr, "failed allocating frame");
  // PyFrame_New links the frame into the thread stack.
  Py_CLEAR(py_frame->f_back);
  py_frame->f_executing = 1;
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
    py_frame->f_gen = reinterpret_cast<PyObject*>(gen);
    // gi_frame is owned
    gen->gi_frame = py_frame.get();
    Py_INCREF(py_frame);
  }
  shadow_frame->data =
      _PyShadowFrame_MakeData(py_frame, PYSF_PYFRAME, PYSF_JIT);
  updatePyFrame(tstate, py_frame, shadow_frame);
  return py_frame;
}

void insertPyFrameBefore(
    PyThreadState* tstate,
    BorrowedRef<PyFrameObject> frame,
    BorrowedRef<PyFrameObject> cursor) {
  if (cursor == nullptr) {
    // Insert frame at the top of the call stack
    Py_XINCREF(tstate->frame);
    frame->f_back = tstate->frame;
    // ThreadState holds a borrowed reference
    tstate->frame = frame;
    return;
  }
  // Insert frame immediately before cursor in the call stack
  // New frame steals reference for cursor->f_back
  frame->f_back = cursor->f_back;
  // Need to create a new reference for cursor to the newly created frame.
  Py_INCREF(frame);
  cursor->f_back = frame;
}

// Get the PyFrameObject for shadow_frame or create and insert one before
// cursor if no PyFrameObject exists.
BorrowedRef<PyFrameObject> materializePyFrame(
    PyThreadState* tstate,
    _PyShadowFrame* shadow_frame,
    PyFrameObject* cursor) {
  if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
    PyFrameObject* py_frame = _PyShadowFrame_GetPyFrame(shadow_frame);
    updatePyFrame(tstate, py_frame, shadow_frame);
    return py_frame;
  }
  // Python frame doesn't exist yet, create it and insert it into the
  // call stack.
  Ref<PyFrameObject> frame = createPyFrame(tstate, shadow_frame);
  insertPyFrameBefore(tstate, frame, cursor);
  // Ownership of the new reference is transferred to whomever unlinks the
  // frame (either the JIT epilogue or the interpreter loop).
  return frame.release();
}

int getLineNo(_PyShadowFrame* shadow_frame) {
  _PyShadowFrame_PtrKind ptr_kind = _PyShadowFrame_GetPtrKind(shadow_frame);
  void* ptr = _PyShadowFrame_GetPtr(shadow_frame);
  int lasti = 0;
  PyCodeObject* code = nullptr;

  switch (ptr_kind) {
    case PYSF_CODE_RT: {
      auto code_rt = static_cast<jit::CodeRuntime*>(ptr);
      uintptr_t ip =
          getIP(_PyThreadState_GET(), shadow_frame, code_rt->frame_size());
      std::optional<int> bc_off = code_rt->getBCOffForIP(ip);
      if (bc_off.has_value()) {
        lasti = bc_off.value();
      }
      code = code_rt->GetCode();
      break;
    }
    case PYSF_PYFRAME: {
      PyFrameObject* frame = static_cast<PyFrameObject*>(ptr);
      lasti = frame->f_lasti;
      code = frame->f_code;
      break;
    }
    case PYSF_PYCODE: {
      // inlined code
      code = static_cast<PyCodeObject*>(ptr);
      break;
    }
    case PYSF_DUMMY:
      JIT_CHECK(false, "Unsupported ptr kind: PYSF_DUMMY");
  }

  JIT_DCHECK(code != nullptr, "code object must be found");

  return PyCode_Addr2Line(code, lasti);
}

} // namespace

Ref<PyFrameObject> materializePyFrameForDeopt(PyThreadState* tstate) {
  auto py_frame = Ref<PyFrameObject>::steal(
      materializePyFrame(tstate, tstate->shadow_frame, nullptr));
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
    prev_py_frame = materializePyFrame(tstate, shadow_frame, prev_py_frame);
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
  _PyShadowFrame* shadow_frame = &gen->gi_shadow_frame;
  if (gen->gi_frame) {
    updatePyFrame(tstate, gen->gi_frame, shadow_frame);
    return gen->gi_frame;
  }

  if (!gen->gi_running) {
    auto gen_footer = reinterpret_cast<GenDataFooter*>(gen->gi_jit_data);
    if (gen_footer->state == _PyJitGenState_Completed) {
      return nullptr;
    }
    Ref<PyFrameObject> py_frame = createPyFrame(tstate, shadow_frame);
    py_frame->f_executing = 0;
    // It's safe to destroy our reference to the frame; gen holds a strong
    // reference to the frame which keeps the frame alive.
    return py_frame;
  }

  // Check if the generator's shadow frame is on the call stack. The generator
  // will be marked as running but will not be on the stack when it appears as
  // a predecessor in a chain of generators into which an exception was
  // thrown. For example, given an "await stack" of coroutines like the
  // following, where ` a <- b` indicates a `a` awaits `b`,
  //
  //   coro0 <- coro1 <- coro2
  //
  // if someone does `coro0.throw(...)`, then `coro0` and `coro1` will be
  // marked as running but will not appear on the stack while `coro2` is
  // handling the exception.
  std::optional<PyFrameObject*> cursor =
      findInnermostPyFrameForShadowFrame(tstate, shadow_frame);
  if (cursor.has_value()) {
    return materializePyFrame(tstate, shadow_frame, cursor.value());
  }
  // It's safe to destroy our reference to the frame; gen holds a strong
  // reference to the frame which keeps the frame alive.
  return createPyFrame(tstate, shadow_frame);
}

} // namespace jit

int _PyShadowFrame_HasGen(_PyShadowFrame* shadow_frame) {
  return is_shadow_frame_for_gen(shadow_frame);
}

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

PyCodeObject* _PyShadowFrame_GetCode(_PyShadowFrame* shadow_frame) {
  _PyShadowFrame_PtrKind ptr_kind = _PyShadowFrame_GetPtrKind(shadow_frame);
  void* ptr = _PyShadowFrame_GetPtr(shadow_frame);
  switch (ptr_kind) {
    case PYSF_CODE_RT:
      return static_cast<jit::CodeRuntime*>(ptr)->GetCode();
    case PYSF_PYFRAME:
      return static_cast<PyFrameObject*>(ptr)->f_code;
    case PYSF_PYCODE:
      return static_cast<PyCodeObject*>(ptr);
    default:
      JIT_CHECK(false, "Unsupported ptr kind %d:", ptr_kind);
  }
}

PyObject* _PyShadowFrame_GetFullyQualifiedName(_PyShadowFrame* shadow_frame) {
  PyObject* mod_name = jit::getModuleName(shadow_frame);
  if (!mod_name) {
    return NULL;
  }
  PyCodeObject* code = _PyShadowFrame_GetCode(shadow_frame);
  PyObject* result = PyUnicode_FromFormat("%U:%U", mod_name, code->co_qualname);
  Py_DECREF(mod_name);
  return result;
}

_PyShadowFrame* _PyShadowFrame_GetAwaiterFrame(_PyShadowFrame* shadow_frame) {
  _PyShadowFrame* result;
  if (_PyShadowFrame_HasGen(shadow_frame)) {
    PyGenObject* gen = _PyShadowFrame_GetGen(shadow_frame);
    if (!PyCoro_CheckExact((PyObject*)gen)) {
      // This means we have a real generator, so it cannot have awaiter frames.
      // but we also did not fail.
      return nullptr;
    }
    PyCoroObject* awaiter = ((PyCoroObject*)gen)->cr_awaiter;
    if (!awaiter) {
      // This is fine, not every coroutine needs to have an awaiter
      return nullptr;
    }
    result = &(awaiter->cr_shadow_frame);
    return result;
  }
  return nullptr;
}

int _PyShadowFrame_WalkAndPopulate(
    PyCodeObject** async_stack,
    int* async_linenos,
    int async_stack_len,
    PyCodeObject** sync_stack,
    int* sync_linenos,
    int sync_stack_len) {
  _PyShadowFrame* shadow_frame = PyThreadState_GET()->shadow_frame;
  // First walk the async stack (through awaiter pointers)
  int i = 0;
  _PyShadowFrame* awaiter_frame = NULL;
  while (shadow_frame != NULL && i < async_stack_len) {
    async_stack[i] = _PyShadowFrame_GetCode(shadow_frame);
    async_linenos[i] = jit::getLineNo(shadow_frame);

    awaiter_frame =
        _PyShadowFrame_GetAwaiterFrame((_PyShadowFrame*)shadow_frame);

    shadow_frame = shadow_frame->prev;
    if (awaiter_frame != NULL) {
      shadow_frame = awaiter_frame;
    }
    i++;
  }

  // Next walk the sync stack (shadow frames only)
  int j = 0;
  shadow_frame = PyThreadState_GET()->shadow_frame;
  while (shadow_frame != NULL && j < sync_stack_len) {
    sync_stack[j] = _PyShadowFrame_GetCode(shadow_frame);
    sync_linenos[j] = jit::getLineNo(shadow_frame);
    shadow_frame = shadow_frame->prev;
    j++;
  }
  return 0;
}
