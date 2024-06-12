#ifndef Py_CPYTHON_IMPORT_H
#  error "this header file must not be included directly"
#endif

PyMODINIT_FUNC PyInit__imp(void);

PyAPI_FUNC(int) _PyImport_IsInitialized(PyInterpreterState *);

PyAPI_FUNC(PyObject *) _PyImport_GetModule(PyThreadState *tstate, PyObject *name);
PyAPI_FUNC(PyObject *) _PyImport_GetModuleId(_Py_Identifier *name);
PyAPI_FUNC(int) _PyImport_SetModule(PyObject *name, PyObject *module);
PyAPI_FUNC(int) _PyImport_SetModuleString(const char *name, PyObject* module);

PyAPI_FUNC(void) _PyImport_AcquireLock(PyInterpreterState *interp);
PyAPI_FUNC(int) _PyImport_ReleaseLock(PyInterpreterState *interp);

PyAPI_FUNC(int) _PyImport_FixupBuiltin(
    PyObject *mod,
    const char *name,            /* UTF-8 encoded string */
    PyObject *modules
    );
PyAPI_FUNC(int) _PyImport_FixupExtensionObject(PyObject*, PyObject *,
                                               PyObject *, PyObject *);

// START META PATCH (expose C API to call a module init function for statically linked extensions)
PyAPI_FUNC(PyObject *) _Ci_PyImport_CallInitFuncWithContext(const char* context,
                                                            PyObject* (*initfunc)(void));
// END META PATCH

PyAPI_FUNC(int) _PyImport_IsLazyImportsActive(PyThreadState *tstate);

PyAPI_FUNC(int) PyImport_IsLazyImportsEnabled(void);
PyAPI_FUNC(PyObject *) PyImport_SetLazyImports(
    PyObject *enabled, PyObject *excluding, PyObject *eager);
PyAPI_FUNC(PyObject *) _PyImport_SetLazyImportsInModule(
    PyObject *enabled);

struct _inittab {
    const char *name;           /* ASCII encoded string */
    PyObject* (*initfunc)(void);
};
// This is not used after Py_Initialize() is called.
PyAPI_DATA(struct _inittab *) PyImport_Inittab;
PyAPI_FUNC(int) PyImport_ExtendInittab(struct _inittab *newtab);

struct _frozen {
    const char *name;                 /* ASCII encoded string */
    const unsigned char *code;
    int size;
    int is_package;
    PyObject *(*get_code)(void);
};

/* Embedding apps may change this pointer to point to their favorite
   collection of frozen modules: */

PyAPI_DATA(const struct _frozen *) PyImport_FrozenModules;

PyAPI_DATA(PyObject *) _PyImport_GetModuleAttr(PyObject *, PyObject *);
PyAPI_DATA(PyObject *) _PyImport_GetModuleAttrString(const char *, const char *);
