#pragma once

#define Ci_Py_AWAITED_CALL_MARKER  ((size_t)1 << (8 * sizeof(size_t) - 2))
#define Ci_Py_AWAITED_CALL(n) ((n)&Ci_Py_AWAITED_CALL_MARKER)
#define Ci_Py_VECTORCALL_INVOKED_STATICALLY_BIT_POS  (8 * sizeof(size_t) - 3)
#define Ci_Py_VECTORCALL_INVOKED_STATICALLY                                     \
    ((size_t)1 << Ci_Py_VECTORCALL_INVOKED_STATICALLY_BIT_POS)
#define Ci_Py_VECTORCALL_INVOKED_METHOD ((size_t)1 << (8 * sizeof(size_t) - 4))
#define Ci_Py_VECTORCALL_INVOKED_CLASSMETHOD ((size_t)1 << (8 * sizeof(size_t) - 5))
#define Ci_Py_VECTORCALL_ARGUMENT_MASK                                           \
    (Ci_Py_AWAITED_CALL_MARKER |              \
      Ci_Py_VECTORCALL_INVOKED_STATICALLY | Ci_Py_VECTORCALL_INVOKED_METHOD |     \
      Ci_Py_VECTORCALL_INVOKED_CLASSMETHOD)

/* Same as PyVectorcall_Call but allows passing extra flags to function being called */
CiAPI_FUNC(PyObject *) Ci_PyVectorcall_Call_WithFlags(
    PyObject *callable, PyObject *tuple, PyObject *kwargs, size_t flags);
