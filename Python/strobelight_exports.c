// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Python.h"

#include "frameobject.h"
#include "internal/pycore_shadow_frame.h"
#include "internal/pycore_runtime.h"

CiAPI_DATA(int32_t) __strobe_PyVersion_major;
CiAPI_DATA(int32_t) __strobe_PyVersion_micro;
CiAPI_DATA(int32_t) __strobe_PyVersion_minor;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_co_flags;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_filename;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_name;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_qualname;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_varnames;
// Not using "ci_cr_awaiter" for backward compatability with existing
// Strobelight symbol lookup.
CiAPI_DATA(int64_t) __strobe_PyCoroObject_cr_awaiter;
CiAPI_DATA(int64_t) __strobe_PyFrameObject_back;
CiAPI_DATA(int64_t) __strobe_PyFrameObject_code;
CiAPI_DATA(int64_t) __strobe_PyFrameObject_gen;
CiAPI_DATA(int64_t) __strobe_PyFrameObject_lineno;
CiAPI_DATA(int64_t) __strobe_PyFrameObject_localsplus;
CiAPI_DATA(int64_t) __strobe_PyGenObject_code;
CiAPI_DATA(int64_t) __strobe_PyGenObject_gi_shadow_frame;
CiAPI_DATA(int64_t) __strobe_PyObject_type;
CiAPI_DATA(int64_t) __strobe_PyThreadState_frame;
CiAPI_DATA(int64_t) __strobe_PyThreadState_shadow_frame;
CiAPI_DATA(int64_t) __strobe_PyThreadState_thread;
CiAPI_DATA(int64_t) __strobe_PyTupleObject_item;
CiAPI_DATA(int64_t) __strobe_PyTypeObject_name;
CiAPI_DATA(int64_t) __strobe_String_data;
CiAPI_DATA(int64_t) __strobe_String_size;
CiAPI_DATA(int64_t) __strobe_TCurrentState_offset;
CiAPI_DATA(int64_t) __strobe_TLSKey_offset;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_CODE_RT;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_PYCODE;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_RTFS;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_PYFRAME;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_PtrKindMask;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_PtrMask;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_data;
CiAPI_DATA(int64_t) __strobe__PyShadowFrame_prev;
CiAPI_DATA(int64_t) __strobe_PyGIL_offset;
CiAPI_DATA(int64_t) __strobe_PyGIL_last_holder;
CiAPI_DATA(int64_t) __strobe_PyFrameObject_lasti;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_firstlineno;
CiAPI_DATA(int64_t) __strobe_PyCodeObject_linetable;
CiAPI_DATA(int64_t) __strobe_PyBytesObject_data;
CiAPI_DATA(int64_t) __strobe_PyVarObject_size;


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
int64_t __strobe_PyFrameObject_lineno =
    offsetof(PyFrameObject, f_lineno); // unused
int64_t __strobe_PyFrameObject_localsplus =
    offsetof(PyFrameObject, f_localsplus);
int64_t __strobe_PyGenObject_code = offsetof(PyGenObject, gi_code); // unused
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
int64_t __strobe_String_size = offsetof(PyVarObject, ob_size); // unused
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

int64_t __strobe_PyGIL_offset = offsetof(_PyRuntimeState, ceval.gil.locked);
int64_t __strobe_PyGIL_last_holder =
    offsetof(_PyRuntimeState, ceval.gil.last_holder);

int64_t __strobe_PyFrameObject_lasti = offsetof(PyFrameObject, f_lasti);
int64_t __strobe_PyCodeObject_firstlineno =
    offsetof(PyCodeObject, co_firstlineno);
int64_t __strobe_PyCodeObject_linetable = offsetof(PyCodeObject, co_linetable);
int64_t __strobe_PyBytesObject_data = offsetof(PyBytesObject, ob_sval);
int64_t __strobe_PyVarObject_size = offsetof(PyVarObject, ob_size);

// These values are actually 0. We assert this at CinderX initialization.
int64_t __strobe_CodeRuntime_py_code = 0;
int64_t __strobe_RuntimeFrameState_py_code = 0;
