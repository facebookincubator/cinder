#ifndef Py_INTERNAL_LAZYIMPORTOBJECT_H
#define Py_INTERNAL_LAZYIMPORTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif


typedef struct {
    PyObject_HEAD
    PyObject *lz_from;
    PyObject *lz_attr;
} PyLazyImportObject;


PyAPI_FUNC(PyObject *) _PyLazyImport_GetName(PyObject *lazy_import);
PyAPI_FUNC(PyObject *) _PyLazyImport_New(PyObject *from, PyObject *attr);


#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_LAZYIMPORTOBJECT_H */
