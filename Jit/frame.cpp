#include "Jit/frame.h"

#include <vector>

#include "Jit/jit_rt.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/util.h"

#include "internal/pycore_pystate.h"

static PyObject* tinyframe_repr(TinyFrame* f) {
  return PyUnicode_FromFormat(
      "<tinyframe at %p, file %R, code %S>",
      f,
      f->code->co_filename,
      f->code->co_name);
}

// this type is only used for identifying tiny frames. It does not use
// any slots in PyTypeObject.
PyTypeObject PyTinyFrame_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "tinyframe",
    sizeof(TinyFrame),
    sizeof(PyObject*),
    (void (*)(PyObject*))JIT_TinyFrameDeallocate, /* tp_dealloc */
    0, /* tp_vectorcall_offset */
    0, /* tp_getattr */
    0, /* tp_setattr */
    0, /* tp_reserved */
    (reprfunc)tinyframe_repr, /* tp_repr */
    0, /* tp_as_number */
    0, /* tp_as_sequence */
    0, /* tp_as_mapping */
    0, /* tp_hash */
    0, /* tp_call */
    0, /* tp_str */
    0, /* tp_getattro */
    0, /* tp_setattro */
    0, /* tp_as_buffer */
    0, /* tp_flags */
    0, /* tp_doc */
    0, /* tp_traverse */
    0, /* tp_clear */
    0, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    0, /* tp_iter */
    0, /* tp_iternext */
    0, /* tp_methods */
    0, /* tp_members */
    0, /* tp_getset */
    0, /* tp_base */
    0, /* tp_dict */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

namespace jit {
class TinyFrameAllocator {
 public:
  ~TinyFrameAllocator() {
    for (auto& frames : frames_) {
      delete[] frames;
    }
  }

  static TinyFrameAllocator& GetInstance() {
    static TinyFrameAllocator allocator;
    return allocator;
  }

  TinyFrame*
  Allocate(PyThreadState* tstate, PyCodeObject* code, PyObject* globals) {
    auto frame = _Allocate();
    if (frame == NULL) {
      return NULL;
    }
    frame->tstate = tstate;
    frame->code = code;
    frame->globals = globals;
    // This is set if/when a generator is created for this frame.
    frame->gen = NULL;

    frame->t.f_back = tstate->frame;
    Py_XINCREF(tstate->frame);

    return frame;
  }

  void Deallocate(TinyFrame* frame) {
    Py_CLEAR(frame->t.tf_back);
    frame->t.tf_back = next_;
    next_ = frame;
  }

  DISALLOW_COPY_AND_ASSIGN(TinyFrameAllocator);

 private:
  TinyFrameAllocator() : next_index_(TINY_FRAME_CHUNK_SIZE), next_(nullptr) {}

  TinyFrame* _Allocate();

  std::vector<TinyFrame*> frames_;
  int next_index_;
  TinyFrame* next_;

  static constexpr int TINY_FRAME_CHUNK_SIZE = 1024;
};

TinyFrame* TinyFrameAllocator::_Allocate() {
  // check if we have available frames from the linked list
  if (next_ != nullptr) {
    auto frame = next_;
    next_ = static_cast<TinyFrame*>(next_->t.tf_back);
    JIT_DCHECK(
        PyTinyFrame_Check(frame),
        "We should only have tiny frames in the linked list.");
    _Py_NewReference((PyObject*)frame);
    return frame;
  }

  // check if we have available frames from the chunk
  TinyFrame* frame = nullptr;
  if (next_index_ < TINY_FRAME_CHUNK_SIZE) {
    frame = frames_.back() + (next_index_++);
  } else {
    // allocate a new chunk
    auto chunk = new TinyFrame[TINY_FRAME_CHUNK_SIZE];
    frames_.push_back(chunk);
    next_index_ = 1;
    frame = chunk;
  }

  PyObject_INIT(frame, &PyTinyFrame_Type);
  return frame;
}
} // namespace jit

using jit::TinyFrameAllocator;

TinyFrame* JIT_TinyFrameAllocate(
    PyThreadState* tstate,
    PyCodeObject* code,
    PyObject* globals) {
  return TinyFrameAllocator::GetInstance().Allocate(tstate, code, globals);
}

void JIT_TinyFrameDeallocate(TinyFrame* frame) {
  TinyFrameAllocator::GetInstance().Deallocate(frame);
}

int JIT_IsTinyFrame(void* frame) {
  return frame != nullptr && PyTinyFrame_Check(frame);
}

static PyFrameObject* _MaterializeFrame(TinyFrame* tf) {
  auto tstate = tf->tstate;
  auto globals = tf->globals;
  PyObject* builtins = PyEval_GetBuiltins();
  if (builtins == nullptr) {
    return nullptr;
  }

  Py_INCREF(builtins);

  auto co = tf->code;
  PyFrameObject* frame =
      _PyFrame_NewWithBuiltins_NoTrack(tstate, co, globals, builtins, nullptr);

  if (frame == nullptr) {
    Py_DECREF(builtins);
    return nullptr;
  }

  // TODO: may need to create a separate function for allocating a frame, but
  // currently, we just use _PyFrame_NewWithBuiltins_NoTrack, and we have to
  // reverse the changes to tstate and the top frame refcount.
  tstate->frame = frame->f_back;
  Py_XDECREF(tstate->frame);

  frame->f_back = tf->t.f_back;
  tf->t.f_back = NULL; // ownership is transfered to the new frame

  frame->f_gen = tf->gen;
  if (frame->f_gen) {
    reinterpret_cast<PyGenObject*>(frame->f_gen)->gi_frame = frame;
    Py_INCREF(frame);
  }

  Py_DECREF(tf);

  frame->f_executing = 1;

  return frame;
}

PyFrameObject* JIT_MaterializePrevFrame(PyFrameObject* cur) {
  assert(!JIT_IsTinyFrame(cur));
  PyFrameObject* f = cur->f_back;
  if (!JIT_IsTinyFrame(f)) {
    return f;
  }

  assert(PyThreadState_Get()->frame != f);
  f = _MaterializeFrame(reinterpret_cast<TinyFrame*>(f));
  // The tiny frame has an implicit reference count from when it
  // shows back up in tstate->frame as the frame unwindows.
  // decref that now, and give the newly created frame a
  // refcount for that.
  Py_DECREF(cur->f_back);
  Py_INCREF(f);

  cur->f_back = f;
  return f;
}

PyFrameObject* JIT_MaterializeTopFrame(PyThreadState* tstate) {
  PyFrameObject* f = tstate->frame;
  if (!JIT_IsTinyFrame(f)) {
    return f;
  }

  f = _MaterializeFrame(reinterpret_cast<TinyFrame*>(f));
  tstate->frame = f;
  return f;
}

PyFrameObject* JIT_MaterializeToFrame(
    PyThreadState* tstate,
    PyFrameObject* frame) {
  if (!JIT_IsTinyFrame(frame)) {
    return frame;
  } else if (tstate->frame == frame) {
    return JIT_MaterializeTopFrame(tstate);
  }

  PyFrameObject* f = tstate->frame;
  while (f->f_back != nullptr) {
    PyFrameObject* next = f->f_back;
    if (next == frame) {
      return JIT_MaterializePrevFrame(f);
    }

    f = JIT_MaterializePrevFrame(f);
  }

  return frame;
}
