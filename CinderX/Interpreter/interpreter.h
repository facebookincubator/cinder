#pragma once

#include "Python.h"
#include "cinder/ci_api.h"

#ifdef __cplusplus
extern "C" {
#endif

CiAPI_FUNC(PyObject *) Ci_GetAIter(PyThreadState *tstate, PyObject *obj);
CiAPI_FUNC(PyObject *) Ci_GetANext(PyThreadState *tstate, PyObject *aiter);
CiAPI_FUNC(void) PyEntry_init(PyFunctionObject *func);
CiAPI_FUNC(PyObject*) _Py_HOT_FUNCTION Ci_EvalFrame(PyThreadState *tstate, PyFrameObject *f, int throwflag);

#ifdef __cplusplus
}
#endif
