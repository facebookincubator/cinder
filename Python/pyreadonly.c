
/* Readonlyness checking logic */

#include "Python.h"

#include "longobject.h"
#include "object.h"
#include "pyreadonly.h"
#include "py_immutable_error.h"
#include "frameobject.h"
#include "funcobject.h"

#include "internal/pycore_shadow_frame.h"
#include "internal/pycore_pystate.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PY_READONLY_IN_OPERATION_FLAG 0x80
#define PY_READONLY_RETURN_TYPE_FLAG 0x40
#define PY_READONLY_ARGUMENTS_MASK \
    (~(PY_READONLY_IN_OPERATION_FLAG | PY_READONLY_RETURN_TYPE_FLAG))

#ifdef PYREADONLY_ENABLED
typedef unsigned char PyReadonlyOperationMask;

static PyReadonlyOperationMask _PyReadonly_GetCurrentOperationMask(void) {
    PyFrameObject *frame = NULL;
    PyThreadState *tstate = _PyThreadState_GET();
    _PyShadowFrame* shadow_frame = tstate->shadow_frame;
    if (shadow_frame != NULL) {
        if (_PyShadowFrame_GetPtrKind(shadow_frame) == PYSF_PYFRAME) {
            frame = _PyShadowFrame_GetPyFrame(shadow_frame);
        } else {
            // Not a full frame, so definitely not a readonly op, as
            // SetCurrentOperationMask currently forces the materialization of
            // a full PyFrameObject. T116253972 tracks making that not required.
            return 0;
        }
    } else {
        frame = PyEval_GetFrame();
    }

    if (frame == NULL) {
        // No frame exists, which means we're likely in the constant
        // folding pass, which doesn't know anything about readonly
        // and won't try to fold readonly values.
        return 0;
    }
    return frame->f_readonly_operation_mask;
}

static int _PyReadonly_SetCurrentOperationMask(int mask) {
    // This will force materialization of the PyFrameObject, which is fine for
    // now. Eventually this should be backed by storage in the shadow frame or
    // adjacent storage.
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        // No frame exists, which means we're likely in the constant
        // folding pass, which doesn't know anything about readonly
        // and won't try to fold readonly values.
        _PyErr_IMMUTABLE_ERR(ReadonlyOperatorInNonFrameContext);
        // This needs to always signal, even when enforcement is off, to ensure
        // things like constant folding don't fold out a readonly error.
        //
        // Since readonly code doesn't currently use the native C compiler,
        // this shouldn't actually happen, but it's better to handle possible
        // errors than to fail silently.
        PyErr_BadInternalCall();
        return -1;
    }
    frame->f_readonly_operation_mask = (unsigned char)mask;
    return 0;
}

/**
 * Do any booking required after an error ocurrs, and raise the actual error
 * if enforcement is enabled. The return value should be returned directly
 * to the caller. A non-zero return value means enforcement is enabled and
 * an error has been raised.
 */
static int _PyReadonly_DoError(void) {
    int ret = _PyReadonly_SetCurrentOperationMask(0);
    if (ret != 0) {
        return ret;
    }

    // TODO: When flags for enabling enforcement are added, check them here,
    // and return non-zero to signal the error has been raised.
    return 0;
}

#define PYREADONLY_CHECK_ONLY 1
#define PYREADONLY_RAISE_ERRORS 0
/**
 * Do the actual readonly check, emitting warnings as appropriate.
 *
 * If checkOnly is nonzero, no errors will be raised, and the current
 * operation state will be untouched.
 */
static int _PyReadonly_DoReadonlyCheck(int checkOnly, PyReadonlyOperationMask operationMask, int functionArgsMask, int functionReturnsReadonly) {
    PyReadonlyOperationMask operationArgs = operationMask & PY_READONLY_ARGUMENTS_MASK;

    if ((operationArgs & (~functionArgsMask & PY_READONLY_ARGUMENTS_MASK)) != 0) {
        if (checkOnly == PYREADONLY_CHECK_ONLY) {
            return -1;
        } else {
            // TODO: Should re-use the function call error reporting when that lands.
            _PyErr_IMMUTABLE_ERR(ReadonlyOperatorArgumentReadonlyMismatch);
            return _PyReadonly_DoError();
        }
    }

    if (functionReturnsReadonly == PYREADONLY_RETURN_READONLY_IS_TRANSITIVE) {
        functionReturnsReadonly = operationArgs != 0;
    }

    if (functionReturnsReadonly && (operationMask & PY_READONLY_RETURN_TYPE_FLAG) == 0) {
        if (checkOnly == PYREADONLY_CHECK_ONLY) {
            return -1;
        } else {
            _PyErr_IMMUTABLE_ERR(ReadonlyOperatorReturnsReadonlyMismatch);
            return _PyReadonly_DoError();
        }
    }

    if (checkOnly == PYREADONLY_CHECK_ONLY) {
        return 0;
    } else {
        return _PyReadonly_SetCurrentOperationMask(0);
    }
}

int PyReadonly_BeginReadonlyOperation(int mask) {
    PyReadonlyOperationMask currentMask = _PyReadonly_GetCurrentOperationMask();
    if ((currentMask & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        PyObject *curMaskObj = PyLong_FromLong(currentMask);
        PyObject *newMaskObj = PyLong_FromLong((unsigned int)mask);
        _PyErr_IMMUTABLE_ERR(ReadonlyOperatorAlreadyInProgress, curMaskObj, newMaskObj);
        Py_DECREF(curMaskObj);
        Py_DECREF(newMaskObj);
        // Always signal on this error and don't touch the current mask,
        // even if enforcement is off, because logic state beyond this point
        // can't be guaranteed. Raising back out to the code that set the
        // initial operation flags should bring us back to a usable state
        // when it calls into PyReadonly_VerifyReadonlyOperationCompleted()
        return -1;
    }

    return _PyReadonly_SetCurrentOperationMask(mask | PY_READONLY_IN_OPERATION_FLAG);
}

int PyReadonly_MaybeBeginReadonlyOperation(int originalOperation, int returnsReadonly, int argMask) {
    if ((originalOperation & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        int newMask = returnsReadonly != 0 ? PY_READONLY_RETURN_TYPE_FLAG : 0;
        newMask |= argMask;
        return PyReadonly_BeginReadonlyOperation(newMask);
    }
    return 0;
}

int PyReadonly_ReorderCurrentOperationArgs2(void) {
    PyReadonlyOperationMask currentMask = _PyReadonly_GetCurrentOperationMask();
    if ((currentMask & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        if ((currentMask & PY_READONLY_ARGUMENTS_MASK) != (currentMask & 0x03)) {
            // PyErr("TOO MANY ARGS")
            return -1;
        }
        PyReadonlyOperationMask newArgMask = ((currentMask & 0x01) << 1) | ((currentMask & 0x02) >> 1);
        currentMask &= ~PY_READONLY_ARGUMENTS_MASK;
        return _PyReadonly_SetCurrentOperationMask(currentMask | newArgMask);
    }
    return 0;
}

int PyReadonly_ReorderCurrentOperationArgs3(int newArg1Pos, int newArg2Pos, int newArg3Pos) {
    PyReadonlyOperationMask currentMask = _PyReadonly_GetCurrentOperationMask();
    if ((currentMask & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        if ((currentMask & PY_READONLY_ARGUMENTS_MASK) != (currentMask & 0x07)) {
            // PyErr("TOO MANY ARGS")
            return -1;
        }
        // This check should be trivially eliminated when inlining with LTO enabled.
        if (newArg1Pos <= 0 || newArg2Pos <= 0 || newArg3Pos <= 0 ||
            newArg1Pos > 3 || newArg2Pos > 3 || newArg3Pos > 3 ||
            newArg1Pos == newArg2Pos || newArg2Pos == newArg3Pos || newArg3Pos == newArg1Pos) {
            // PyErr("Invalid argument positions!")
            return -1;
        }
        PyReadonlyOperationMask newArgMask = 0;
        newArgMask |= (currentMask & 0x01) << (newArg1Pos - 1);
        newArgMask |= ((currentMask & 0x02) >> 1) << (newArg2Pos - 1);
        newArgMask |= ((currentMask & 0x04) >> 2) << (newArg3Pos - 1);
        currentMask &= ~PY_READONLY_ARGUMENTS_MASK;
        return _PyReadonly_SetCurrentOperationMask(currentMask | newArgMask);
    }
    return 0;
}

int PyReadonly_SaveCurrentReadonlyOperation(int* savedOperation) {
    PyReadonlyOperationMask currentMask = _PyReadonly_GetCurrentOperationMask();
    if ((currentMask & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        *savedOperation = currentMask;
        return 0;
    }
    *savedOperation = 0;
    return 0;
}

int PyReadonly_RestoreCurrentReadonlyOperation(int savedOperation) {
    if ((savedOperation & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        return _PyReadonly_SetCurrentOperationMask(savedOperation);
    }
    return 0;
}

int PyReadonly_SuspendCurrentReadonlyOperation(int* suspendedOperation) {
    PyReadonlyOperationMask currentMask = _PyReadonly_GetCurrentOperationMask();
    if ((currentMask & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        *suspendedOperation = currentMask;
        return _PyReadonly_SetCurrentOperationMask(0);
    }
    *suspendedOperation = 0;
    return 0;
}

int PyReadonly_IsReadonlyOperationValid(int operationMask, int functionArgsMask, int functionReturnsReadonly) {
    return _PyReadonly_DoReadonlyCheck(PYREADONLY_CHECK_ONLY, operationMask, functionArgsMask, functionReturnsReadonly);
}

int PyReadonly_IsTransitiveReadonlyOperationValid(int operationMask, int argCount) {
    int functionArgsMask = (1 << argCount) - 1;
    return _PyReadonly_DoReadonlyCheck(PYREADONLY_CHECK_ONLY, operationMask, functionArgsMask, PYREADONLY_RETURN_READONLY_IS_TRANSITIVE);
}

int PyReadonly_CheckReadonlyOperation(int functionArgsMask, int functionReturnsReadonly) {
    PyReadonlyOperationMask operationMask = _PyReadonly_GetCurrentOperationMask();
    if ((operationMask & PY_READONLY_IN_OPERATION_FLAG) == 0) {
        return 0;
    }
    return _PyReadonly_DoReadonlyCheck(PYREADONLY_RAISE_ERRORS, operationMask, functionArgsMask, functionReturnsReadonly);
}

int PyReadonly_CheckTransitiveReadonlyOperation(int argCount) {
    PyReadonlyOperationMask operationMask = _PyReadonly_GetCurrentOperationMask();
    if ((operationMask & PY_READONLY_IN_OPERATION_FLAG) == 0) {
        return 0;
    }

    int functionArgsMask = (1 << argCount) - 1;
    return _PyReadonly_DoReadonlyCheck(PYREADONLY_RAISE_ERRORS, operationMask, functionArgsMask, PYREADONLY_RETURN_READONLY_IS_TRANSITIVE);
}

int PyReadonly_CheckReadonlyOperationOnCallable(PyObject* callable) {
    PyReadonlyOperationMask operationMask = _PyReadonly_GetCurrentOperationMask();
    if ((operationMask & PY_READONLY_IN_OPERATION_FLAG) == 0) {
        return 0;
    }

    if (PyFunction_Check(callable)) {
        PyFunctionObject *funcObj = (PyFunctionObject*)callable;
        int functionArgsMask = CLEAR_NONARG_READONLY_MASK(funcObj->readonly_mask);
        int functionReturnsReadonly = RETURNS_READONLY(funcObj->readonly_mask);
        return _PyReadonly_DoReadonlyCheck(PYREADONLY_RAISE_ERRORS, operationMask, functionArgsMask, functionReturnsReadonly);
    } else {
        _PyErr_IMMUTABLE_ERR(ReadonlyOperatorCallOnUnknownCallableType);
        return _PyReadonly_DoError();
    }
}

int PyReadonly_VerifyReadonlyOperationCompleted(void) {
    PyReadonlyOperationMask operationMask = _PyReadonly_GetCurrentOperationMask();
    if ((operationMask & PY_READONLY_IN_OPERATION_FLAG) != 0) {
        PyObject* cur_mask_obj = PyLong_FromLong(operationMask);
        _PyErr_IMMUTABLE_ERR(ReadonlyOperatorCheckNotRan, cur_mask_obj);
        Py_DECREF(cur_mask_obj);
        return _PyReadonly_DoError();
    }
    return 0;
}
#endif

void PyReadonly_Check_LoadAttr(PyObject *obj, int check_return,
                               int check_read) {
  assert(obj);
  PyTypeObject *type = Py_TYPE(obj);

  if (check_read &&
      PyType_HasFeature(type, Py_TPFLAG_READONLY_SIDE_EFFECT_DESCR)) {

    _PyErr_IMMUTABLE_ERR(ReadonlyAttributeAccess);
  }
  if (check_return &&
      PyType_HasFeature(type, Py_TPFLAG_DESCR_RETURNS_READONLY)) {
    _PyErr_IMMUTABLE_ERR(ReadonlyAttributeAccessReturnReadonly);
  }
}

#ifdef __cplusplus
}
#endif
