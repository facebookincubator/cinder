#ifndef Py_ARRAYMODULE_H
#define Py_ARRAYMODULE_H
#ifdef __cplusplus
extern "C" {
#endif
struct arrayobject; /* Forward */
PyAPI_DATA(PyTypeObject) PyArray_Type;

#define PyStaticArray_Check(op) PyObject_TypeCheck(op, &PyArray_Type)
#define PyStaticArray_CheckExact(op) (Py_TYPE(op) == &PyArray_Type)

struct arraydescr {
    char typecode;
    int itemsize;
    PyObject * (*getitem)(struct arrayobject *, Py_ssize_t);
    int (*setitem)(struct arrayobject *, Py_ssize_t, PyObject *);
    int (*compareitems)(const void *, const void *, Py_ssize_t);
    const char *formats;
    int is_integer_type;
    int is_signed;

    int (*setitem_signed)(struct arrayobject *, Py_ssize_t, int64_t);
    int (*setitem_unsigned)(struct arrayobject *, Py_ssize_t, uint64_t);
};

typedef struct arraydescr PyArrayDescriptor;

typedef struct arrayobject {
    PyObject_VAR_HEAD
    char *ob_item;
    Py_ssize_t allocated;
    const struct arraydescr *ob_descr;
    PyObject *weakreflist; /* List of weak references */
    int ob_exports;  /* Number of exported buffers */
} arrayobject;

typedef arrayobject PyStaticArrayObject;

PyObject *_PyArray_GetItem(PyObject *, Py_ssize_t);

int _PyArray_SetItem(PyObject *, Py_ssize_t, PyObject *);

int _PyArray_AppendSigned(PyObject *, int64_t);

int _PyArray_AppendUnsigned(PyObject *, uint64_t);

#ifdef __cplusplus
}
#endif
#endif /* !Py_ARRAYMODULE_H */
