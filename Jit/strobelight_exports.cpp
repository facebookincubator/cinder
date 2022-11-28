// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/strobelight_exports.h"

#include "frameobject.h"
#include "internal/pycore_shadow_frame.h"

#include "Jit/runtime.h"

int64_t __strobe_CodeRuntime_py_code = jit::CodeRuntime::kPyCodeOffset;
int32_t __strobe_PyVersion_major = PY_MAJOR_VERSION;
int32_t __strobe_PyVersion_micro = PY_MICRO_VERSION;
int32_t __strobe_PyVersion_minor = PY_MINOR_VERSION;
int64_t __strobe_PyCodeObject_co_flags = offsetof(PyCodeObject, co_flags);
int64_t __strobe_PyCodeObject_filename = offsetof(PyCodeObject, co_filename);
int64_t __strobe_PyCodeObject_name = offsetof(PyCodeObject, co_name);
int64_t __strobe_PyCodeObject_qualname = offsetof(PyCodeObject, co_qualname);
int64_t __strobe_PyCodeObject_varnames = offsetof(PyCodeObject, co_varnames);
int64_t __strobe_PyCoroObject_cr_awaiter =
    offsetof(PyCoroObject, ci_cr_awaiter);
int64_t __strobe_PyFrameObject_back = offsetof(PyFrameObject, f_back);
int64_t __strobe_PyFrameObject_code = offsetof(PyFrameObject, f_code);
int64_t __strobe_PyFrameObject_gen = offsetof(PyFrameObject, f_gen);
int64_t __strobe_PyFrameObject_lineno = offsetof(PyFrameObject, f_lineno);
int64_t __strobe_PyFrameObject_localsplus =
    offsetof(PyFrameObject, f_localsplus);
int64_t __strobe_PyGenObject_code = offsetof(PyGenObject, gi_code);
int64_t __strobe_PyGenObject_gi_shadow_frame =
    offsetof(PyGenObject, gi_shadow_frame);
int64_t __strobe_PyObject_type = offsetof(PyObject, ob_type);
int64_t __strobe_PyThreadState_frame = offsetof(PyThreadState, frame);
int64_t __strobe_PyThreadState_shadow_frame =
    offsetof(PyThreadState, shadow_frame);
int64_t __strobe_PyThreadState_thread = offsetof(PyThreadState, thread_id);
int64_t __strobe_PyTupleObject_item = offsetof(PyTupleObject, ob_item);
int64_t __strobe_PyTypeObject_name = offsetof(PyTypeObject, tp_name);
int64_t __strobe_String_data = sizeof(PyASCIIObject);
int64_t __strobe_String_size = offsetof(PyVarObject, ob_size);
int64_t __strobe_TCurrentState_offset =
    offsetof(_PyRuntimeState, gilstate.tstate_current);
int64_t __strobe_TLSKey_offset =
    offsetof(_PyRuntimeState, gilstate.autoTSSkey._key);
int64_t __strobe__PyShadowFrame_PYSF_CODE_RT = PYSF_CODE_RT;
int64_t __strobe__PyShadowFrame_PYSF_PYCODE = PYSF_RTFS;
int64_t __strobe__PyShadowFrame_PYSF_RTFS = PYSF_RTFS;
int64_t __strobe__PyShadowFrame_PYSF_PYFRAME = PYSF_PYFRAME;
int64_t __strobe__PyShadowFrame_PtrKindMask = _PyShadowFrame_PtrKindMask;
int64_t __strobe__PyShadowFrame_PtrMask = _PyShadowFrame_PtrMask;
int64_t __strobe__PyShadowFrame_data = offsetof(_PyShadowFrame, data);
int64_t __strobe__PyShadowFrame_prev = offsetof(_PyShadowFrame, prev);
int64_t __strobe_RuntimeFrameState_py_code =
    jit::RuntimeFrameState::codeOffset();
