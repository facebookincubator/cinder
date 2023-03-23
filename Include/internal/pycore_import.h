#ifndef Py_LIMITED_API
#ifndef Py_INTERNAL_IMPORT_H
#define Py_INTERNAL_IMPORT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_FORK
extern PyStatus _PyImport_ReInitLock(void);
#endif
extern PyObject* _PyImport_BootstrapImp(PyThreadState *tstate);

PyAPI_FUNC(PyObject *) _PyImport_LoadLazyImport(PyObject *lazy_import, int deep);

PyAPI_FUNC(PyObject *) _PyImport_LazyImportName(PyObject *builtins,
                                                PyObject *globals,
                                                PyObject *locals,
                                                PyObject *name,
                                                PyObject *fromlist,
                                                PyObject *level);

PyAPI_FUNC(PyObject *) _PyImport_EagerImportName(PyObject *builtins,
                                                 PyObject *globals,
                                                 PyObject *locals,
                                                 PyObject *name,
                                                 PyObject *fromlist,
                                                 PyObject *level);

CiAPI_FUNC(PyObject *) _PyImport_ImportFrom(PyThreadState *tstate,
                                            PyObject *v,
                                            PyObject *name);

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_IMPORT_H */
#endif /* !Py_LIMITED_API */
