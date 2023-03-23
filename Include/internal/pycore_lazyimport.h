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
    PyObject *lz_lazy_import;
    PyObject *lz_name;
    PyObject *lz_globals;
    PyObject *lz_locals;
    PyObject *lz_fromlist;
    PyObject *lz_level;
    PyObject *lz_resolved;
    PyObject *lz_resolving;
} PyLazyImportObject;


PyAPI_FUNC(PyObject *) _PyLazyImport_GetName(PyObject *lazy_import);
CiAPI_FUNC(PyObject *) _PyLazyImport_NewModule(PyObject *name,
                                               PyObject *globals,
                                               PyObject *locals,
                                               PyObject *fromlist,
                                               PyObject *level);
CiAPI_FUNC(PyObject *) _PyLazyImport_NewObject(PyObject *from, PyObject *name);


#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_LAZYIMPORTOBJECT_H */
