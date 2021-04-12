#ifndef Py_CPYTHON_TUPLEOBJECT_H
#  error "this header file must not be included directly"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PyObject_VAR_HEAD
    /* ob_item contains space for 'ob_size' elements.
       Items must normally not be NULL, except during construction when
       the tuple is not yet visible outside the function that builds it. */
    PyObject *ob_item[1];
} PyTupleObject;

PyAPI_FUNC(int) _PyTuple_Resize(PyObject **, Py_ssize_t);
PyAPI_FUNC(void) _PyTuple_MaybeUntrack(PyObject *);
PyAPI_FUNC(PyObject *) _PyTuple_FromArrayNoTrack(PyObject *const *, Py_ssize_t);
PyAPI_FUNC(PyObject *) _PyTuple_NewNoTrack(Py_ssize_t);
PyAPI_FUNC(Py_hash_t) _PyTuple_HashStack(PyObject *const *p, Py_ssize_t len);
PyAPI_FUNC(PyObject *) _PyTuple_Repeat(PyTupleObject *, Py_ssize_t);

#define PyTupleDECREF_MAYBE_TRACK(t)                                    \
    do {                                                                \
        if (Py_REFCNT((t)) > 1) {                                       \
            Py_DECREF((t));                                             \
            if (PyTuple_GET_SIZE((t)) > 0)                              \
                _PyObject_GC_TRACK((t));                                \
        }                                                               \
        else {                                                          \
            Py_DECREF((t));                                             \
        }                                                               \
    } while (0)

/* Macros trading safety for speed */

/* Cast argument to PyTupleObject* type. */
#define _PyTuple_CAST(op) (assert(PyTuple_Check(op)), (PyTupleObject *)(op))

#define PyTuple_GET_SIZE(op)    Py_SIZE(_PyTuple_CAST(op))

#define PyTuple_GET_ITEM(op, i) (_PyTuple_CAST(op)->ob_item[i])

/* Macro, *only* to be used to fill in brand new tuples */
#define PyTuple_SET_ITEM(op, i, v) (_PyTuple_CAST(op)->ob_item[i] = v)

PyAPI_FUNC(void) _PyTuple_DebugMallocStats(FILE *out);

PyAPI_FUNC(PyObject *) _PyTuple_Subscript(PyObject *self, PyObject *item);

#ifdef __cplusplus
}
#endif
