// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(int32_t) __strobe_PyVersion_major;
PyAPI_DATA(int32_t) __strobe_PyVersion_micro;
PyAPI_DATA(int32_t) __strobe_PyVersion_minor;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_co_flags;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_filename;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_name;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_qualname;
PyAPI_DATA(int64_t) __strobe_PyCodeObject_varnames;
// Not using "ci_cr_awaiter" for backward compatability with existing
// Strobelight symbol lookup.
PyAPI_DATA(int64_t) __strobe_PyCoroObject_cr_awaiter;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_back;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_code;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_gen;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_lineno;
PyAPI_DATA(int64_t) __strobe_PyFrameObject_localsplus;
PyAPI_DATA(int64_t) __strobe_PyGenObject_code;
PyAPI_DATA(int64_t) __strobe_PyGenObject_gi_shadow_frame;
PyAPI_DATA(int64_t) __strobe_PyObject_type;
PyAPI_DATA(int64_t) __strobe_PyThreadState_frame;
PyAPI_DATA(int64_t) __strobe_PyThreadState_shadow_frame;
PyAPI_DATA(int64_t) __strobe_PyThreadState_thread;
PyAPI_DATA(int64_t) __strobe_PyTupleObject_item;
PyAPI_DATA(int64_t) __strobe_PyTypeObject_name;
PyAPI_DATA(int64_t) __strobe_String_data;
PyAPI_DATA(int64_t) __strobe_String_size;
PyAPI_DATA(int64_t) __strobe_TCurrentState_offset;
PyAPI_DATA(int64_t) __strobe_TLSKey_offset;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_CODE_RT;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_PYCODE;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_RTFS;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_PYSF_PYFRAME;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_PtrKindMask;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_PtrMask;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_data;
PyAPI_DATA(int64_t) __strobe__PyShadowFrame_prev;
PyAPI_DATA(int64_t) __strobe_RuntimeFrameState_py_code;
PyAPI_DATA(int64_t) __strobe_CodeRuntime_py_code;

#ifdef __cplusplus
}
#endif
